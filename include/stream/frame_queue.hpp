#ifndef AKIRA_IO_FRAME_QUEUE_HPP
#define AKIRA_IO_FRAME_QUEUE_HPP

#include <mutex>
#include <queue>
#include <functional>
#include <chrono>

extern "C"
{
#include <libavcodec/avcodec.h>
}

class FrameQueue
{
public:
    static constexpr size_t DEFAULT_LIMIT = 5;

    FrameQueue();
    ~FrameQueue();

    FrameQueue(const FrameQueue&) = delete;
    FrameQueue& operator=(const FrameQueue&) = delete;

    void setLimit(size_t limit) { m_limit = limit; }

    void push(AVFrame* frame);

    AVFrame* pop();

    void get(const std::function<void(AVFrame*)>& fn);

    bool hasFrames() const;

    size_t size() const;

    size_t getFramesDropped() const { return m_frames_dropped; }
    size_t getFakeFrameUsed() const { return m_fake_frame_used; }
    float getCurrentFPS() const { return m_current_fps; }

    void cleanup();

private:
    std::queue<AVFrame*> m_queue;
    AVFrame* m_buffer_frame = nullptr;
    size_t m_limit = DEFAULT_LIMIT;
    size_t m_frames_dropped = 0;
    size_t m_fake_frame_used = 0;
    mutable std::mutex m_mutex;

    std::chrono::steady_clock::time_point m_fps_start_time;
    uint64_t m_frames_this_second = 0;
    float m_current_fps = 0.0f;
    bool m_fps_initialized = false;
};

#endif // AKIRA_IO_FRAME_QUEUE_HPP
