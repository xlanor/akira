#include "core/trophy_manager.hpp"
#include "core/settings_manager.hpp"
#include "psn/auth.hpp"
#include "psn/log.hpp"

#include <borealis.hpp>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <format>
#include <thread>

#include <sys/stat.h>
#include <switch.h>

#include <json-c/json.h>

#include "util/http.hpp"

static void forwardPsnLog(psn::LogLevel level, const std::string& message)
{
    switch (level)
    {
        case psn::LogLevel::Info: brls::Logger::info("{}", message); break;
        case psn::LogLevel::Warning: brls::Logger::warning("{}", message); break;
        case psn::LogLevel::Error: brls::Logger::error("{}", message); break;
    }
}

TrophyManager* TrophyManager::getInstance()
{
    static TrophyManager* instance = new TrophyManager();
    return instance;
}

TrophyManager::TrophyManager()
{
    settings = SettingsManager::getInstance();
    psn::setLogSink(forwardPsnLog);
}

void TrophyManager::ensureCacheDirs()
{
    mkdir(CACHE_DIR, 0755);
    mkdir(TROPHY_CACHE_DIR, 0755);
}

void TrophyManager::ensureIconCacheDir()
{
    if (iconCacheDirReady.load())
        return;

    ensureCacheDirs();
    mkdir(ICON_CACHE_DIR, 0755);

    iconCacheDirReady.store(true);
}

bool TrophyManager::cacheEntryFresh(int64_t savedAt, int ttlMinutes)
{
    if (savedAt <= 0)
        return false;

    int64_t now = static_cast<int64_t>(std::time(nullptr));

    // A timestamp in the future means the clock moved, not that the entry is invalid.
    // Treating it as fresh errs toward fewer requests, which is the safe direction here.
    if (savedAt > now)
        return true;

    return (now - savedAt) < static_cast<int64_t>(ttlMinutes) * 60;
}

static std::string readWholeFile(const std::string& path)
{
    FILE* file = fopen(path.c_str(), "rb");
    if (!file)
        return std::string();

    std::string body;
    char buffer[4096];
    size_t read = 0;
    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0)
        body.append(buffer, read);

    fclose(file);
    return body;
}

// FAT via newlib does not replace on rename the way POSIX does: it fails with EEXIST if
// the destination is already there. The destination has to be removed first, which gives
// up atomicity, but on this filesystem there was never an atomic replace to give up. The
// failure window leaves no file at all, and a missing cache entry just means a refetch.
static bool replaceFile(const std::string& temp, const std::string& path)
{
    remove(path.c_str());

    if (rename(temp.c_str(), path.c_str()) == 0)
        return true;

    brls::Logger::warning("Trophy: rename {} -> {} failed: {}", temp, path, strerror(errno));
    remove(temp.c_str());
    return false;
}

static bool writeWholeFile(const std::string& path, const std::string& body)
{
    std::string temp = path + ".tmp";

    FILE* file = fopen(temp.c_str(), "wb");
    if (!file)
    {
        brls::Logger::warning("Trophy: could not open {}: {}", temp, strerror(errno));
        return false;
    }

    size_t written = fwrite(body.data(), 1, body.size(), file);
    fclose(file);

    if (written != body.size())
    {
        brls::Logger::warning("Trophy: short write to {} ({}/{} bytes)", temp, written, body.size());
        remove(temp.c_str());
        return false;
    }

    return replaceFile(temp, path);
}

bool TrophyManager::loadSummaryFromDisk(psn::TrophySummary& outSummary, int64_t& outSavedAt) const
{
    psn::Json doc(readWholeFile(SUMMARY_CACHE_PATH));
    if (!doc)
        return false;

    outSavedAt = psn::jsonInt64(doc.get(), "savedAt");

    json_object* payload = nullptr;
    if (!psn::jsonField(doc.get(), "summary", &payload))
        return false;

    return psn::parseSummary(payload, outSummary);
}

void TrophyManager::saveSummaryToDisk(const psn::TrophySummary& summary) const
{
    json_object* root = json_object_new_object();
    json_object_object_add(root, "savedAt", json_object_new_int64(static_cast<int64_t>(std::time(nullptr))));
    json_object_object_add(root, "summary", psn::toJson(summary));

    const char* text = json_object_to_json_string(root);
    if (!writeWholeFile(SUMMARY_CACHE_PATH, text ? text : ""))
        brls::Logger::warning("Trophy: could not write {}", SUMMARY_CACHE_PATH);

    json_object_put(root);
}

