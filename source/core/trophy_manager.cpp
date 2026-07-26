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
#include <unordered_set>

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

std::string TrophyManager::detailCachePath(const std::string& npCommunicationId) const
{
    std::string safe;
    safe.reserve(npCommunicationId.size());

    for (char c : npCommunicationId)
    {
        bool allowed = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') || c == '_' || c == '-';
        safe += allowed ? c : '_';
    }

    return std::format("{}/{}.json", DETAIL_CACHE_DIR, safe);
}

bool TrophyManager::loadDetailFromDisk(const std::string& npCommunicationId,
    psn::TitleDetail& outDetail, int64_t& outSavedAt) const
{
    psn::Json doc(readWholeFile(detailCachePath(npCommunicationId)));
    if (!doc)
        return false;

    outSavedAt = psn::jsonInt64(doc.get(), "savedAt");
    outDetail.npCommunicationId = psn::jsonString(doc.get(), "npCommunicationId");
    outDetail.npServiceName = psn::jsonString(doc.get(), "npServiceName");
    outDetail.lastUpdatedDateTime = psn::jsonString(doc.get(), "lastUpdatedDateTime");

    if (outDetail.npCommunicationId != npCommunicationId)
        return false;

    json_object* groups = nullptr;
    if (psn::jsonField(doc.get(), "groups", &groups) && json_object_is_type(groups, json_type_array))
    {
        size_t count = json_object_array_length(groups);
        for (size_t i = 0; i < count; i++)
        {
            psn::TrophyGroup group;
            if (psn::parseCachedGroup(json_object_array_get_idx(groups, i), group))
                outDetail.groups.push_back(std::move(group));
        }
    }

    json_object* trophies = nullptr;
    if (!psn::jsonField(doc.get(), "trophies", &trophies) || !json_object_is_type(trophies, json_type_array))
        return false;

    size_t count = json_object_array_length(trophies);
    outDetail.trophies.reserve(count);

    for (size_t i = 0; i < count; i++)
    {
        psn::Trophy trophy;
        if (psn::parseCachedTrophy(json_object_array_get_idx(trophies, i), trophy))
            outDetail.trophies.push_back(std::move(trophy));
    }

    return !outDetail.trophies.empty();
}

void TrophyManager::saveDetailToDisk(const psn::TitleDetail& detail) const
{
    json_object* groups = json_object_new_array();
    for (const psn::TrophyGroup& group : detail.groups)
        json_object_array_add(groups, psn::toJson(group));

    json_object* trophies = json_object_new_array();
    for (const psn::Trophy& trophy : detail.trophies)
        json_object_array_add(trophies, psn::toJson(trophy));

    json_object* root = json_object_new_object();
    json_object_object_add(root, "savedAt", json_object_new_int64(static_cast<int64_t>(std::time(nullptr))));
    json_object_object_add(root, "npCommunicationId", json_object_new_string(detail.npCommunicationId.c_str()));
    json_object_object_add(root, "npServiceName", json_object_new_string(detail.npServiceName.c_str()));
    json_object_object_add(root, "lastUpdatedDateTime", json_object_new_string(detail.lastUpdatedDateTime.c_str()));
    json_object_object_add(root, "groups", groups);
    json_object_object_add(root, "trophies", trophies);

    const char* text = json_object_to_json_string(root);
    if (!writeWholeFile(detailCachePath(detail.npCommunicationId), text ? text : ""))
        brls::Logger::warning("Trophy: could not write the detail cache for {}", detail.npCommunicationId);

    json_object_put(root);
}

PersistedRateLimiter::Status TrophyManager::budgetStatus() const
{
    return limiter.status();
}

int64_t TrophyManager::librarySavedAtSeconds() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return hasCachedLibrary ? librarySavedAt : 0;
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

void TrophyManager::loadTitleMapLocked()
{
    if (titleMapLoaded)
        return;

    titleMapLoaded = true;

    psn::Json doc(readWholeFile(TITLE_MAP_PATH));
    if (!doc)
        return;

    json_object_object_foreach(doc.get(), key, value)
    {
        if (!value || !json_object_is_type(value, json_type_object))
            continue;

        GameProgress entry;
        entry.playDurationSeconds = psn::jsonInt64(value, "playDurationSeconds");
        entry.lastPlayedDateTime = psn::jsonString(value, "lastPlayedDateTime");
        entry.valid = true;
        gameProgress[key] = std::move(entry);
    }

    brls::Logger::info("Trophy: {} game progression entr(ies) restored", gameProgress.size());
}

