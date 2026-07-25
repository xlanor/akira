#include "core/trophy_manager.hpp"
#include "core/discovery_manager.hpp"
#include "core/settings_manager.hpp"

#include <borealis.hpp>
#include <algorithm>
#include <cstdio>
#include <format>
#include <thread>

#include <sys/stat.h>
#include <switch.h>

#include <json-c/json.h>

#include "util/http.hpp"

const char* trophyStatusName(TrophyStatus status)
{
    switch (status)
    {
        case TrophyStatus::Ok: return "Ok";
        case TrophyStatus::NotLinked: return "NotLinked";
        case TrophyStatus::SessionExpired: return "SessionExpired";
        case TrophyStatus::Offline: return "Offline";
        case TrophyStatus::RateLimited: return "RateLimited";
        case TrophyStatus::ServerError: return "ServerError";
    }
    return "Unknown";
}

static bool jsonField(json_object* parent, const char* key, json_object** out)
{
    return parent && json_object_object_get_ex(parent, key, out) && *out &&
        !json_object_is_type(*out, json_type_null);
}

static std::string sanitizeApiText(const char* raw)
{
    if (!raw)
        return std::string();

    std::string value(raw);

    for (char& c : value)
    {
        if (c == '\r' || c == '\n' || c == '\t')
            c = ' ';
    }

    size_t begin = value.find_first_not_of(' ');
    if (begin == std::string::npos)
        return std::string();

    size_t end = value.find_last_not_of(' ');
    return value.substr(begin, end - begin + 1);
}

static std::string jsonString(json_object* parent, const char* key)
{
    json_object* field = nullptr;
    if (!jsonField(parent, key, &field))
        return std::string();

    return sanitizeApiText(json_object_get_string(field));
}

static int jsonInt(json_object* parent, const char* key)
{
    json_object* field = nullptr;
    if (!jsonField(parent, key, &field))
        return 0;

    if (json_object_is_type(field, json_type_string))
    {
        const char* value = json_object_get_string(field);
        if (!value)
            return 0;

        try
        {
            return std::stoi(value);
        }
        catch (const std::exception&)
        {
            return 0;
        }
    }

    return json_object_get_int(field);
}

static bool jsonBool(json_object* parent, const char* key)
{
    json_object* field = nullptr;
    if (!jsonField(parent, key, &field))
        return false;

    return json_object_get_boolean(field);
}

static TrophyCounts jsonCounts(json_object* parent, const char* key)
{
    TrophyCounts counts;

    json_object* field = nullptr;
    if (!jsonField(parent, key, &field))
        return counts;

    counts.bronze = jsonInt(field, "bronze");
    counts.silver = jsonInt(field, "silver");
    counts.gold = jsonInt(field, "gold");
    counts.platinum = jsonInt(field, "platinum");
    return counts;
}

TrophyManager* TrophyManager::getInstance()
{
    static TrophyManager* instance = new TrophyManager();
    return instance;
}

TrophyManager::TrophyManager()
{
    settings = SettingsManager::getInstance();
}