bool TrophyManager::loadLibraryFromDisk(std::vector<psn::TrophyTitle>& outTitles, int64_t& outSavedAt) const
{
    psn::Json doc(readWholeFile(LIBRARY_CACHE_PATH));
    if (!doc)
        return false;

    outSavedAt = psn::jsonInt64(doc.get(), "savedAt");

    json_object* array = nullptr;
    if (!psn::jsonField(doc.get(), "titles", &array) || !json_object_is_type(array, json_type_array))
        return false;

    size_t count = json_object_array_length(array);
    outTitles.reserve(count);

    for (size_t i = 0; i < count; i++)
    {
        psn::TrophyTitle title;
        if (psn::parseTitle(json_object_array_get_idx(array, i), title))
            outTitles.push_back(std::move(title));
    }

    return true;
}

void TrophyManager::saveLibraryToDisk(const std::vector<psn::TrophyTitle>& titles) const
{
    json_object* array = json_object_new_array();

    for (const psn::TrophyTitle& title : titles)
        json_object_array_add(array, psn::toJson(title));

    json_object* root = json_object_new_object();
    json_object_object_add(root, "savedAt", json_object_new_int64(static_cast<int64_t>(std::time(nullptr))));
    json_object_object_add(root, "titles", array);

    const char* text = json_object_to_json_string(root);
    if (!writeWholeFile(LIBRARY_CACHE_PATH, text ? text : ""))
        brls::Logger::warning("Trophy: could not write {}", LIBRARY_CACHE_PATH);

    json_object_put(root);
}

PersistedRateLimiter::Status TrophyManager::budgetStatus() const
{
    return limiter.status();
}

void TrophyManager::loadForceStateLocked()
{
    if (forceStateLoaded)
        return;

    forceStateLoaded = true;

    psn::Json doc(readWholeFile(FORCE_STATE_PATH));
    if (!doc)
        return;

    json_object_object_foreach(doc.get(), key, value)
    {
        if (value && json_object_is_type(value, json_type_int))
            forcedAt[key] = json_object_get_int64(value);
    }
}

void TrophyManager::saveForceStateLocked() const
{
    json_object* root = json_object_new_object();

    for (const auto& entry : forcedAt)
        json_object_object_add(root, entry.first.c_str(), json_object_new_int64(entry.second));

    const char* text = json_object_to_json_string(root);
    if (!writeWholeFile(FORCE_STATE_PATH, text ? text : ""))
        brls::Logger::warning("Trophy: could not write {}", FORCE_STATE_PATH);

    json_object_put(root);
}

psn::ActionStatus TrophyManager::forceRefreshStatus(const std::string& scope)
{
    std::lock_guard<std::mutex> lock(mutex);
    loadForceStateLocked();

    auto entry = forcedAt.find(scope);
    if (entry == forcedAt.end() || entry->second <= 0)
        return {psn::ActionState::Ready, 0};

    int64_t now = static_cast<int64_t>(std::time(nullptr));

    if (entry->second > now)
    {
        brls::Logger::warning("Trophy: force refresh stamp for '{}' is in the future, restarting the cooldown", scope);
        entry->second = now;
        saveForceStateLocked();
    }

    int64_t readyAt = entry->second + static_cast<int64_t>(FORCE_REFRESH_COOLDOWN_MINUTES) * 60;
    if (readyAt <= now)
        return {psn::ActionState::Ready, 0};

    return {psn::ActionState::CoolingDown, static_cast<int>(readyAt - now)};
}

void TrophyManager::recordForcedRefresh(const std::string& scope)
{
    ensureCacheDirs();

    std::lock_guard<std::mutex> lock(mutex);
    loadForceStateLocked();

    forcedAt[scope] = static_cast<int64_t>(std::time(nullptr));
    saveForceStateLocked();

    brls::Logger::info("Trophy: force refresh of '{}' recorded, next available in {} min",
        scope, FORCE_REFRESH_COOLDOWN_MINUTES);
}

