#include "core/rate_limiter.hpp"

#include <borealis.hpp>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <format>

#include <json-c/json.h>

PersistedRateLimiter::PersistedRateLimiter(std::string path, int budget, int windowSeconds)
    : path(std::move(path))
    , budget(std::max(1, budget))
    , windowSeconds(std::max(1, windowSeconds))
{
}

void PersistedRateLimiter::reconfigure(int newBudget, int newWindowSeconds)
{
    std::lock_guard<std::mutex> lock(mutex);
    budget = std::max(1, newBudget);
    windowSeconds = std::max(1, newWindowSeconds);
}

void PersistedRateLimiter::retarget(std::string newPath)
{
    std::lock_guard<std::mutex> lock(mutex);
    path = std::move(newPath);
    loaded = false;
    stamps.clear();
    breakerUntil = 0;
    lastSeen = 0;
    throttleCount = 0;
    lastThrottleAt = 0;
}

void PersistedRateLimiter::pruneLocked(int64_t now)
{
    int64_t cutoff = now - windowSeconds;
    while (!stamps.empty() && stamps.front() <= cutoff)
        stamps.pop_front();
    while (!stamps.empty() && stamps.back() > now)
        stamps.pop_back();
}

void PersistedRateLimiter::loadLocked()
{
    if (loaded)
        return;

    loaded = true;

    int64_t now = static_cast<int64_t>(std::time(nullptr));
    lastSeen = now;

    FILE* file = fopen(path.c_str(), "rb");
    if (!file)
    {
        brls::Logger::info("Rate limiter: no state at {}, starting a fresh window", path);
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
        stamps.assign(static_cast<size_t>(budgetLocked()), now);
        brls::Logger::warning("Rate limiter: state at {} is unreadable, treating this window as spent", path);
        return;
    }

    auto readInt64 = [parsed](const char* key) -> int64_t {
        json_object* field = nullptr;
        if (!json_object_object_get_ex(parsed, key, &field) || !field)
            return 0;
        return json_object_get_int64(field);
    };

    breakerUntil = readInt64("breaker_until");
    int64_t storedLastSeen = readInt64("last_seen");
    throttleCount = static_cast<int>(readInt64("throttle_count"));
    lastThrottleAt = readInt64("last_throttle_at");

    json_object* stampsArray = nullptr;
    if (json_object_object_get_ex(parsed, "stamps", &stampsArray) && stampsArray &&
        json_object_is_type(stampsArray, json_type_array))
    {
        size_t length = json_object_array_length(stampsArray);
        for (size_t i = 0; i < length; i++)
        {
            json_object* entry = json_object_array_get_idx(stampsArray, i);
            if (entry)
                stamps.push_back(json_object_get_int64(entry));
        }
    }

    json_object_put(parsed);

    std::sort(stamps.begin(), stamps.end());

    if (storedLastSeen > now)
    {
        stamps.assign(static_cast<size_t>(budgetLocked()), now);
        breakerUntil = std::min(breakerUntil, now + BREAKER_MAX_SECONDS);
        brls::Logger::warning("Rate limiter: clock moved backwards, treating this window as spent");
        return;
    }

    pruneLocked(now);

    if (breakerUntil > now + BREAKER_MAX_SECONDS)
    {
        brls::Logger::warning("Rate limiter: breaker deadline is implausibly far out, clamping");
        breakerUntil = now + BREAKER_MAX_SECONDS;
    }
}

int PersistedRateLimiter::budgetLocked() const
{
    if (lastThrottleAt > 0)
    {
        int64_t now = static_cast<int64_t>(std::time(nullptr));
        if (now - lastThrottleAt < TIGHTEN_WINDOW_SECONDS)
            return std::max(1, budget / 2);
    }

    return budget;
}

void PersistedRateLimiter::persistLocked()
{
    lastSeen = static_cast<int64_t>(std::time(nullptr));

    std::string joined;
    for (size_t i = 0; i < stamps.size(); i++)
    {
        if (i > 0)
            joined += ",";
        joined += std::to_string(stamps[i]);
    }

    std::string body = std::format(
        "{{\"stamps\":[{}],\"breaker_until\":{},\"last_seen\":{},"
        "\"throttle_count\":{},\"last_throttle_at\":{}}}\n",
        joined, breakerUntil, lastSeen, throttleCount, lastThrottleAt);

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
        stamps.assign(static_cast<size_t>(budgetLocked()), now);
        brls::Logger::warning("Rate limiter: clock moved backwards mid-session, spending this window");
    }

    lastSeen = now;
    pruneLocked(now);

    if (breakerUntil > now)
    {
        outReason = std::format("PSN throttled us; requests resume in {}s", breakerUntil - now);
        return false;
    }

    int limit = budgetLocked();
    if (static_cast<int>(stamps.size()) >= limit)
    {
        int64_t resetsAt = stamps.empty() ? now : stamps.front() + windowSeconds;
        outReason = std::format("Request budget used up ({}/{}), resets in {}s",
            stamps.size(), limit, std::max<int64_t>(0, resetsAt - now));
        return false;
    }

    stamps.push_back(now);
    while (stamps.size() > MAX_STAMPS)
        stamps.pop_front();

    persistLocked();
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

    brls::Logger::error("Rate limiter: throttled by the server ({} lifetime), breaker open for {}s",
        throttleCount, clamped);

    persistLocked();
}

PersistedRateLimiter::Status PersistedRateLimiter::status() const
{
    std::lock_guard<std::mutex> lock(mutex);
    auto* self = const_cast<PersistedRateLimiter*>(this);
    self->loadLocked();

    int64_t now = static_cast<int64_t>(std::time(nullptr));
    self->pruneLocked(now);

    Status result;
    result.limit = budgetLocked();
    result.used = std::min(static_cast<int>(stamps.size()), result.limit);
    result.bucketResetsAt = (result.used >= result.limit && !stamps.empty())
        ? stamps.front() + windowSeconds
        : now;
    result.breakerUntil = breakerUntil;
    result.throttleCount = throttleCount;
    return result;
}
