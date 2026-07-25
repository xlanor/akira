#include "util/serial_worker.hpp"

#include <borealis.hpp>

SerialWorker::SerialWorker(std::string name)
    : name(std::move(name))
{
}

SerialWorker::~SerialWorker()
{
    stop();
}

void SerialWorker::post(std::function<void()> job)
{
    if (!job)
        return;

    std::lock_guard<std::mutex> lock(mutex);
    if (stopping)
        return;

    if (!thread.joinable())
    {
        brls::Logger::info("SerialWorker '{}' starting", name);
        thread = std::thread(&SerialWorker::run, this);
    }

    jobs.push_back(std::move(job));
    cond.notify_one();
}

void SerialWorker::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping)
            return;
        stopping = true;
        cond.notify_all();
    }

    if (thread.joinable())
        thread.join();
}

void SerialWorker::run()
{
    while (true)
    {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex);
            cond.wait(lock, [this]() { return stopping || !jobs.empty(); });

            if (stopping)
            {
                jobs.clear();
                break;
            }

            job = std::move(jobs.front());
            jobs.pop_front();
        }

        job();
    }

    brls::Logger::info("SerialWorker '{}' exiting", name);
}
