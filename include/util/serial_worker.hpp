#ifndef AKIRA_SERIAL_WORKER_HPP
#define AKIRA_SERIAL_WORKER_HPP

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class SerialWorker {
public:
    explicit SerialWorker(std::string name);
    ~SerialWorker();

    SerialWorker(const SerialWorker&) = delete;
    SerialWorker& operator=(const SerialWorker&) = delete;

    void post(std::function<void()> job);
    void stop();

private:
    void run();

    std::string name;
    std::thread thread;
    std::mutex mutex;
    std::condition_variable cond;
    std::deque<std::function<void()>> jobs;
    bool stopping = false;
};

#endif // AKIRA_SERIAL_WORKER_HPP
