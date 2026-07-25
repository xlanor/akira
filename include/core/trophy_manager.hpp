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
#include <vector>

#include <borealis.hpp>

#include "core/rate_limiter.hpp"
#include "psn/auth.hpp"
#include "psn/client.hpp"
#include "psn/models.hpp"
#include "util/http.hpp"
#include "util/http_pool.hpp"

class SettingsManager;

class TrophyManager {
public:
    template <typename T>
    using Callback = std::function<void(const T&)>;
    using ErrorCallback = std::function<void(psn::Status, const std::string&)>;

    static TrophyManager* getInstance();

    using IconCallback = std::function<void(const std::string& url, const std::vector<uint8_t>&)>;

    void fetchSummary(bool forceRefresh, Callback<psn::TrophySummary> onSuccess, ErrorCallback onError);
    void fetchLibrary(bool forceRefresh, Callback<std::vector<psn::TrophyTitle>> onSuccess, ErrorCallback onError);

    void fetchTitleDetail(const psn::TrophyTitle& title, bool forceRefresh,
        Callback<psn::TitleDetail> onSuccess, ErrorCallback onError);

    void fetchIcon(const std::string& url, IconCallback onSuccess);
    void discardIcon(const std::string& url);

    void clearCache();

    void startAutoRefresh();

    void setSummaryObserver(Callback<psn::TrophySummary> observer);
    void setLibraryObserver(Callback<std::vector<psn::TrophyTitle>> observer);

    PersistedRateLimiter::Status budgetStatus() const;
    int64_t librarySavedAtSeconds() const;

    static constexpr const char* SCOPE_LIBRARY = "library";

    psn::ActionStatus forceRefreshStatus(const std::string& scope = SCOPE_LIBRARY);
    void recordForcedRefresh(const std::string& scope = SCOPE_LIBRARY);

private:
    static constexpr long REQUEST_TIMEOUT_S = 15;
    static constexpr int MAX_ATTEMPTS = 3;
    static constexpr int BURST_LIMIT = 5;
    static constexpr int BURST_WINDOW_MS = 1000;
    static constexpr int BREAKER_MINUTES = 15;
    static constexpr int SUSTAINED_BUDGET = 300;
    static constexpr int STALE_CHECK_MINUTES = 5;
    static constexpr int FORCE_REFRESH_COOLDOWN_MINUTES = 360;
    static constexpr int LIBRARY_LOG_CAP = 50;
    static constexpr int ICON_PREFETCH_CAP = 120;
    static constexpr int SUMMARY_TTL_MINUTES = 360;
    static constexpr int DETAIL_TTL_MINUTES = 360;
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
    static constexpr const char* DETAIL_CACHE_DIR = "sdmc:/switch/akira/cache/trophies/detail";
    static constexpr const char* FORCE_STATE_PATH = "sdmc:/switch/akira/cache/trophies/refresh_state.json";

    TrophyManager();

    bool hasConnectivity() const;
    void awaitBurstSlot();
    bool breakerOpen(int& outSecondsRemaining) const;
    void tripBreaker(int seconds);

    psn::Error governedGet(HttpSession& session, const std::string& url, std::string& outBody);
    psn::Client clientFor(HttpSession& session);

    void ensureCacheDirs();
    void ensureIconCacheDir();

    bool loadLibraryFromDisk(std::vector<psn::TrophyTitle>& outTitles, int64_t& outSavedAt) const;
    void saveLibraryToDisk(const std::vector<psn::TrophyTitle>& titles) const;
    bool loadSummaryFromDisk(psn::TrophySummary& outSummary, int64_t& outSavedAt) const;
    std::string detailCachePath(const std::string& npCommunicationId) const;
    bool loadDetailFromDisk(const std::string& npCommunicationId, psn::TitleDetail& outDetail,
        int64_t& outSavedAt) const;
    void saveDetailToDisk(const psn::TitleDetail& detail) const;
    psn::Error fetchDetailBlocking(HttpSession& session, const psn::TrophyTitle& title,
        psn::TitleDetail& outDetail);
    void saveSummaryToDisk(const psn::TrophySummary& summary) const;
    static bool cacheEntryFresh(int64_t savedAt, int ttlMinutes);
    void loadForceStateLocked();
    void saveForceStateLocked() const;
    std::string iconCachePath(const std::string& url) const;
    bool readIconFromDisk(const std::string& path, std::vector<uint8_t>& outBytes) const;
    void writeIconToDisk(const std::string& path, const std::vector<uint8_t>& bytes) const;
    void storeIconInMemory(const std::string& url, const std::vector<uint8_t>& bytes);
    void prefetchIcons(const std::vector<psn::TrophyTitle>& titles);
    void logLibrary(const std::vector<psn::TrophyTitle>& titles) const;
    void runStaleCheck();

    SettingsManager* settings = nullptr;
    PersistedRateLimiter limiter{RATELIMIT_PATH, SUSTAINED_BUDGET};

    brls::RepeatingTimer staleTimer;
    bool autoRefreshStarted = false;
    Callback<psn::TrophySummary> summaryObserver;
    Callback<std::vector<psn::TrophyTitle>> libraryObserver;
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

    psn::TrophySummary cachedSummary;
    bool hasCachedSummary = false;
    int64_t summarySavedAt = 0;

    std::unordered_map<std::string, int64_t> forcedAt;
    bool forceStateLoaded = false;

    std::unordered_map<std::string, psn::TitleDetail> cachedDetails;

    std::vector<psn::TrophyTitle> cachedLibrary;
    bool hasCachedLibrary = false;
    int64_t librarySavedAt = 0;
};

#endif // AKIRA_TROPHY_MANAGER_HPP
