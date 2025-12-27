#ifndef AKIRA_IO_VIDEO_RENDERER_HPP
#define AKIRA_IO_VIDEO_RENDERER_HPP

#include <chiaki/log.h>
#include "core/io/stream_stats.hpp"

extern "C"
{
#include <libavcodec/avcodec.h>
}

// Abstract interface for video rendering
// Implementations: OpenGLRenderer, Deko3dRenderer
class IVideoRenderer
{
public:
    virtual ~IVideoRenderer() = default;

    virtual bool initialize(int frame_width, int frame_height,
                           int screen_width, int screen_height,
                           ChiakiLog* log) = 0;

    virtual bool isInitialized() const = 0;

    virtual void draw(AVFrame* frame) = 0;

    virtual void cleanup() = 0;

    virtual void waitIdle() {}

    virtual void setShowStatsOverlay(bool show) { m_show_stats = show; }
    virtual void setStreamStats(const StreamStats& stats) { m_stats = stats; }

protected:
    bool m_show_stats = false;
    StreamStats m_stats;
};

#endif // AKIRA_IO_VIDEO_RENDERER_HPP
