#include "core/rate_limiter.hpp"

#include <borealis.hpp>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <format>

#include <json-c/json.h>

PersistedRateLimiter::PersistedRateLimiter(std::string path, int budget)
    : path(std::move(path))
    , budget(budget)
{
}

void PersistedRateLimiter::loadLocked()
{
    if (loaded)
        return;

    loaded = true;

    int64_t now = static_cast<int64_t>(std::time(nullptr));
    bucket = now / BUCKET_SECONDS;
    lastSeen = now;

    FILE* file = fopen(path.c_str(), "rb");
    if (!file)
    {
        // Nothing spent yet. A missing file on a fresh install is not evidence of usage,
        // so starting at zero is correct rather than optimistic.
        brls::Logger::info("Rate limiter: no state at {}, starting a fresh bucket", path);
        return;
    }

    std::string body;
    char buffer[512];
    size_t read = 0;
    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0)
        body.append(buffer, read);
    fclose(file);

    json_object* parsed = json_tokener_parse(body.c_str());
    if (!parsed)
    {
        // A file we cannot read might describe a spent bucket, so assume it did.
        count = budget;
        brls::Logger::warning("Rate limiter: state at {} is unreadable, treating this bucket as spent", path);
        return;
    }

    auto readInt64 = [parsed](const char* key) -> int64_t {
        json_object* field = nullptr;
        if (!json_object_object_get_ex(parsed, key, &field) || !field)
            return 0;
        return json_object_get_int64(field);
    };

    int64_t storedBucket = readInt64("bucket");
    int storedCount = static_cast<int>(readInt64("count"));
    breakerUntil = readInt64("breaker_until");
    int64_t storedLastSeen = readInt64("last_seen");
    throttleCount = static_cast<int>(readInt64("throttle_count"));
    lastThrottleAt = readInt64("last_throttle_at");

    json_object_put(parsed);

    if (storedLastSeen > now)
    {
        // The clock moved backwards. The stored counter may describe a bucket we are now
        // pretending to be inside again, so spend it rather than trust it.
        bucket = now / BUCKET_SECONDS;
        count = budget;
        brls::Logger::warning("Rate limiter: clock moved backwards, treating this bucket as spent");
    }
    else if (storedBucket == bucket)
    {
        count = std::clamp(storedCount, 0, budget);
    }
    else
    {
        count = 0;
    }

    if (breakerUntil > now + BREAKER_MAX_SECONDS)
    {
        brls::Logger::warning("Rate limiter: breaker deadline is implausibly far out, clamping");
        breakerUntil = now + BREAKER_MAX_SECONDS;
    }
}

void PersistedRateLimiter::rollBucketLocked(int64_t now)
{
    int64_t currentBucket = now / BUCKET_SECONDS;
    if (currentBucket == bucket)
        return;

    bucket = currentBucket;
    count = 0;
    creditsHeld = 0;
}

int PersistedRateLimiter::budgetLocked() const
{
    // Our ceiling is a guess against an unpublished quota. If the server has throttled us
    // recently the guess was wrong, so halve it for a day. It only ever relaxes by that
    // day elapsing, never by probing upwards.
    if (lastThrottleAt > 0)
    {
        int64_t now = static_cast<int64_t>(std::time(nullptr));
        if (now - lastThrottleAt < TIGHTEN_WINDOW_SECONDS)
            return std::max(1, budget / 2);
    }

    return budget;
}

void PersistedRateLimiter::persistLocked(int countToStore)
{
    lastSeen = static_cast<int64_t>(std::time(nullptr));

    std::string body = std::format(
        "{{\"bucket\":{},\"count\":{},\"breaker_until\":{},\"last_seen\":{},"
        "\"throttle_count\":{},\"last_throttle_at\":{}}}\n",
        bucket, countToStore, breakerUntil, lastSeen, throttleCount, lastThrottleAt);

    // Written in place rather than temp+rename: a torn write leaves an unparseable file,
    // which the loader already treats as a spent bucket. That fails in the safe direction.
    FILE* file = fopen(path.c_str(), "wb");
    if (!file)
    {
        brls::Logger::warning("Rate limiter: could not write {}", path);
        return;
    }

    fwrite(body.data(), 1, body.size(), file);
    fclose(file);
}

bool PersistedRateLimiter::tryAcquire(std::string& outReason)
{
    std::lock_guard<std::mutex> lock(mutex);
    loadLocked();

    int64_t now = static_cast<int64_t>(std::time(nullptr));

    if (now < lastSeen)
    {
        count = budget;
        creditsHeld = 0;
        brls::Logger::warning("Rate limiter: clock moved backwards mid-session, spending this bucket");
    }

    rollBucketLocked(now);
    lastSeen = now;

    if (breakerUntil > now)
    {
        outReason = std::format("PSN throttled us; requests resume in {}s", breakerUntil - now);
        return false;
    }

    int limit = budgetLocked();
    if (count >= limit)
    {
        int64_t resetsAt = (bucket + 1) * BUCKET_SECONDS;
        outReason = std::format("Request budget used up ({}/{}), resets in {}s",
            count, limit, std::max<int64_t>(0, resetsAt - now));
        return false;
    }

    if (creditsHeld == 0)
    {
        int block = std::min(RESERVATION, limit - count);
        persistLocked(count + block);
        creditsHeld = block;
    }

    creditsHeld--;
    count++;
    return true;
}

void PersistedRateLimiter::recordThrottle(int cooldownSeconds)
{
    std::lock_guard<std::mutex> lock(mutex);
    loadLocked();

    int64_t now = static_cast<int64_t>(std::time(nullptr));
    int clamped = std::clamp(cooldownSeconds, 1, BREAKER_MAX_SECONDS);

    breakerUntil = std::max(breakerUntil, now + clamped);
    throttleCount++;
    lastThrottleAt = now;
    creditsHeld = 0;

    brls::Logger::error("Rate limiter: throttled by the server ({} lifetime), breaker open for {}s",
        throttleCount, clamped);

    persistLocked(count);
}

PersistedRateLimiter::Status PersistedRateLimiter::status() const
{
    std::lock_guard<std::mutex> lock(mutex);
    const_cast<PersistedRateLimiter*>(this)->loadLocked();

    int64_t now = static_cast<int64_t>(std::time(nullptr));
    int64_t currentBucket = now / BUCKET_SECONDS;

    Status result;
    result.limit = budgetLocked();
    result.used = currentBucket == bucket ? std::min(count, result.limit) : 0;
    result.bucketResetsAt = (currentBucket + 1) * BUCKET_SECONDS;
    result.breakerUntil = breakerUntil;
    result.throttleCount = throttleCount;
    return result;
}
