#include "core/io/video_decoder.hpp"
#include "core/exception.hpp"
#include <borealis.hpp>

#ifdef BOREALIS_USE_DEKO3D
extern "C"
{
#include <libavutil/hwcontext_nvtegra.h>
}
#endif

VideoDecoder::VideoDecoder()
{
}

VideoDecoder::~VideoDecoder()
{
    cleanup();
}

bool VideoDecoder::initCodec(bool is_PS5, int width, int height)
{
    brls::Logger::info("VideoDecoder: loading AVCodec");

    m_is_hevc = is_PS5;
    if (is_PS5)
    {
        m_codec = avcodec_find_decoder_by_name("hevc");
    }
    else
    {
        m_codec = avcodec_find_decoder_by_name("h264");
    }

    if (!m_codec)
    {
        throw Exception("Codec not available");
    }

    brls::Logger::info("VideoDecoder: got codec {}", m_codec->name);

    m_codec_context = avcodec_alloc_context3(m_codec);
    if (!m_codec_context)
    {
        throw Exception("Failed to alloc codec context");
    }

    if (m_hw_accel_enabled)
    {
        m_codec_context->skip_loop_filter = AVDISCARD_ALL;
        m_codec_context->flags |= AV_CODEC_FLAG_LOW_DELAY;
        m_codec_context->flags2 |= AV_CODEC_FLAG2_FAST;
        m_codec_context->thread_type = FF_THREAD_FRAME;
        m_codec_context->thread_count = 1;
#ifdef BOREALIS_USE_DEKO3D
        // Create HW device context BEFORE opening codec 
	brls::Logger::info("VideoDecoder: Creating NVTegra hardware device context...");
        if (av_hwdevice_ctx_create(&m_hw_device_ctx, AV_HWDEVICE_TYPE_NVTEGRA, NULL, NULL, 0) < 0)
        {
            throw Exception("Failed to create NVTegra hardware device context");
        }
        m_codec_context->hw_device_ctx = av_buffer_ref(m_hw_device_ctx);
        brls::Logger::info("VideoDecoder: NVTegra hw device context created: {}", (void*)m_hw_device_ctx);

        if (width > 0 && height > 0)
        {
            m_codec_context->width = width;
            m_codec_context->height = height;
            m_codec_context->pix_fmt = AV_PIX_FMT_NVTEGRA;
            brls::Logger::info("VideoDecoder: Setting NVTEGRA format {}x{}", width, height);
        }
#endif
    }
    else
    {
        m_codec_context->flags |= AV_CODEC_FLAG_LOW_DELAY;
        m_codec_context->flags2 |= AV_CODEC_FLAG2_FAST;
        m_codec_context->thread_type = FF_THREAD_SLICE;
        m_codec_context->thread_count = 4;
    }

    // Open codec AFTER hw_device_ctx is attached (so FFmpeg knows to use HW acceleration)
    if (avcodec_open2(m_codec_context, m_codec, nullptr) < 0)
    {
        avcodec_free_context(&m_codec_context);
        throw Exception("Failed to open codec context");
    }

    return true;
}

bool VideoDecoder::initVideo(int video_width, int video_height, int screen_width, int screen_height)
{
    brls::Logger::info("VideoDecoder: initVideo {}x{}", video_width, video_height);

    m_video_width = video_width;
    m_video_height = video_height;
    m_screen_width = screen_width;
    m_screen_height = screen_height;

    // Wait for IDR frame at session start
    // PS5 may resume with P-frames; returning false triggers CORRUPTFRAME → keyframe request
    m_waiting_for_idr = true;
    brls::Logger::info("VideoDecoder: Waiting for IDR frame to start decoding");

    m_frame_queue.setLimit(10);

    m_tmp_frame = av_frame_alloc();
    if (!m_tmp_frame)
    {
        brls::Logger::error("VideoDecoder: Failed to allocate temp frame");
        return false;
    }

    return true;
}

