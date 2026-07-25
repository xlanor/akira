#ifndef AKIRA_TROPHY_MANAGER_HPP
#define AKIRA_TROPHY_MANAGER_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <borealis.hpp>

#include "core/rate_limiter.hpp"
#include "util/http.hpp"
#include "util/http_pool.hpp"

class SettingsManager;

enum class TrophyStatus {
    Ok,
    NotLinked,
    SessionExpired,
    Offline,
    RateLimited,
    ServerError
};

const char* trophyStatusName(TrophyStatus status);

struct TrophyCounts {
    int bronze = 0;
    int silver = 0;
    int gold = 0;
    int platinum = 0;

    int total() const { return bronze + silver + gold + platinum; }
};

struct TrophySummary {
    std::string accountId;
    int trophyLevel = 0;
    int tier = 0;
    int progress = 0;
    int trophyPoint = 0;
    int trophyLevelBasePoint = 0;
    int trophyLevelNextPoint = 0;
    TrophyCounts earnedTrophies;
};

struct TrophyTitle {
    std::string npCommunicationId;
    std::string npServiceName;
    std::string trophyTitleName;
    std::string trophyTitleDetail;
    std::string trophyTitleIconUrl;
    std::string trophyTitlePlatform;
    std::string trophySetVersion;
    bool hasTrophyGroups = false;
    int trophyGroupCount = 0;
    TrophyCounts definedTrophies;
    TrophyCounts earnedTrophies;
    int progress = 0;
    bool hiddenFlag = false;
    std::string lastUpdatedDateTime;
};

class TrophyManager {
public:
    template <typename T>
    using Callback = std::function<void(const T&)>;
    using ErrorCallback = std::function<void(TrophyStatus, const std::string&)>;

    static TrophyManager* getInstance();

    using IconCallback = std::function<void(const std::string& url, const std::vector<uint8_t>&)>;

    void fetchSummary(bool forceRefresh, Callback<TrophySummary> onSuccess, ErrorCallback onError);
    void fetchLibrary(bool forceRefresh, Callback<std::vector<TrophyTitle>> onSuccess, ErrorCallback onError);

    void fetchIcon(const std::string& url, IconCallback onSuccess);

    void clearCache();

    // Started once the feature is first used, so an install that never opens Trophies
    // never issues background traffic. Checks staleness on a timer rather than computing
    // one exact deadline, which stays correct across sleep, resume and clock changes.
    void startAutoRefresh();

    void setSummaryObserver(Callback<TrophySummary> observer);
    void setLibraryObserver(Callback<std::vector<TrophyTitle>> observer);

    PersistedRateLimiter::Status budgetStatus() const;

private:
    struct Response {
        TrophyStatus status = TrophyStatus::Ok;
        std::string body;
        std::string message;
    };

    static constexpr const char* TROPHY_API_BASE = "https://m.np.playstation.com/api/trophy/v1";
    static constexpr long REQUEST_TIMEOUT_S = 15;
    static constexpr int LIBRARY_PAGE_SIZE = 100;
    static constexpr int LIBRARY_PAGE_CAP = 50;
    static constexpr int LIBRARY_LOG_CAP = 50;
    static constexpr int MAX_ATTEMPTS = 3;
    static constexpr int BURST_LIMIT = 5;
    static constexpr int BURST_WINDOW_MS = 1000;
    static constexpr int BREAKER_MINUTES = 15;
    static constexpr int SUSTAINED_BUDGET = 300;
    static constexpr int STALE_CHECK_MINUTES = 5;
    static constexpr int ICON_PREFETCH_CAP = 120;
    static constexpr int SUMMARY_TTL_MINUTES = 360;
    static constexpr int LIBRARY_TTL_MINUTES = 360;
    static constexpr long ICON_TIMEOUT_S = 20;
    static constexpr size_t ICON_CACHE_MAX_BYTES = 12 * 1024 * 1024;
    static constexpr size_t ICON_MAX_BYTES = 2 * 1024 * 1024;
    static constexpr const char* CACHE_DIR = "sdmc:/switch/akira/cache";
    static constexpr const char* TROPHY_CACHE_DIR = "sdmc:/switch/akira/cache/trophies";
    static constexpr const char* ICON_CACHE_DIR = "sdmc:/switch/akira/cache/trophies/icons";
    static constexpr const char* RATELIMIT_PATH = "sdmc:/switch/akira/cache/ratelimit.json";
    static constexpr const char* LIBRARY_CACHE_PATH = "sdmc:/switch/akira/cache/trophies/library.json";
    static constexpr const char* SUMMARY_CACHE_PATH = "sdmc:/switch/akira/cache/trophies/summary.json";

    TrophyManager();

    bool hasConnectivity() const;
    TrophyStatus ensureToken(HttpSession& session, std::string& outToken, std::string& outMessage);
    void awaitBurstSlot();
    bool breakerOpen(int& outSecondsRemaining) const;
    void tripBreaker(int seconds);

    Response request(HttpSession& session, const std::string& path);

    Response fetchSummaryBlocking(HttpSession& session, TrophySummary& outSummary);
    Response fetchLibraryBlocking(HttpSession& session, std::vector<TrophyTitle>& outTitles);

    void ensureCacheDirs();
    void ensureIconCacheDir();

    bool loadLibraryFromDisk(std::vector<TrophyTitle>& outTitles, int64_t& outSavedAt) const;
    void saveLibraryToDisk(const std::vector<TrophyTitle>& titles) const;
    bool loadSummaryFromDisk(TrophySummary& outSummary, int64_t& outSavedAt) const;
    void saveSummaryToDisk(const TrophySummary& summary) const;
    static bool cacheEntryFresh(int64_t savedAt, int ttlMinutes);
    std::string iconCachePath(const std::string& url) const;
    bool readIconFromDisk(const std::string& path, std::vector<uint8_t>& outBytes) const;
    void writeIconToDisk(const std::string& path, const std::vector<uint8_t>& bytes) const;
    void storeIconInMemory(const std::string& url, const std::vector<uint8_t>& bytes);
    void prefetchIcons(const std::vector<TrophyTitle>& titles);
    void runStaleCheck();

    SettingsManager* settings = nullptr;
    PersistedRateLimiter limiter{RATELIMIT_PATH, SUSTAINED_BUDGET};

    brls::RepeatingTimer staleTimer;
    bool autoRefreshStarted = false;
    Callback<TrophySummary> summaryObserver;
    Callback<std::vector<TrophyTitle>> libraryObserver;
    std::atomic<bool> iconCacheDirReady{false};
    std::atomic<bool> iconDiskWritable{true};

    mutable std::mutex mutex;

    mutable std::mutex iconMutex;
    std::unordered_map<std::string, std::vector<uint8_t>> iconCache;
    std::deque<std::string> iconOrder;
    std::unordered_map<std::string, std::vector<IconCallback>> iconWaiters;
    size_t iconCacheBytes = 0;
    std::deque<std::chrono::steady_clock::time_point> burstWindow;
    std::chrono::steady_clock::time_point breakerUntil{};

    TrophySummary cachedSummary;
    bool hasCachedSummary = false;
    int64_t summarySavedAt = 0;

    std::vector<TrophyTitle> cachedLibrary;
    bool hasCachedLibrary = false;
    int64_t librarySavedAt = 0;
};

#endif // AKIRA_TROPHY_MANAGER_HPP