void TrophyManager::ensureIconCacheDir()
{
    if (iconCacheDirReady.load())
        return;

    mkdir(CACHE_DIR, 0755);
    mkdir(TROPHY_CACHE_DIR, 0755);
    mkdir(ICON_CACHE_DIR, 0755);

    iconCacheDirReady.store(true);
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

    if (rename(temp.c_str(), path.c_str()) != 0)
        remove(temp.c_str());
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

TrophyStatus TrophyManager::ensureToken(std::string& outToken, std::string& outMessage)
{
    DiscoveryManager* discovery = DiscoveryManager::getInstance();

    if (discovery->isPsnTokenValid())
    {
        outToken = settings->getPsnAccessToken();
        if (outToken.empty())
        {
            outMessage = "No PSN access token stored";
            return TrophyStatus::NotLinked;
        }
        return TrophyStatus::Ok;
    }

    if (settings->getPsnRefreshToken().empty())
    {
        outMessage = "PSN account not linked";
        return TrophyStatus::NotLinked;
    }

    brls::Logger::info("Trophy: access token expired, refreshing");
    PsnResult refresh = discovery->refreshPsnTokenBlocking();

    if (refresh.success)
    {
        outToken = settings->getPsnAccessToken();
        return TrophyStatus::Ok;
    }

    outMessage = refresh.message;

    if (refresh.error == PsnAuthError::Invalid)
    {
        brls::Logger::error("Trophy: PSN refresh token rejected ({}), clearing stored PSN token data", refresh.message);
        settings->clearPsnTokenData();
        settings->writeFile();
        return TrophyStatus::SessionExpired;
    }

    brls::Logger::warning("Trophy: token refresh failed transiently ({}), keeping stored tokens", refresh.message);
    return TrophyStatus::Offline;
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

TrophyManager::Response TrophyManager::request(HttpSession& session, const std::string& path)
{
    Response result;

    int breakerSeconds = 0;
    if (breakerOpen(breakerSeconds))
    {
        result.status = TrophyStatus::RateLimited;
        result.message = std::format("PSN rate limit cooldown active, {}s remaining", breakerSeconds);
        brls::Logger::warning("Trophy: {} blocked, {}", path, result.message);
        return result;
    }

    if (settings->getPsnAccessToken().empty() && settings->getPsnRefreshToken().empty())
    {
        result.status = TrophyStatus::NotLinked;
        result.message = "PSN account not linked";
        return result;
    }

    if (!hasConnectivity())
    {
        result.status = TrophyStatus::Offline;
        result.message = "No network connection";
        brls::Logger::info("Trophy: {} skipped, no network connection", path);
        return result;
    }

    std::string token;
    std::string tokenMessage;
    TrophyStatus tokenStatus = ensureToken(token, tokenMessage);
    if (tokenStatus != TrophyStatus::Ok)
    {
        result.status = tokenStatus;
        result.message = tokenMessage;
        return result;
    }

    std::string url = std::string(TROPHY_API_BASE) + path;
    bool refreshedOn401 = false;
    int backoffSeconds = 2;

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++)
    {
        awaitBurstSlot();

        HttpResponse response = session.get(url, token, REQUEST_TIMEOUT_S);

        if (!response.transportFailed() && response.status == 200)
        {
            result.status = TrophyStatus::Ok;
            result.body = std::move(response.body);
            return result;
        }

        if (!response.transportFailed() && response.status == 401)
        {
            if (refreshedOn401)
            {
                result.status = TrophyStatus::SessionExpired;
                result.message = "PSN rejected the access token after a refresh";
                brls::Logger::error("Trophy: {} still 401 after refresh, giving up", path);
                return result;
            }

            refreshedOn401 = true;
            brls::Logger::info("Trophy: {} returned 401, refreshing token once", path);

            PsnResult refresh = DiscoveryManager::getInstance()->refreshPsnTokenBlocking();
            if (!refresh.success)
            {
                result.message = refresh.message;
                if (refresh.error == PsnAuthError::Invalid)
                {
                    settings->clearPsnTokenData();
                    settings->writeFile();
                    result.status = TrophyStatus::SessionExpired;
                }
                else
                {
                    result.status = TrophyStatus::Offline;
                }
                return result;
            }

            token = settings->getPsnAccessToken();
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

            result.status = TrophyStatus::RateLimited;
            result.message = std::format("PSN is rate-limiting, backing off for {}s", cooldown);
            brls::Logger::error("Trophy: {} returned 429 (Retry-After '{}'), tripping breaker for {}s",
                path, retryAfter, cooldown);
            return result;
        }

        bool retryable = response.transportFailed() || response.status >= 500;

        if (response.transportFailed())
        {
            result.status = TrophyStatus::Offline;
            result.message = response.error;
            brls::Logger::warning("Trophy: {} attempt {}/{} transport failure: {}",
                path, attempt, MAX_ATTEMPTS, response.error);
        }
        else
        {
            result.status = TrophyStatus::ServerError;
            result.message = std::format("HTTP {}", response.status);
            brls::Logger::warning("Trophy: {} attempt {}/{} returned HTTP {}",
                path, attempt, MAX_ATTEMPTS, response.status);
        }

        if (!retryable)
            return result;

        if (attempt < MAX_ATTEMPTS)
        {
            brls::Logger::info("Trophy: retrying {} in {}s", path, backoffSeconds);
            std::this_thread::sleep_for(std::chrono::seconds(backoffSeconds));
            backoffSeconds *= 2;
        }
    }

    return result;
}

TrophyManager::Response TrophyManager::fetchSummaryBlocking(HttpSession& session, TrophySummary& outSummary)
{
    Response response = request(session, "/users/me/trophySummary");
    if (response.status != TrophyStatus::Ok)
        return response;

    json_object* parsed = json_tokener_parse(response.body.c_str());
    if (!parsed)
    {
        response.status = TrophyStatus::ServerError;
        response.message = "Could not parse trophySummary response";
        brls::Logger::error("Trophy: {}", response.message);
        return response;
    }

    outSummary.accountId = jsonString(parsed, "accountId");
    outSummary.trophyLevel = jsonInt(parsed, "trophyLevel");
    outSummary.tier = jsonInt(parsed, "tier");
    outSummary.progress = jsonInt(parsed, "progress");
    outSummary.trophyPoint = jsonInt(parsed, "trophyPoint");
    outSummary.trophyLevelBasePoint = jsonInt(parsed, "trophyLevelBasePoint");
    outSummary.trophyLevelNextPoint = jsonInt(parsed, "trophyLevelNextPoint");
    outSummary.earnedTrophies = jsonCounts(parsed, "earnedTrophies");

    json_object_put(parsed);

    brls::Logger::info("Trophy summary: level {} tier {} progress {}% points {} ({}/{}) earned {} (P{} G{} S{} B{})",
        outSummary.trophyLevel, outSummary.tier, outSummary.progress, outSummary.trophyPoint,
        outSummary.trophyLevelBasePoint, outSummary.trophyLevelNextPoint,
        outSummary.earnedTrophies.total(), outSummary.earnedTrophies.platinum,
        outSummary.earnedTrophies.gold, outSummary.earnedTrophies.silver,
        outSummary.earnedTrophies.bronze);

    return response;
}

TrophyManager::Response TrophyManager::fetchLibraryBlocking(HttpSession& session, std::vector<TrophyTitle>& outTitles)
{
    Response response;
    int offset = 0;
    int page = 0;
    int totalItemCount = -1;

    while (true)
    {
        if (page >= LIBRARY_PAGE_CAP)
        {
            brls::Logger::error("Trophy: library pagination hit the {}-page cap at offset {}, stopping with {} titles",
                LIBRARY_PAGE_CAP, offset, outTitles.size());
            break;
        }

        page++;

        response = request(session, std::format("/users/me/trophyTitles?limit={}&offset={}", LIBRARY_PAGE_SIZE, offset));
        if (response.status != TrophyStatus::Ok)
            return response;

        json_object* parsed = json_tokener_parse(response.body.c_str());
        if (!parsed)
        {
            response.status = TrophyStatus::ServerError;
            response.message = "Could not parse trophyTitles response";
            brls::Logger::error("Trophy: {}", response.message);
            return response;
        }

        if (totalItemCount < 0)
            totalItemCount = jsonInt(parsed, "totalItemCount");

        json_object* titles = nullptr;
        int pageCount = 0;

        if (jsonField(parsed, "trophyTitles", &titles) && json_object_is_type(titles, json_type_array))
        {
            pageCount = static_cast<int>(json_object_array_length(titles));

            for (int i = 0; i < pageCount; i++)
            {
                json_object* entry = json_object_array_get_idx(titles, i);
                if (!entry)
                    continue;

                TrophyTitle title;
                title.npCommunicationId = jsonString(entry, "npCommunicationId");
                title.npServiceName = jsonString(entry, "npServiceName");
                title.trophyTitleName = jsonString(entry, "trophyTitleName");
                title.trophyTitleDetail = jsonString(entry, "trophyTitleDetail");
                title.trophyTitleIconUrl = jsonString(entry, "trophyTitleIconUrl");
                title.trophyTitlePlatform = jsonString(entry, "trophyTitlePlatform");
                title.trophySetVersion = jsonString(entry, "trophySetVersion");
                title.hasTrophyGroups = jsonBool(entry, "hasTrophyGroups");
                title.trophyGroupCount = jsonInt(entry, "trophyGroupCount");
                title.definedTrophies = jsonCounts(entry, "definedTrophies");
                title.earnedTrophies = jsonCounts(entry, "earnedTrophies");
                title.progress = jsonInt(entry, "progress");
                title.hiddenFlag = jsonBool(entry, "hiddenFlag");
                title.lastUpdatedDateTime = jsonString(entry, "lastUpdatedDateTime");

                if (title.npCommunicationId.empty())
                {
                    brls::Logger::warning("Trophy: skipping library entry {} with no npCommunicationId", i);
                    continue;
                }

                outTitles.push_back(std::move(title));
            }
        }

        json_object* nextOffsetField = nullptr;
        bool hasNextOffset = jsonField(parsed, "nextOffset", &nextOffsetField);
        int nextOffset = hasNextOffset ? jsonInt(parsed, "nextOffset") : 0;

        json_object_put(parsed);

        if (pageCount == 0)
            break;

        if (!hasNextOffset)
            break;

        if (nextOffset <= offset)
        {
            brls::Logger::warning("Trophy: nextOffset {} did not advance past {}, stopping pagination",
                nextOffset, offset);
            break;
        }

        offset = nextOffset;
    }

    brls::Logger::info("Trophy library: {} titles over {} page(s), totalItemCount={}",
        outTitles.size(), page, totalItemCount);

    int logged = 0;
    for (const TrophyTitle& title : outTitles)
    {
        if (logged >= LIBRARY_LOG_CAP)
        {
            brls::Logger::info("Trophy library: {} further title(s) not logged",
                outTitles.size() - static_cast<size_t>(logged));
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

    if (totalItemCount >= 0 && static_cast<int>(outTitles.size()) != totalItemCount)
    {
        brls::Logger::warning("Trophy: parsed {} titles but totalItemCount says {}",
            outTitles.size(), totalItemCount);
    }

    response.status = TrophyStatus::Ok;
    return response;
}

void TrophyManager::fetchSummary(bool forceRefresh, Callback<TrophySummary> onSuccess, ErrorCallback onError)
{
    HttpPool::instance().submit([this, forceRefresh, onSuccess, onError](HttpSession& session) {
        if (!forceRefresh)
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (hasCachedSummary &&
                std::chrono::steady_clock::now() - summaryFetchedAt < std::chrono::minutes(SUMMARY_TTL_MINUTES))
            {
                TrophySummary cached = cachedSummary;
                brls::Logger::debug("Trophy: serving cached summary");
                brls::sync([onSuccess, cached]() { if (onSuccess) onSuccess(cached); });
                return;
            }
        }

        TrophySummary summary;
        Response response = fetchSummaryBlocking(session, summary);

        if (response.status == TrophyStatus::Ok)
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                cachedSummary = summary;
                hasCachedSummary = true;
                summaryFetchedAt = std::chrono::steady_clock::now();
            }

            brls::sync([onSuccess, summary]() { if (onSuccess) onSuccess(summary); });
            return;
        }

        brls::Logger::error("Trophy: summary fetch failed with {} ({})",
            trophyStatusName(response.status), response.message);

        TrophyStatus status = response.status;
        std::string message = response.message;
        brls::sync([onError, status, message]() { if (onError) onError(status, message); });
    });
}