void TrophyManager::saveTitleMapLocked() const
{
    json_object* root = json_object_new_object();

    for (const auto& entry : gameProgress)
    {
        json_object* row = json_object_new_object();
        json_object_object_add(row, "playDurationSeconds", json_object_new_int64(entry.second.playDurationSeconds));
        json_object_object_add(row, "lastPlayedDateTime",
            json_object_new_string(entry.second.lastPlayedDateTime.c_str()));
        json_object_object_add(root, entry.first.c_str(), row);
    }

    const char* text = json_object_to_json_string(root);
    if (!writeWholeFile(TITLE_MAP_PATH, text ? text : ""))
        brls::Logger::warning("Trophy: could not write {}", TITLE_MAP_PATH);

    json_object_put(root);
}

TrophyManager::GameProgress TrophyManager::gameProgressFor(const std::string& npCommunicationId) const
{
    std::lock_guard<std::mutex> lock(mutex);
    const_cast<TrophyManager*>(this)->loadTitleMapLocked();

    auto entry = gameProgress.find(npCommunicationId);
    return entry == gameProgress.end() ? GameProgress{} : entry->second;
}

void TrophyManager::resolveGameProgress(const std::vector<psn::TrophyTitle>& titles, std::function<void()> onUpdated)
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        loadTitleMapLocked();

        if (gameProgressResolving)
            return;

        size_t missing = 0;
        for (const psn::TrophyTitle& title : titles)
        {
            if (gameProgress.find(title.npCommunicationId) == gameProgress.end())
                missing++;
        }

        if (missing == 0)
            return;

        gameProgressResolving = true;
        brls::Logger::info("Trophy: {} title(s) have no game progression yet, resolving", missing);
    }

    HttpPool::instance().submit([this, titles, onUpdated](HttpSession& session) {
        psn::Client client = clientFor(session);

        std::vector<psn::PlayedGame> games;
        psn::Error error = client.fetchPlayedGames(games);

        if (!error.ok())
        {
            brls::Logger::warning("Trophy: game progression unavailable [{}] {}",
                psn::statusName(error.status), error.message);

            std::lock_guard<std::mutex> lock(mutex);
            gameProgressResolving = false;
            return;
        }

        std::unordered_set<std::string> wanted;
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (const psn::TrophyTitle& title : titles)
            {
                if (gameProgress.find(title.npCommunicationId) == gameProgress.end())
                    wanted.insert(title.npCommunicationId);
            }
        }

        std::unordered_map<std::string, const psn::PlayedGame*> byTitleId;
        for (const psn::PlayedGame& game : games)
            byTitleId[game.titleId] = &game;

        int resolved = 0;
        int batches = 0;
        std::vector<std::string> batch;

        auto flush = [&]() {
            if (batch.empty() || wanted.empty())
            {
                batch.clear();
                return true;
            }

            std::vector<std::pair<std::string, std::string>> mapping;
            psn::Error mapError = client.fetchTitleMapping(batch, mapping);
            batches++;

            if (!mapError.ok())
            {
                brls::Logger::warning("Trophy: title mapping failed [{}] {}",
                    psn::statusName(mapError.status), mapError.message);
                batch.clear();
                return false;
            }

            std::lock_guard<std::mutex> lock(mutex);

            for (const auto& pair : mapping)
            {
                auto game = byTitleId.find(pair.first);
                if (game == byTitleId.end())
                    continue;

                GameProgress entry;
                entry.playDurationSeconds = game->second->playDurationSeconds;
                entry.lastPlayedDateTime = game->second->lastPlayedDateTime;
                entry.valid = true;

                gameProgress[pair.second] = std::move(entry);
                wanted.erase(pair.second);
                resolved++;
            }

            batch.clear();
            return true;
        };

        for (const psn::PlayedGame& game : games)
        {
            if (wanted.empty())
                break;

            batch.push_back(game.titleId);

            if (batch.size() < psn::Client::TITLE_MAP_BATCH)
                continue;

            if (!flush())
                break;
        }

        if (!wanted.empty())
            flush();

        {
            std::lock_guard<std::mutex> lock(mutex);
            gameProgressResolving = false;

            if (resolved > 0)
            {
                ensureCacheDirs();
                saveTitleMapLocked();
            }
        }

        brls::Logger::info("Trophy: resolved game progression for {} title(s) over {} mapping call(s), {} unmatched",
            resolved, batches, wanted.size());

        if (resolved > 0 && onUpdated)
            brls::sync([onUpdated]() { onUpdated(); });
    });
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