std::string TrophyManager::iconCachePath(const std::string& url) const
{
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : url)
    {
        hash ^= c;
        hash *= 1099511628211ULL;
    }

    return std::format("{}/{:016x}.img", ICON_CACHE_DIR, hash);
}

bool TrophyManager::readIconFromDisk(const std::string& path, std::vector<uint8_t>& outBytes) const
{
    FILE* file = fopen(path.c_str(), "rb");
    if (!file)
        return false;

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size <= 0 || static_cast<size_t>(size) > ICON_MAX_BYTES)
    {
        fclose(file);
        return false;
    }

    outBytes.resize(static_cast<size_t>(size));
    size_t read = fread(outBytes.data(), 1, outBytes.size(), file);
    fclose(file);

    if (read != outBytes.size())
    {
        outBytes.clear();
        return false;
    }

    return true;
}

void TrophyManager::writeIconToDisk(const std::string& path, const std::vector<uint8_t>& bytes) const
{
    std::string temp = path + ".tmp";

    FILE* file = fopen(temp.c_str(), "wb");
    if (!file)
        return;

    size_t written = fwrite(bytes.data(), 1, bytes.size(), file);
    fclose(file);

    if (written != bytes.size())
    {
        remove(temp.c_str());
        return;
    }

    replaceFile(temp, path);
}

void TrophyManager::storeIconInMemory(const std::string& url, const std::vector<uint8_t>& bytes)
{
    std::lock_guard<std::mutex> lock(iconMutex);

    if (iconCache.find(url) == iconCache.end())
    {
        iconCacheBytes += bytes.size();
        iconCache[url] = bytes;
        iconOrder.push_back(url);
    }

    while (iconCacheBytes > ICON_CACHE_MAX_BYTES && !iconOrder.empty())
    {
        const std::string oldest = iconOrder.front();
        iconOrder.pop_front();

        if (oldest == url)
            continue;

        auto entry = iconCache.find(oldest);
        if (entry != iconCache.end())
        {
            iconCacheBytes -= entry->second.size();
            iconCache.erase(entry);
        }
    }
}

bool TrophyManager::hasConnectivity() const
{
    Result rc = nifmInitialize(NifmServiceType_User);
    if (R_FAILED(rc))
        return true;

    NifmInternetConnectionType type = NifmInternetConnectionType_WiFi;
    NifmInternetConnectionStatus status = NifmInternetConnectionStatus_ConnectingUnknown1;
    u32 strength = 0;

    rc = nifmGetInternetConnectionStatus(&type, &strength, &status);
    nifmExit();

    if (R_FAILED(rc))
        return true;

    return status == NifmInternetConnectionStatus_Connected;
}

psn::Status TrophyManager::ensureToken(HttpSession& session, std::string& outToken, std::string& outMessage)
{
    psn::Auth& auth = psn::Auth::instance();

    if (auth.tokenValid())
    {
        outToken = auth.accessToken();
        if (outToken.empty())
        {
            outMessage = "No PSN access token stored";
            return psn::Status::NotLinked;
        }
        return psn::Status::Ok;
    }

    if (settings->getPsnRefreshToken().empty())
    {
        outMessage = "PSN account not linked";
        return psn::Status::NotLinked;
    }

    brls::Logger::info("Trophy: access token expired, refreshing");
    psn::AuthResult refresh = auth.refreshBlocking(session);

    if (refresh.success)
    {
        outToken = auth.accessToken();
        return psn::Status::Ok;
    }

    outMessage = refresh.message;

    if (refresh.error == psn::AuthError::Invalid)
    {
        auth.clearTokens(refresh.message);
        return psn::Status::SessionExpired;
    }

    brls::Logger::warning("Trophy: token refresh failed transiently ({}), keeping stored tokens", refresh.message);
    return psn::Status::Offline;
}

void TrophyManager::awaitBurstSlot()
{
    while (true)
    {
        std::chrono::steady_clock::time_point waitUntil;

        {
            std::lock_guard<std::mutex> lock(mutex);

            auto now = std::chrono::steady_clock::now();
            auto cutoff = now - std::chrono::milliseconds(BURST_WINDOW_MS);
            while (!burstWindow.empty() && burstWindow.front() <= cutoff)
                burstWindow.pop_front();

            if (static_cast<int>(burstWindow.size()) < BURST_LIMIT)
            {
                burstWindow.push_back(now);
                return;
            }

            waitUntil = burstWindow.front() + std::chrono::milliseconds(BURST_WINDOW_MS);
        }

        std::this_thread::sleep_until(waitUntil);
    }
}

