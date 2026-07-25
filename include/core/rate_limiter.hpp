#ifndef AKIRA_RATE_LIMITER_HPP
#define AKIRA_RATE_LIMITER_HPP

#include <cstdint>
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

    PersistedRateLimiter(std::string path, int budget);

    bool tryAcquire(std::string& outReason);

    void recordThrottle(int cooldownSeconds);

    Status status() const;

private:
    static constexpr int BUCKET_SECONDS = 900;
    static constexpr int RESERVATION = 10;
    static constexpr int BREAKER_MAX_SECONDS = 3600;
    static constexpr int64_t TIGHTEN_WINDOW_SECONDS = 24 * 60 * 60;

    void loadLocked();
    void persistLocked(int countToStore);
    void rollBucketLocked(int64_t now);
    int budgetLocked() const;

    std::string path;
    int budget;

    mutable std::mutex mutex;
    bool loaded = false;

    int64_t bucket = 0;
    int count = 0;
    int creditsHeld = 0;
    int64_t breakerUntil = 0;
    int64_t lastSeen = 0;
    int throttleCount = 0;
    int64_t lastThrottleAt = 0;
};

#endif // AKIRA_RATE_LIMITER_HPP
