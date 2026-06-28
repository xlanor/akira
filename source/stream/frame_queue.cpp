#include "stream/frame_queue.hpp"
#include <borealis.hpp>
#include <algorithm>

namespace
{
size_t ringIndex(size_t head, size_t offset, size_t capacity)
{
    return (head + offset) % capacity;
}
}

FrameQueue::FrameQueue()
{
    m_ring.resize(m_limit, nullptr);
}

FrameQueue::~FrameQueue()
{
    cleanup();
}

void FrameQueue::setLimit(size_t limit)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    limit = std::max<size_t>(1, limit);
    if (limit == m_limit && m_ring.size() == limit)
        return;

    std::vector<AVFrame*> new_ring(limit, nullptr);
    size_t keep = std::min(limit, m_count);
    size_t drop = m_count - keep;

    for (size_t i = 0; i < drop; ++i)
    {
        size_t index = ringIndex(m_head, i, m_ring.size());
        if (m_ring[index])
        {
            av_frame_free(&m_ring[index]);
            m_frames_dropped++;
        }
    }

    size_t start = m_count > keep ? m_count - keep : 0;
    for (size_t i = 0; i < keep; ++i)
    {
        size_t old_index = ringIndex(m_head, start + i, m_ring.size());
        new_ring[i] = m_ring[old_index];
        m_ring[old_index] = nullptr;
    }

    m_ring = std::move(new_ring);
    m_limit = limit;
    m_head = 0;
    m_count = keep;
}

void FrameQueue::push(AVFrame* frame)
{
    if (!frame)
        return;

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_ring.empty())
        m_ring.resize(m_limit, nullptr);

    if (m_count == m_limit)
    {
        AVFrame*& oldest = m_ring[m_head];
        if (oldest)
        {
            av_frame_free(&oldest);
            if (m_limit > 1)
                m_frames_dropped++;
        }
        m_head = ringIndex(m_head, 1, m_limit);
        m_count--;
    }

    size_t tail = ringIndex(m_head, m_count, m_limit);
    m_ring[tail] = frame;
    m_count++;

    recordDecodedFrameLocked(std::chrono::steady_clock::now());
}

void FrameQueue::recordDecodedFrame()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    recordDecodedFrameLocked(std::chrono::steady_clock::now());
}

void FrameQueue::recordDecodedFrameLocked(std::chrono::steady_clock::time_point now)
{
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

    if (m_count > 0)
    {
        if (m_buffer_frame)
            av_frame_free(&m_buffer_frame);

        m_buffer_frame = m_ring[m_head];
        m_ring[m_head] = nullptr;
        m_head = ringIndex(m_head, 1, m_limit);
        m_count--;
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
        fn(frame);
}

bool FrameQueue::hasFrames() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_count > 0 || m_buffer_frame != nullptr;
}

size_t FrameQueue::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_count;
}

void FrameQueue::cleanup()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_buffer_frame)
    {
        av_frame_free(&m_buffer_frame);
        m_buffer_frame = nullptr;
    }

    for (AVFrame*& frame : m_ring)
    {
        if (frame)
            av_frame_free(&frame);
    }

    m_head = 0;
    m_count = 0;
    m_frames_dropped = 0;
    m_fake_frame_used = 0;
    m_current_fps = 0.0f;
    m_frames_this_second = 0;
    m_fps_initialized = false;
}
