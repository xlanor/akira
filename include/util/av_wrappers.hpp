#ifndef AKIRA_AV_WRAPPERS_HPP
#define AKIRA_AV_WRAPPERS_HPP

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

struct AVPacketGuard {
    AVPacket* pkt;
    AVPacketGuard() : pkt(av_packet_alloc()) {}
    ~AVPacketGuard() { if (pkt) av_packet_free(&pkt); }
    AVPacketGuard(const AVPacketGuard&) = delete;
    AVPacketGuard& operator=(const AVPacketGuard&) = delete;
    operator AVPacket*() { return pkt; }
    AVPacket* operator->() { return pkt; }
};

struct AVFrameGuard {
    AVFrame* frame;
    AVFrameGuard() : frame(av_frame_alloc()) {}
    ~AVFrameGuard() { if (frame) av_frame_free(&frame); }
    AVFrameGuard(const AVFrameGuard&) = delete;
    AVFrameGuard& operator=(const AVFrameGuard&) = delete;
    operator AVFrame*() { return frame; }
    AVFrame* operator->() { return frame; }
    AVFrame* release() { auto* f = frame; frame = nullptr; return f; }
};

#endif // AKIRA_AV_WRAPPERS_HPP
