#ifndef AKIRA_IO_VIDEO_DECODER_HPP
#define AKIRA_IO_VIDEO_DECODER_HPP

#include <cstdint>
#include <functional>
#include <utility>
#include <chiaki/log.h>
#include <chrono>
#include <mutex>

extern "C"
{
#include <libavcodec/avcodec.h>
}

class VideoDecoder
{
public:
    using FrameReadyCallback = std::function<void(AVFrame*)>;

    VideoDecoder();
    ~VideoDecoder();

    // Disable copy
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    void setLogger(ChiakiLog* log) { m_log = log; }
    void setFrameReadyCallback(FrameReadyCallback callback) { m_frame_ready_callback = std::move(callback); }

    // Initialize codec (H.264 or HEVC for PS5)
    // For deko3d, width/height are required for NVTEGRA hardware decoder
    bool initCodec(bool is_PS5, int width = 0, int height = 0);

    // Initialize video with frame queue
    bool initVideo(int video_width, int video_height);

    // Decode a video packet (returns false if waiting for keyframe)
    bool decode(uint8_t* buf, size_t buf_size);

    // Flush decoder buffers and set waiting-for-keyframe state
    void flush();

    // Cleanup
    void cleanup();

    int getVideoWidth() const { return m_video_width; }
    int getVideoHeight() const { return m_video_height; }
    void updateResolution(int width, int height) { m_video_width = width; m_video_height = height; }
    bool isHEVC() const { return m_is_hevc; }
    bool isHardwareAccelerated() const { return m_hw_accel_enabled; }

    struct DecoderStats
    {
        float decode_ms = 0.0f;
        float source_fps = 0.0f;
        float jitter_ms = 0.0f;
        uint64_t decoder_drops = 0;
    };

    DecoderStats getStats() const;
    std::chrono::steady_clock::time_point lastEmitTime() const { return m_last_emit; }

private:
    void noteDecodeSample(std::chrono::steady_clock::duration d);
    void notePacketArrival(std::chrono::steady_clock::time_point now);
    void publishStats(std::chrono::steady_clock::time_point now);

    mutable std::mutex m_stats_mutex;
    DecoderStats m_stats;
    uint64_t m_drops = 0;
    std::chrono::steady_clock::time_point m_last_emit{};
    std::chrono::steady_clock::time_point m_last_packet{};
    std::chrono::steady_clock::time_point m_window_start{};
    double m_decode_us_accum = 0.0;
    uint64_t m_decode_sample_count = 0;
    uint64_t m_frames_emitted = 0;
    double m_interval_us_accum = 0.0;
    double m_interval_dev_accum = 0.0;
    uint64_t m_interval_count = 0;
    double m_interval_mean_us = 0.0;

    ChiakiLog* m_log = nullptr;

    const AVCodec* m_codec = nullptr;
    AVCodecContext* m_codec_context = nullptr;
    AVBufferRef* m_hw_device_ctx = nullptr;
    AVFrame* m_tmp_frame = nullptr;

    FrameReadyCallback m_frame_ready_callback;

    int m_video_width = 0;
    int m_video_height = 0;

    bool m_hw_accel_enabled = true;
    bool m_is_hevc = false;  // true for PS5 (HEVC), false for PS4 (H.264)
    bool m_waiting_for_idr = false;  // Skip packets until we get an IDR frame

    // Track received parameter sets (VPS/SPS/PPS)
    // Chiaki sends these separately from IDR slices
    bool m_has_vps = false;  // HEVC only
    bool m_has_sps = false;
    bool m_has_pps = false;

    bool scanNALUnits(uint8_t* buf, size_t buf_size);
};

#endif // AKIRA_IO_VIDEO_DECODER_HPP
