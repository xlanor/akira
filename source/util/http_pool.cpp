#include "util/http_pool.hpp"

#include <borealis.hpp>

HttpPool& HttpPool::instance()
{
    static HttpPool* pool = new HttpPool();
    return *pool;
}

HttpPool::~HttpPool()
{
    stop();
}

void HttpPool::submit(Task task)
{
    if (!task)
        return;

    std::lock_guard<std::mutex> lock(mutex);
    if (stopping)
        return;

    ensureStarted();

    tasks.push_back(std::move(task));
    cond.notify_one();
}

void HttpPool::ensureStarted()
{
    if (!threads.empty())
        return;

    brls::Logger::info("HttpPool starting {} worker(s)", THREAD_COUNT);

    threads.reserve(THREAD_COUNT);
    for (int i = 0; i < THREAD_COUNT; i++)
        threads.emplace_back(&HttpPool::run, this, i);
}

void HttpPool::stop()
{
    std::vector<std::thread> joining;

    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping)
            return;

        stopping = true;
        tasks.clear();
        cond.notify_all();
        joining.swap(threads);
    }

    for (std::thread& thread : joining)
    {
        if (thread.joinable())
            thread.join();
    }
}

void HttpPool::run(int index)
{
    // One session per thread, alive for as long as the thread is. Every request this
    // worker makes reuses its connection, so the SSL context is created once per host.
    HttpSession session;

    while (true)
    {
        Task task;

        {
            std::unique_lock<std::mutex> lock(mutex);
            cond.wait(lock, [this]() { return stopping || !tasks.empty(); });

            if (stopping)
                break;

            task = std::move(tasks.front());
            tasks.pop_front();
        }

        task(session);
    }

    brls::Logger::info("HttpPool worker {} exiting", index);
}