bool TrophyManager::breakerOpen(int& outSecondsRemaining) const
{
    std::lock_guard<std::mutex> lock(mutex);

    auto now = std::chrono::steady_clock::now();
    if (breakerUntil <= now)
        return false;

    auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(breakerUntil - now).count();
    outSecondsRemaining = static_cast<int>((remainingMs + 999) / 1000);
    return true;
}

void TrophyManager::tripBreaker(int seconds)
{
    std::lock_guard<std::mutex> lock(mutex);

    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    if (until > breakerUntil)
        breakerUntil = until;
}

psn::Error TrophyManager::governedGet(HttpSession& session, const std::string& url, std::string& outBody)
{
    int breakerSeconds = 0;
    if (breakerOpen(breakerSeconds))
    {
        psn::Error blocked{psn::Status::RateLimited,
            std::format("PSN rate limit cooldown active, {}s remaining", breakerSeconds)};
        brls::Logger::warning("Trophy: {} blocked, {}", url, blocked.message);
        return blocked;
    }

    if (!psn::Auth::instance().linked())
        return {psn::Status::NotLinked, "PSN account not linked"};

    if (!hasConnectivity())
    {
        brls::Logger::info("Trophy: {} skipped, no network connection", url);
        return {psn::Status::Offline, "No network connection"};
    }

    std::string token;
    std::string tokenMessage;
    psn::Status tokenStatus = ensureToken(session, token, tokenMessage);
    if (tokenStatus != psn::Status::Ok)
        return {tokenStatus, tokenMessage};

    psn::Error result;
    bool refreshedOn401 = false;
    int backoffSeconds = 2;

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++)
    {
        std::string budgetReason;
        if (!limiter.tryAcquire(budgetReason))
        {
            brls::Logger::warning("Trophy: {} refused, {}", url, budgetReason);
            return {psn::Status::RateLimited, budgetReason};
        }

        awaitBurstSlot();

        HttpResponse response = session.get(url, token, REQUEST_TIMEOUT_S);

        if (!response.transportFailed() && response.status == 200)
        {
            outBody = std::move(response.body);
            return {};
        }

        if (!response.transportFailed() && response.status == 401)
        {
            if (refreshedOn401)
            {
                brls::Logger::error("Trophy: {} still 401 after refresh, giving up", url);
                return {psn::Status::SessionExpired, "PSN rejected the access token after a refresh"};
            }

            refreshedOn401 = true;
            brls::Logger::info("Trophy: {} returned 401, refreshing token once", url);

            psn::AuthResult refresh = psn::Auth::instance().refreshBlocking(session);
            if (!refresh.success)
            {
                if (refresh.error == psn::AuthError::Invalid)
                {
                    psn::Auth::instance().clearTokens(refresh.message);
                    return {psn::Status::SessionExpired, refresh.message};
                }

                return {psn::Status::Offline, refresh.message};
            }

            token = psn::Auth::instance().accessToken();
            continue;
        }

        if (!response.transportFailed() && response.status == 429)
        {
            int cooldown = BREAKER_MINUTES * 60;

            std::string retryAfter = response.header("Retry-After");
            if (!retryAfter.empty())
            {
                try
                {
                    cooldown = std::max(cooldown, std::stoi(retryAfter));
                }
                catch (const std::exception&)
                {
                }
            }

            cooldown = std::min(cooldown, 60 * 60);
            tripBreaker(cooldown);
            limiter.recordThrottle(cooldown);

            brls::Logger::error("Trophy: {} returned 429 (Retry-After '{}'), tripping breaker for {}s",
                url, retryAfter, cooldown);
            return {psn::Status::RateLimited,
                std::format("PSN is rate-limiting, backing off for {}s", cooldown)};
        }

        bool retryable = response.transportFailed() || response.status >= 500;

        if (response.transportFailed())
        {
            result = {psn::Status::Offline, response.error};
            brls::Logger::warning("Trophy: {} attempt {}/{} transport failure: {}",
                url, attempt, MAX_ATTEMPTS, response.error);
        }
        else
        {
            result = {psn::Status::ServerError, std::format("HTTP {}", response.status)};
            brls::Logger::warning("Trophy: {} attempt {}/{} returned HTTP {}",
                url, attempt, MAX_ATTEMPTS, response.status);
        }

        if (!retryable)
            return result;

        if (attempt < MAX_ATTEMPTS)
        {
            brls::Logger::info("Trophy: retrying {} in {}s", url, backoffSeconds);
            std::this_thread::sleep_for(std::chrono::seconds(backoffSeconds));
            backoffSeconds *= 2;
        }
    }

    return result;
}

