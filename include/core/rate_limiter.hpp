#ifndef AKIRA_RATE_LIMITER_HPP
#define AKIRA_RATE_LIMITER_HPP

#include <cstdint>
#include <mutex>
#include <string>

// A sustained request budget that survives relaunch.
//
// Homebrew gets closed and reopened constantly, so an in-memory window is trivially reset
// by restarting. State lives in a fixed-size JSON file: a quarter-hour bucket id, a count,
// and a circuit breaker deadline.
//
// Credits are reserved in blocks and spent from RAM, so a session costs one or two SD
// writes rather than one per request. A crash forfeits the unspent block, which over-counts
// rather than under-counts.
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

    // Charges one request. Returns false and fills outReason when the breaker is open or
    // the bucket is spent.
    bool tryAcquire(std::string& outReason);

    // Called when the server reports throttling. Opens the breaker and records the event
    // so the budget tightens for the next day.
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
