#ifndef AKIRA_IO_VIDEO_DECODER_HPP
#define AKIRA_IO_VIDEO_DECODER_HPP

#include <cstdint>
#include <chiaki/log.h>
#include "core/io/frame_queue.hpp"

extern "C"
{
#include <libavcodec/avcodec.h>
}

class VideoDecoder
{
public:
    VideoDecoder();
    ~VideoDecoder();

    // Disable copy
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    void setLogger(ChiakiLog* log) { m_log = log; }

    // Initialize codec (H.264 or HEVC for PS5)
    // For deko3d, width/height are required for NVTEGRA hardware decoder
    bool initCodec(bool is_PS5, int width = 0, int height = 0);

    // Initialize video with frame queue
    bool initVideo(int video_width, int video_height, int screen_width, int screen_height);

    // Decode a video packet (returns false if waiting for keyframe)
    bool decode(uint8_t* buf, size_t buf_size);

    // Flush decoder buffers and set waiting-for-keyframe state
    void flush();

    // Cleanup
    void cleanup();

    // Get frame queue for renderer access
    FrameQueue* getFrameQueue() { return &m_frame_queue; }

    int getVideoWidth() const { return m_video_width; }
    int getVideoHeight() const { return m_video_height; }
    bool isHEVC() const { return m_is_hevc; }
    bool isHardwareAccelerated() const { return m_hw_accel_enabled; }

private:
    ChiakiLog* m_log = nullptr;

    const AVCodec* m_codec = nullptr;
    AVCodecContext* m_codec_context = nullptr;
    AVBufferRef* m_hw_device_ctx = nullptr;
    AVFrame* m_tmp_frame = nullptr;

    FrameQueue m_frame_queue;

    int m_video_width = 0;
    int m_video_height = 0;
    int m_screen_width = 1280;
    int m_screen_height = 720;

    bool m_hw_accel_enabled = true;
    bool m_is_hevc = false;  // true for PS5 (HEVC), false for PS4 (H.264)
    bool m_waiting_for_idr = false;  // Skip packets until we get an IDR frame

    // Track received parameter sets (VPS/SPS/PPS)
    // Chiaki sends these separately from IDR slices
    bool m_has_vps = false;  // HEVC only
    bool m_has_sps = false;
    bool m_has_pps = false;

    // Scan packet for parameter sets and update flags (with logging)
    void updateParamFlags(uint8_t* buf, size_t buf_size);

    // Check if packet contains an IDR slice
    bool hasIDRSlice(uint8_t* buf, size_t buf_size);
};

#endif // AKIRA_IO_VIDEO_DECODER_HPP