void TrophyManager::writeIconToDisk(const std::string& path, const std::vector<uint8_t>& bytes)
{
    std::string temp = path + ".tmp";

    FILE* file = fopen(temp.c_str(), "wb");
    if (!file)
    {
        stopIconDiskWrites(std::format("could not open {}: {}", temp, strerror(errno)));
        return;
    }

    size_t written = fwrite(bytes.data(), 1, bytes.size(), file);
    fclose(file);

    if (written != bytes.size())
    {
        remove(temp.c_str());
        stopIconDiskWrites(std::format("short write to {} ({}/{} bytes)", temp, written, bytes.size()));
        return;
    }

    if (!replaceFile(temp, path))
        stopIconDiskWrites(std::format("could not replace {}", path));
}

void TrophyManager::stopIconDiskWrites(const std::string& reason)
{
    if (!iconDiskWritable.exchange(false))
        return;

    brls::Logger::error("Trophy: icon disk cache disabled for this session ({}); icons will still load from memory", reason);
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

static psn::Credential credentialForUrl(const std::string& url)
{
    return url.find("/api/gamelist/") != std::string::npos
        ? psn::Credential::MobileSso
        : psn::Credential::RemotePlay;
}

psn::Error TrophyManager::governedGet(HttpSession& session, const std::string& url, std::string& outBody)
{
    psn::Auth& auth = psn::Auth::forCredential(credentialForUrl(url));

    int breakerSeconds = 0;
    if (breakerOpen(breakerSeconds))
    {
        psn::Error blocked{psn::Status::RateLimited,
            std::format("PSN rate limit cooldown active, {}s remaining", breakerSeconds)};
        brls::Logger::warning("Trophy: {} blocked, {}", url, blocked.message);
        return blocked;
    }

    if (auth.state() == psn::SessionState::NotLinked)
        return {psn::Status::NotLinked, "PSN account not linked"};

    if (!hasConnectivity())
    {
        brls::Logger::info("Trophy: {} skipped, no network connection", url);
        return {psn::Status::Offline, "No network connection"};
    }

    psn::Error sessionError = auth.ensureSession(session);
    if (!sessionError.ok())
        return sessionError;

    std::string token = auth.accessToken();

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

            psn::Error refreshError = auth.ensureSession(session, true);
            if (!refreshError.ok())
                return refreshError;

            token = auth.accessToken();
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

psn::Error TrophyManager::fetchDetailBlocking(HttpSession& session, const psn::TrophyTitle& title,
    psn::TitleDetail& outDetail)
{
    psn::Client client = clientFor(session);

    outDetail.npCommunicationId = title.npCommunicationId;
    outDetail.npServiceName = title.npServiceName;
    outDetail.lastUpdatedDateTime = title.lastUpdatedDateTime;

    std::vector<psn::Trophy> definitions;
    psn::Error error = client.fetchTrophyDefinitions(title.npCommunicationId, title.npServiceName, definitions);
    if (!error.ok())
        return error;

    std::vector<psn::Trophy> progress;
    error = client.fetchTrophyProgress(title.npCommunicationId, title.npServiceName, progress);
    if (!error.ok())
        return error;

    psn::mergeTrophies(definitions, progress);
    outDetail.trophies = std::move(definitions);

    if (!title.hasTrophyGroups)
    {
        psn::TrophyGroup base;
        base.trophyGroupId = "default";
        base.trophyGroupName = title.trophyTitleName;
        base.trophyGroupIconUrl = title.trophyTitleIconUrl;
        base.definedTrophies = title.definedTrophies;
        base.earnedTrophies = title.earnedTrophies;
        base.progress = title.progress;
        base.lastUpdatedDateTime = title.lastUpdatedDateTime;
        outDetail.groups.push_back(std::move(base));

        brls::Logger::info("Trophy detail {}: {} trophies, single group",
            title.npCommunicationId, outDetail.trophies.size());
        return {};
    }

    std::vector<psn::TrophyGroup> groups;
    error = client.fetchGroupDefinitions(title.npCommunicationId, title.npServiceName, groups);
    if (!error.ok())
        return error;

    std::vector<psn::TrophyGroup> groupProgress;
    psn::Error progressError = client.fetchGroupProgress(title.npCommunicationId, title.npServiceName, groupProgress);

    if (progressError.ok())
    {
        psn::mergeGroups(groups, groupProgress);
    }
    else
    {
        psn::tallyGroupEarned(groups, outDetail.trophies);
        brls::Logger::warning("Trophy detail {}: group progress failed ({}), earned counts tallied from trophies",
            title.npCommunicationId, progressError.message);
    }

    outDetail.groups = std::move(groups);

    brls::Logger::info("Trophy detail {}: {} trophies across {} group(s)",
        title.npCommunicationId, outDetail.trophies.size(), outDetail.groups.size());
    return {};
}

void TrophyManager::fetchTitleDetail(const psn::TrophyTitle& title, bool forceRefresh,
    Callback<psn::TitleDetail> onSuccess, ErrorCallback onError)
{
    HttpPool::instance().submit([this, title, forceRefresh, onSuccess, onError](HttpSession& session) {
        const std::string& id = title.npCommunicationId;

        if (!forceRefresh)
        {
            psn::TitleDetail cached;
            bool haveCached = false;

            {
                std::lock_guard<std::mutex> lock(mutex);
                auto entry = cachedDetails.find(id);
                if (entry != cachedDetails.end() && entry->second.lastUpdatedDateTime == title.lastUpdatedDateTime)
                {
                    cached = entry->second;
                    haveCached = true;
                }
            }

            if (!haveCached)
            {
                int64_t savedAt = 0;
                psn::TitleDetail fromDisk;

                if (loadDetailFromDisk(id, fromDisk, savedAt))
                {
                    bool signalMatches = !title.lastUpdatedDateTime.empty() &&
                        fromDisk.lastUpdatedDateTime == title.lastUpdatedDateTime;

                    if (signalMatches || cacheEntryFresh(savedAt, DETAIL_TTL_MINUTES))
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        cachedDetails[id] = fromDisk;
                        cached = std::move(fromDisk);
                        haveCached = true;

                        brls::Logger::info("Trophy detail {} served from disk cache ({})",
                            id, signalMatches ? "unchanged since last fetch" : "within TTL");
                    }
                    else
                    {
                        brls::Logger::info("Trophy detail {} is stale, refetching", id);
                    }
                }
            }

            if (haveCached)
            {
                brls::sync([onSuccess, cached]() { if (onSuccess) onSuccess(cached); });
                return;
            }
        }

        psn::TitleDetail detail;
        psn::Error error = fetchDetailBlocking(session, title, detail);

        if (error.ok())
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                cachedDetails[id] = detail;
            }

            ensureCacheDirs();
            mkdir(DETAIL_CACHE_DIR, 0755);
            saveDetailToDisk(detail);

            brls::sync([onSuccess, detail]() { if (onSuccess) onSuccess(detail); });
            return;
        }

        brls::Logger::error("Trophy: detail fetch for {} failed with {} ({})",
            id, psn::statusName(error.status), error.message);

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

void TrophyManager::discardIcon(const std::string& url)
{
    {
        std::lock_guard<std::mutex> lock(iconMutex);

        auto entry = iconCache.find(url);
        if (entry != iconCache.end())
        {
            iconCacheBytes -= entry->second.size();
            iconCache.erase(entry);
        }

        auto position = std::find(iconOrder.begin(), iconOrder.end(), url);
        if (position != iconOrder.end())
            iconOrder.erase(position);
    }

    remove(iconCachePath(url).c_str());
    brls::Logger::warning("Trophy: discarded the cached icon for {}", url);
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
