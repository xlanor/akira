#ifndef AKIRA_CLOUD_SERVICE_HPP
#define AKIRA_CLOUD_SERVICE_HPP

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "cloud/models.hpp"

class Host;
class SettingsManager;

namespace cloud {

enum class Availability {
    NoProfile,
    NeedsPairing,
    Checking,
    Ready,
    Warning,
    Empty,
    Error,
    LaunchBlocked
};

struct Status {
    Availability availability = Availability::NoProfile;
    std::string title;
    std::string detail;
    bool canBrowse = false;
    bool canPair = false;
    bool degraded = false;
    int gameCount = 0;
};

struct Snapshot {
    Status status;
    Catalog catalog;
    bool hasCatalog = false;
};

class Service {
public:
    using SnapshotCallback = std::function<void(const Snapshot&)>;
    using HostCallback = std::function<void(std::shared_ptr<Host>)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    using ProgressCallback = std::function<void(const std::string&)>;

    static Service& instance();

    Snapshot snapshotForActiveProfile() const;

    void markActiveProfileDirty();
    void clearCatalogCache();
    void refreshActiveProfile(bool force, SnapshotCallback onDone = {});
    void launchGame(const Game& game, HostCallback onSuccess, ErrorCallback onError,
        ProgressCallback onProgress = {}, bool forceSkipAttrCheck = false);

private:
    struct Entry {
        Snapshot snapshot;
        bool refreshing = false;
        int generation = 0;
        std::vector<SnapshotCallback> pending;
        std::string psnowDatacentersJson;
        std::string pscloudDatacentersJson;
    };

    Service();

    Snapshot defaultSnapshotForActiveProfile() const;
    Snapshot defaultSnapshotForProfile(bool hasProfile, bool paired) const;
    std::string consoleLocale() const;
    std::string catalogLocale() const;
    std::string streamLanguage() const;
    std::string reconcileCatalogLocale(int64_t profileId) const;
    void noteSettledLocale(const std::string& settled) const;
    std::string cacheRoot() const;
    std::string cacheDirForProfile(int64_t profileId) const;
    void ensureCacheDirsForProfile(int64_t profileId) const;

    void storeSnapshot(int64_t profileId, const Snapshot& snapshot);
    void storeLaunchError(int64_t profileId, const std::string& errorMessage);

    SettingsManager* settings = nullptr;

    mutable std::mutex mutex;
    std::map<int64_t, Entry> entries;
};

} // namespace cloud

#endif // AKIRA_CLOUD_SERVICE_HPP