psn::Client TrophyManager::clientFor(HttpSession& session)
{
    return psn::Client([this, &session](const std::string& url, std::string& outBody) {
        return governedGet(session, url, outBody);
    });
}

void TrophyManager::logLibrary(const std::vector<psn::TrophyTitle>& titles) const
{
    int logged = 0;

    for (const psn::TrophyTitle& title : titles)
    {
        if (logged >= LIBRARY_LOG_CAP)
        {
            brls::Logger::info("Trophy library: {} further title(s) not logged",
                titles.size() - static_cast<size_t>(logged));
            break;
        }

        brls::Logger::info("  [{}] {} {} '{}' svc={} {}% {}/{} (P{}/{} G{}/{} S{}/{} B{}/{}) groups={}({}) hidden={} v{} updated={}",
            logged, title.npCommunicationId, title.trophyTitlePlatform, title.trophyTitleName,
            title.npServiceName, title.progress,
            title.earnedTrophies.total(), title.definedTrophies.total(),
            title.earnedTrophies.platinum, title.definedTrophies.platinum,
            title.earnedTrophies.gold, title.definedTrophies.gold,
            title.earnedTrophies.silver, title.definedTrophies.silver,
            title.earnedTrophies.bronze, title.definedTrophies.bronze,
            title.hasTrophyGroups ? "yes" : "no", title.trophyGroupCount,
            title.hiddenFlag ? "yes" : "no", title.trophySetVersion,
            title.lastUpdatedDateTime);

        logged++;
    }
}

void TrophyManager::fetchSummary(bool forceRefresh, Callback<psn::TrophySummary> onSuccess, ErrorCallback onError)
{
    HttpPool::instance().submit([this, forceRefresh, onSuccess, onError](HttpSession& session) {
        if (!forceRefresh)
        {
            psn::TrophySummary cached;
            bool haveCached = false;

            {
                std::lock_guard<std::mutex> lock(mutex);
                if (hasCachedSummary)
                {
                    cached = cachedSummary;
                    haveCached = cacheEntryFresh(summarySavedAt, SUMMARY_TTL_MINUTES);
                }
            }

            if (!haveCached)
            {
                int64_t savedAt = 0;
                psn::TrophySummary fromDisk;
                if (loadSummaryFromDisk(fromDisk, savedAt) && cacheEntryFresh(savedAt, SUMMARY_TTL_MINUTES))
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    cachedSummary = fromDisk;
                    hasCachedSummary = true;
                    summarySavedAt = savedAt;
                    cached = fromDisk;
                    haveCached = true;
                    brls::Logger::info("Trophy: summary served from disk cache");
                }
            }

            if (haveCached)
            {
                brls::sync([onSuccess, cached]() { if (onSuccess) onSuccess(cached); });
                return;
            }
        }

        psn::TrophySummary summary;
        psn::Error error = clientFor(session).fetchSummary(summary);

        if (error.ok())
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                cachedSummary = summary;
                hasCachedSummary = true;
                summarySavedAt = static_cast<int64_t>(std::time(nullptr));
            }

            ensureCacheDirs();
            saveSummaryToDisk(summary);

            brls::Logger::info("Trophy summary: level {} tier {} progress {}% points {} ({}/{}) earned {} (P{} G{} S{} B{})",
                summary.trophyLevel, summary.tier, summary.progress, summary.trophyPoint,
                summary.trophyLevelBasePoint, summary.trophyLevelNextPoint,
                summary.earnedTrophies.total(), summary.earnedTrophies.platinum,
                summary.earnedTrophies.gold, summary.earnedTrophies.silver,
                summary.earnedTrophies.bronze);

            brls::sync([onSuccess, summary]() { if (onSuccess) onSuccess(summary); });
            return;
        }

        brls::Logger::error("Trophy: summary fetch failed with {} ({})",
            psn::statusName(error.status), error.message);

        brls::sync([onError, error]() { if (onError) onError(error.status, error.message); });
    });
}

