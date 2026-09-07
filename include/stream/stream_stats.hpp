#ifndef AKIRA_IO_STREAM_STATS_HPP
#define AKIRA_IO_STREAM_STATS_HPP

#include <cstdint>
#include <cstddef>

struct StreamStats
{
    // Requested profile (what user configured)
    int requested_width = 0;
    int requested_height = 0;
    int requested_fps = 0;
    int requested_bitrate = 0;
    bool requested_hevc = false;

    // Rendered info (what's actually being decoded)
    bool is_hardware_decoder = false;
    bool is_hevc = false;  // true = HEVC (PS5), false = H.264 (PS4)

    // Renderer info
    const char* renderer_name = "Unknown";

    // Frame stats
    float fps = 0.0f;

    int video_width = 0;
    int video_height = 0;

    float packet_loss_percent = 0.0f;
    uint64_t packets_received = 0;
    uint64_t packets_lost = 0;

    float measured_bitrate_mbps = 0.0f;

    size_t network_frames_lost = 0;
    size_t frames_recovered = 0;

    uint64_t stream_duration_seconds = 0;

    /*
     * Analog triggers, shown live because there is no other way to know.
     *
     * The value Akira sends is indistinguishable from a digital press once it
     * has left the console, and "it felt right" is not evidence. A percentage
     * that moves with your finger is - and if it only ever reads 0 or 100, the
     * pressure is not arriving however convincing the game feels.
     */
    bool analog_triggers_active = false;
    uint8_t analog_l2 = 0;   /* 0..255, as sent */
    uint8_t analog_r2 = 0;
};

#endif // AKIRA_IO_STREAM_STATS_HPP