bool VideoDecoder::decode(uint8_t* buf, size_t buf_size)
{
    bool has_idr = scanNALUnits(buf, buf_size);

    bool has_all_params = m_is_hevc ? (m_has_vps && m_has_sps && m_has_pps)
                                     : (m_has_sps && m_has_pps);

    if (m_waiting_for_idr)
    {
        if (has_all_params && has_idr)
        {
            brls::Logger::info("VideoDecoder: Got complete keyframe (params + IDR), resuming decode");
            m_waiting_for_idr = false;
        }
        else
        {
            static uint32_t idr_wait_count = 0;
            if (idr_wait_count++ % 10 == 0)
            {
                if (m_is_hevc)
                {
                    brls::Logger::info("VideoDecoder: Waiting for keyframe (VPS={}, SPS={}, PPS={}, IDR={}) skip #{}",
                        m_has_vps, m_has_sps, m_has_pps, has_idr, idr_wait_count);
                }
                else
                {
                    brls::Logger::info("VideoDecoder: Waiting for keyframe (SPS={}, PPS={}, IDR={}) skip #{}",
                        m_has_sps, m_has_pps, has_idr, idr_wait_count);
                }
            }
        }
    }

    AVPacket* packet = av_packet_alloc();
    packet->data = buf;
    packet->size = buf_size;

send_packet:
    int r = avcodec_send_packet(m_codec_context, packet);
    if (r != 0)
    {
        if (r == AVERROR(EAGAIN))
        {
            brls::Logger::error("AVCodec internal buffer is full removing frames before pushing");
            r = avcodec_receive_frame(m_codec_context, m_tmp_frame);
            if (r != 0)
            {
                brls::Logger::error("Failed to pull frame");
                av_packet_free(&packet);
                return false;
            }
            goto send_packet;
        }
        else
        {
            char errbuf[128];
            av_make_error_string(errbuf, sizeof(errbuf), r);
            brls::Logger::error("Failed to push frame: {}", errbuf);
            av_packet_free(&packet);
            return false;
        }
    }

    r = avcodec_receive_frame(m_codec_context, m_tmp_frame);

    if (r == 0)
    {
        m_frame_queue.push(m_tmp_frame);
    }
    else if (r != AVERROR(EAGAIN)) 
    {
        brls::Logger::warning("VideoDecoder: avcodec_receive_frame failed: {}", r);
    }

    av_packet_free(&packet);

    return true;
}

void VideoDecoder::flush()
{
    if (m_codec_context)
    {
        brls::Logger::warning("VideoDecoder: Flushing decoder, waiting for IDR");
        avcodec_flush_buffers(m_codec_context);
        m_waiting_for_idr = true;
    }
}

bool VideoDecoder::scanNALUnits(uint8_t* buf, size_t buf_size)
{
    if (buf_size < 5)
        return false;

    bool has_idr = false;
    bool prev_vps = m_has_vps, prev_sps = m_has_sps, prev_pps = m_has_pps;

    size_t offset = 0;
    while (offset + 4 < buf_size)
    {
        if (buf[offset] == 0x00 && buf[offset + 1] == 0x00)
        {
            if (buf[offset + 2] == 0x01)
                offset += 3;
            else if (buf[offset + 2] == 0x00 && offset + 3 < buf_size && buf[offset + 3] == 0x01)
                offset += 4;
            else
            {
                offset++;
                continue;
            }

            if (offset >= buf_size)
                break;

            if (m_is_hevc)
            {
                uint8_t nal_type = (buf[offset] >> 1) & 0x3F;
                if (nal_type == 32) m_has_vps = true;
                else if (nal_type == 33) m_has_sps = true;
                else if (nal_type == 34) m_has_pps = true;
                else if (nal_type == 19 || nal_type == 20) has_idr = true;
            }
            else
            {
                uint8_t nal_type = buf[offset] & 0x1F;
                if (nal_type == 7) m_has_sps = true;
                else if (nal_type == 8) m_has_pps = true;
                else if (nal_type == 5) has_idr = true;
            }
        }
        else
        {
            offset++;
        }
    }

    if (m_is_hevc)
    {
        if (!prev_vps && m_has_vps) brls::Logger::info("VideoDecoder: Received VPS");
        if (!prev_sps && m_has_sps) brls::Logger::info("VideoDecoder: Received SPS");
        if (!prev_pps && m_has_pps) brls::Logger::info("VideoDecoder: Received PPS");
    }
    else
    {
        if (!prev_sps && m_has_sps) brls::Logger::info("VideoDecoder: Received SPS");
        if (!prev_pps && m_has_pps) brls::Logger::info("VideoDecoder: Received PPS");
    }

    return has_idr;
}

void VideoDecoder::cleanup()
{
    m_frame_queue.cleanup();

    if (m_tmp_frame)
    {
        av_frame_free(&m_tmp_frame);
        m_tmp_frame = nullptr;
    }

    if (m_hw_device_ctx)
    {
        av_buffer_unref(&m_hw_device_ctx);
        m_hw_device_ctx = nullptr;
    }

    if (m_codec_context)
    {
        avcodec_free_context(&m_codec_context);
        m_codec_context = nullptr;
    }
}
