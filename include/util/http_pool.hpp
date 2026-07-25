#ifndef AKIRA_HTTP_POOL_HPP
#define AKIRA_HTTP_POOL_HPP

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "util/http.hpp"

// Worker threads that each own one long-lived HttpSession for their whole lifetime.
// Tasks borrow their thread's session, so the number of live SSL contexts is bounded by
// the thread count rather than by the number of requests.
//
// Sized deliberately small: chiaki-ng's holepunch opens its own curl handles outside this
// pool, so akira must leave the Switch ssl service headroom rather than take all of it.
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
