#ifndef AKIRA_RATE_LIMITER_HPP
#define AKIRA_RATE_LIMITER_HPP

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

class PersistedRateLimiter {
public:
    struct Status {
        int used = 0;
        int limit = 0;
        int64_t bucketResetsAt = 0;
        int64_t breakerUntil = 0;
        int throttleCount = 0;

        bool breakerOpen(int64_t now) const { return breakerUntil > now; }
        int remaining() const { return used >= limit ? 0 : limit - used; }
    };

    static constexpr int DEFAULT_WINDOW_SECONDS = 900;

    PersistedRateLimiter(std::string path, int budget, int windowSeconds = DEFAULT_WINDOW_SECONDS);

    void reconfigure(int budget, int windowSeconds);

    void retarget(std::string newPath);

    bool tryAcquire(std::string& outReason);

    void recordThrottle(int cooldownSeconds);

    Status status() const;

private:
    static constexpr int BREAKER_MAX_SECONDS = 3600;
    static constexpr int64_t TIGHTEN_WINDOW_SECONDS = 24 * 60 * 60;
    static constexpr size_t MAX_STAMPS = 8192;

    void loadLocked();
    void persistLocked();
    void pruneLocked(int64_t now);
    int budgetLocked() const;

    std::string path;
    int budget;
    int windowSeconds;

    mutable std::mutex mutex;
    bool loaded = false;

    std::deque<int64_t> stamps;
    int64_t breakerUntil = 0;
    int64_t lastSeen = 0;
    int throttleCount = 0;
    int64_t lastThrottleAt = 0;
};

#endif // AKIRA_RATE_LIMITER_HPP
