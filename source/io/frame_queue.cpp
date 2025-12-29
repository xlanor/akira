#include "core/io/frame_queue.hpp"
#include <borealis.hpp>

FrameQueue::FrameQueue()
{
}

FrameQueue::~FrameQueue()
{
    cleanup();
}

void FrameQueue::push(AVFrame* frame)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    AVFrame* ref_frame = av_frame_alloc();
    if (!ref_frame || av_frame_ref(ref_frame, frame) < 0)
    {
        if (ref_frame) av_frame_free(&ref_frame);
        brls::Logger::error("FrameQueue: Failed to ref frame");
        return;
    }

    m_queue.push(ref_frame);

    // Drop oldest if over limit
    if (m_queue.size() > m_limit)
    {
        AVFrame* old = m_queue.front();
        m_queue.pop();
        av_frame_free(&old);
        m_frames_dropped++;
    }

    // Update FPS calculation
    auto now = std::chrono::steady_clock::now();
    if (!m_fps_initialized)
    {
        m_fps_start_time = now;
        m_frames_this_second = 0;
        m_fps_initialized = true;
    }

    m_frames_this_second++;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_fps_start_time).count();
    if (elapsed >= 1000)
    {
        m_current_fps = (m_frames_this_second * 1000.0f) / elapsed;
        m_frames_this_second = 0;
        m_fps_start_time = now;
    }
}

AVFrame* FrameQueue::pop()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_queue.empty())
    {
        // Free previous buffer frame before replacing
        if (m_buffer_frame)
        {
            av_frame_free(&m_buffer_frame);
        }
        m_buffer_frame = m_queue.front();
        m_queue.pop();
    }
    else
    {
        m_fake_frame_used++;
    }

    return m_buffer_frame;
}

void FrameQueue::get(const std::function<void(AVFrame*)>& fn)
{
    AVFrame* frame = pop();
    if (frame)
    {
        fn(frame);
    }
}

bool FrameQueue::hasFrames() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_queue.empty() || m_buffer_frame != nullptr;
}

size_t FrameQueue::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size();
}

void FrameQueue::cleanup()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Free buffer frame
    if (m_buffer_frame)
    {
        av_frame_free(&m_buffer_frame);
        m_buffer_frame = nullptr;
    }

    // Free all queued frames (we own them now via av_frame_clone)
    while (!m_queue.empty())
    {
        AVFrame* frame = m_queue.front();
        m_queue.pop();
        av_frame_free(&frame);
    }

    // Clear stats
    m_frames_dropped = 0;
    m_fake_frame_used = 0;

    // Reset FPS tracking
    m_current_fps = 0.0f;
    m_frames_this_second = 0;
    m_fps_initialized = false;
}