void TrophyManager::fetchLibrary(bool forceRefresh, Callback<std::vector<psn::TrophyTitle>> onSuccess, ErrorCallback onError)
{
    HttpPool::instance().submit([this, forceRefresh, onSuccess, onError](HttpSession& session) {
        if (!forceRefresh)
        {
            std::vector<psn::TrophyTitle> cached;
            bool haveCached = false;

            {
                std::lock_guard<std::mutex> lock(mutex);
                if (hasCachedLibrary && cacheEntryFresh(librarySavedAt, LIBRARY_TTL_MINUTES))
                {
                    cached = cachedLibrary;
                    haveCached = true;
                }
            }

            if (!haveCached)
            {
                int64_t savedAt = 0;
                std::vector<psn::TrophyTitle> fromDisk;
                if (loadLibraryFromDisk(fromDisk, savedAt) && cacheEntryFresh(savedAt, LIBRARY_TTL_MINUTES))
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    cachedLibrary = fromDisk;
                    hasCachedLibrary = true;
                    librarySavedAt = savedAt;
                    cached = std::move(fromDisk);
                    haveCached = true;
                    brls::Logger::info("Trophy: {} title(s) served from disk cache", cached.size());
                }
            }

            if (haveCached)
            {
                prefetchIcons(cached);
                brls::sync([onSuccess, cached]() { if (onSuccess) onSuccess(cached); });
                return;
            }
        }

        std::vector<psn::TrophyTitle> titles;
        psn::Error error = clientFor(session).fetchTitles(titles);

        if (error.ok())
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                cachedLibrary = titles;
                hasCachedLibrary = true;
                librarySavedAt = static_cast<int64_t>(std::time(nullptr));
            }

            ensureCacheDirs();
            saveLibraryToDisk(titles);
            logLibrary(titles);
            prefetchIcons(titles);

            brls::sync([onSuccess, titles]() { if (onSuccess) onSuccess(titles); });
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            if (hasCachedLibrary)
            {
                brls::Logger::warning("Trophy: library fetch failed with {} ({}), {} stale title(s) held in memory",
                    psn::statusName(error.status), error.message, cachedLibrary.size());
            }
            else
            {
                brls::Logger::error("Trophy: library fetch failed with {} ({}), no cache available",
                    psn::statusName(error.status), error.message);
            }
        }

        brls::sync([onError, error]() { if (onError) onError(error.status, error.message); });
    });
}

void TrophyManager::fetchIcon(const std::string& url, IconCallback onSuccess)
{
    if (url.empty())
        return;

    {
        std::lock_guard<std::mutex> lock(iconMutex);

        auto cached = iconCache.find(url);
        if (cached != iconCache.end())
        {
            if (onSuccess)
            {
                std::vector<uint8_t> bytes = cached->second;
                brls::sync([onSuccess, url, bytes]() { onSuccess(url, bytes); });
            }
            return;
        }

        // Every caller for a URL is queued, not just the first. Dropping later callers
        // meant a prefetch with no callback could claim the slot and leave the card that
        // actually wanted the image waiting for a result it never received.
        auto existing = iconWaiters.find(url);
        bool alreadyInFlight = existing != iconWaiters.end();

        if (onSuccess)
            iconWaiters[url].push_back(std::move(onSuccess));
        else if (!alreadyInFlight)
            iconWaiters[url];

        if (alreadyInFlight)
            return;
    }

    HttpPool::instance().submit([this, url](HttpSession& session) {
        ensureIconCacheDir();

        std::string path = iconCachePath(url);
        std::vector<uint8_t> bytes;
        bool usable = readIconFromDisk(path, bytes);
        bool fromDisk = usable;

        if (!usable)
        {
            HttpResponse response = session.get(url, "", ICON_TIMEOUT_S);

            if (response.transportFailed())
            {
                brls::Logger::warning("Trophy icon fetch failed ({}): {}", url, response.error);
            }
            else if (response.status != 200)
            {
                brls::Logger::warning("Trophy icon fetch returned HTTP {} for {}", response.status, url);
            }
            else if (response.body.size() > ICON_MAX_BYTES)
            {
                brls::Logger::warning("Trophy icon {} is {} bytes, over the {} byte cap, dropping",
                    url, response.body.size(), ICON_MAX_BYTES);
            }
            else if (response.body.empty())
            {
                brls::Logger::warning("Trophy icon {} came back empty", url);
            }
            else
            {
                bytes.assign(response.body.begin(), response.body.end());
                usable = true;
            }
        }

        if (usable)
            storeIconInMemory(url, bytes);

        std::vector<IconCallback> waiters;
        {
            std::lock_guard<std::mutex> lock(iconMutex);
            auto entry = iconWaiters.find(url);
            if (entry != iconWaiters.end())
            {
                waiters.swap(entry->second);
                iconWaiters.erase(entry);
            }
        }

        if (!usable)
            return;

        if (!fromDisk && iconDiskWritable.load())
            writeIconToDisk(path, bytes);

        if (waiters.empty())
            return;

        brls::sync([waiters, url, bytes]() {
            for (const IconCallback& waiter : waiters)
                waiter(url, bytes);
        });
    });
}

