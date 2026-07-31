#ifndef AKIRA_HTTP_POOL_HPP
#define AKIRA_HTTP_POOL_HPP

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "util/http.hpp"

class HttpPool {
public:
    using Task = std::function<void(HttpSession&)>;

    static HttpPool& instance();

    void submit(Task task);
    void stop();

    size_t threadCount() const { return threads.size(); }

private:
    HttpPool() = default;
    ~HttpPool();

    HttpPool(const HttpPool&) = delete;
    HttpPool& operator=(const HttpPool&) = delete;

    void ensureStarted();
    void run(int index);

    static constexpr int THREAD_COUNT = 4;

    std::vector<std::thread> threads;
    std::deque<Task> tasks;
    mutable std::mutex mutex;
    std::condition_variable cond;
    bool stopping = false;
};

#endif // AKIRA_HTTP_POOL_HPP
