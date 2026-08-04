#ifndef AKIRA_PSN_TOKEN_REFRESHER_HPP
#define AKIRA_PSN_TOKEN_REFRESHER_HPP

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>

#include <chiaki/thread.h>

class HttpSession;

namespace psn {

class TokenRefresher {
public:
    static TokenRefresher& instance();

    void start();
    void stop();
    void refreshNow(std::function<void()> onDone = nullptr);

private:
    TokenRefresher() = default;
    ~TokenRefresher();

    TokenRefresher(const TokenRefresher&) = delete;
    TokenRefresher& operator=(const TokenRefresher&) = delete;

    static void* threadFunc(void* user);
    void run();
    void tick(HttpSession& session, bool force);

    ChiakiThread thread{};
    bool threadStarted = false;
    std::atomic<bool> running{false};
    std::atomic<bool> forceNow{false};
    std::mutex mutex;
    std::condition_variable cond;
    std::function<void()> pendingDone;

    static constexpr int64_t INTERVAL_SECONDS = 300;
    static constexpr int64_t WINDOW_SECONDS = 600;
};

}  // namespace psn

#endif  // AKIRA_PSN_TOKEN_REFRESHER_HPP
