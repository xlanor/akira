#pragma once

#include <stdint.h>

#define AKIRA_IPC_API_VERSION 3
#define AKIRA_IPC_SERVICE_NAME "akira:s"
#define AKIRA_ERROR_NO_STREAM 0x1A901

enum AkiraIpcCmd
{
    AkiraIpcCmd_GetApiVersion = 0,
    AkiraIpcCmd_GetStreamStats = 1,
    AkiraIpcCmd_IsStreamActive = 2,
};

typedef struct
{
    int32_t requested_width;
    int32_t requested_height;
    int32_t requested_fps;
    int32_t requested_bitrate;
    uint8_t requested_hevc;

    int32_t video_width;
    int32_t video_height;
    uint8_t is_hardware_decoder;
    uint8_t is_hevc;

    float fps;

    float packet_loss_percent;
    uint64_t packets_received;
    uint64_t packets_lost;
    float measured_bitrate_mbps;

    uint32_t network_frames_lost;
    uint32_t frames_recovered;

    uint64_t stream_duration_seconds;

    uint8_t ghash_mode;
    uint8_t vpn_connected;
    char vpn_ip[16];
} AkiraStreamStats;