void TrophyManager::fetchLibrary(bool forceRefresh, Callback<std::vector<TrophyTitle>> onSuccess, ErrorCallback onError)
{
    HttpPool::instance().submit([this, forceRefresh, onSuccess, onError](HttpSession& session) {
        if (!forceRefresh)
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (hasCachedLibrary &&
                std::chrono::steady_clock::now() - libraryFetchedAt < std::chrono::minutes(LIBRARY_TTL_MINUTES))
            {
                std::vector<TrophyTitle> cached = cachedLibrary;
                brls::Logger::debug("Trophy: serving {} cached title(s)", cached.size());
                brls::sync([onSuccess, cached]() { if (onSuccess) onSuccess(cached); });
                return;
            }
        }

        std::vector<TrophyTitle> titles;
        Response response = fetchLibraryBlocking(session, titles);

        if (response.status == TrophyStatus::Ok)
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                cachedLibrary = titles;
                hasCachedLibrary = true;
                libraryFetchedAt = std::chrono::steady_clock::now();
            }

            brls::sync([onSuccess, titles]() { if (onSuccess) onSuccess(titles); });
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            if (hasCachedLibrary)
            {
                brls::Logger::warning("Trophy: library fetch failed with {} ({}), {} stale title(s) held in memory",
                    trophyStatusName(response.status), response.message, cachedLibrary.size());
            }
            else
            {
                brls::Logger::error("Trophy: library fetch failed with {} ({}), no cache available",
                    trophyStatusName(response.status), response.message);
            }
        }

        TrophyStatus status = response.status;
        std::string message = response.message;
        brls::sync([onError, status, message]() { if (onError) onError(status, message); });
    });
}

void TrophyManager::fetchIcon(const std::string& url, IconCallback onSuccess)
{
    if (url.empty() || !onSuccess)
        return;

    {
        std::lock_guard<std::mutex> lock(iconMutex);

        auto cached = iconCache.find(url);
        if (cached != iconCache.end())
        {
            std::vector<uint8_t> bytes = cached->second;
            brls::sync([onSuccess, url, bytes]() { onSuccess(url, bytes); });
            return;
        }

        if (iconInFlight.count(url) > 0)
            return;

        iconInFlight.insert(url);
    }

    HttpPool::instance().submit([this, url, onSuccess](HttpSession& session) {
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

        {
            std::lock_guard<std::mutex> lock(iconMutex);
            iconInFlight.erase(url);
        }

        if (!usable)
            return;

        if (!fromDisk && iconDiskWritable.load())
            writeIconToDisk(path, bytes);

        brls::sync([onSuccess, url, bytes]() { onSuccess(url, bytes); });
    });
}

void TrophyManager::clearCache()
{
    std::lock_guard<std::mutex> lock(mutex);
    hasCachedSummary = false;
    hasCachedLibrary = false;
    cachedLibrary.clear();
    brls::Logger::info("Trophy: in-memory cache cleared");
}