void TrophyManager::setSummaryObserver(Callback<psn::TrophySummary> observer)
{
    summaryObserver = std::move(observer);
}

void TrophyManager::setLibraryObserver(Callback<std::vector<psn::TrophyTitle>> observer)
{
    libraryObserver = std::move(observer);
}

void TrophyManager::startAutoRefresh()
{
    if (autoRefreshStarted)
        return;

    autoRefreshStarted = true;

    staleTimer.setCallback([this]() { runStaleCheck(); });
    staleTimer.start(STALE_CHECK_MINUTES * 60 * 1000);

    brls::Logger::info("Trophy: auto refresh checking every {} min", STALE_CHECK_MINUTES);
}

void TrophyManager::runStaleCheck()
{
    bool stale = false;

    {
        std::lock_guard<std::mutex> lock(mutex);

        // Only keep warm what has actually been loaded once. Nothing cached means the user
        // has never opened the tab, and refreshing on their behalf would be traffic they
        // never asked for.
        if (!hasCachedLibrary)
            return;

        stale = !cacheEntryFresh(librarySavedAt, LIBRARY_TTL_MINUTES);
    }

    if (!stale)
        return;

    brls::Logger::info("Trophy: cache passed its TTL, refreshing in the background");

    fetchSummary(false,
        [this](const psn::TrophySummary& summary) {
            if (summaryObserver)
                summaryObserver(summary);
        },
        [](psn::Status status, const std::string& message) {
            brls::Logger::warning("Trophy: background summary refresh failed [{}] {}",
                psn::statusName(status), message);
        });

    fetchLibrary(false,
        [this](const std::vector<psn::TrophyTitle>& titles) {
            if (libraryObserver)
                libraryObserver(titles);
        },
        [](psn::Status status, const std::string& message) {
            brls::Logger::warning("Trophy: background library refresh failed [{}] {}",
                psn::statusName(status), message);
        });
}

void TrophyManager::prefetchIcons(const std::vector<psn::TrophyTitle>& titles)
{
    int queued = 0;
    int skipped = 0;

    for (const psn::TrophyTitle& title : titles)
    {
        if (title.trophyTitleIconUrl.empty())
            continue;

        if (queued >= ICON_PREFETCH_CAP)
        {
            skipped++;
            continue;
        }

        queued++;
        // No callback: this only warms the cache. A visible card asking for the same URL
        // joins the in-flight request as a waiter and is served when it lands.
        fetchIcon(title.trophyTitleIconUrl, nullptr);
    }

    if (skipped > 0)
    {
        brls::Logger::info("Trophy: prefetching {} icon(s), {} beyond the cap left to load on scroll",
            queued, skipped);
    }
    else if (queued > 0)
    {
        brls::Logger::info("Trophy: prefetching {} icon(s) into the disk cache", queued);
    }
}

void TrophyManager::clearCache()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        hasCachedSummary = false;
        hasCachedLibrary = false;
        cachedLibrary.clear();
        summarySavedAt = 0;
        librarySavedAt = 0;
    }

    remove(SUMMARY_CACHE_PATH);
    remove(LIBRARY_CACHE_PATH);

    brls::Logger::info("Trophy: cache cleared (memory and disk)");
}
