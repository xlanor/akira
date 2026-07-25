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
    static constexpr int SUMMARY_TTL_MINUTES = 360;
    static constexpr int LIBRARY_TTL_MINUTES = 360;
    static constexpr long ICON_TIMEOUT_S = 20;
    static constexpr size_t ICON_CACHE_MAX_BYTES = 12 * 1024 * 1024;
    static constexpr size_t ICON_MAX_BYTES = 2 * 1024 * 1024;
    static constexpr const char* CACHE_DIR = "sdmc:/switch/akira/cache";
    static constexpr const char* TROPHY_CACHE_DIR = "sdmc:/switch/akira/cache/trophies";
    static constexpr const char* ICON_CACHE_DIR = "sdmc:/switch/akira/cache/trophies/icons";

    TrophyManager();

    bool hasConnectivity() const;
    TrophyStatus ensureToken(std::string& outToken, std::string& outMessage);
    void awaitBurstSlot();
    bool breakerOpen(int& outSecondsRemaining) const;
    void tripBreaker(int seconds);

    Response request(HttpSession& session, const std::string& path);

    Response fetchSummaryBlocking(HttpSession& session, TrophySummary& outSummary);
    Response fetchLibraryBlocking(HttpSession& session, std::vector<TrophyTitle>& outTitles);

    void ensureIconCacheDir();
    std::string iconCachePath(const std::string& url) const;
    bool readIconFromDisk(const std::string& path, std::vector<uint8_t>& outBytes) const;
    void writeIconToDisk(const std::string& path, const std::vector<uint8_t>& bytes) const;
    void storeIconInMemory(const std::string& url, const std::vector<uint8_t>& bytes);

    SettingsManager* settings = nullptr;
    std::atomic<bool> iconCacheDirReady{false};
    std::atomic<bool> iconDiskWritable{true};

    mutable std::mutex mutex;

    mutable std::mutex iconMutex;
    std::unordered_map<std::string, std::vector<uint8_t>> iconCache;
    std::deque<std::string> iconOrder;
    std::unordered_set<std::string> iconInFlight;
    size_t iconCacheBytes = 0;
    std::deque<std::chrono::steady_clock::time_point> burstWindow;
    std::chrono::steady_clock::time_point breakerUntil{};

    TrophySummary cachedSummary;
    bool hasCachedSummary = false;
    std::chrono::steady_clock::time_point summaryFetchedAt{};

    std::vector<TrophyTitle> cachedLibrary;
    bool hasCachedLibrary = false;
    std::chrono::steady_clock::time_point libraryFetchedAt{};
};

#endif // AKIRA_TROPHY_MANAGER_HPP
