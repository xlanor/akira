#ifndef AKIRA_CLOUD_MODELS_HPP
#define AKIRA_CLOUD_MODELS_HPP

#include <string>
#include <vector>

namespace cloud {

struct Game {
    std::string productId;
    std::string name;
    std::string imageUrl;
    std::string landscapeImageUrl;
    std::string conceptId;
    std::string category;
    std::string serviceType;
    std::string platform;
    bool isOwned = false;
    std::string streamServiceType;
    std::string streamIdentifier;
    std::string entitlementId;
    std::string storeProductId;
    std::string conceptUrl;
    bool plusCatalog = false;

    bool launchable() const { return !streamServiceType.empty() && !streamIdentifier.empty(); }
    bool streamableNow() const { return launchable() && (isOwned || category != "purchaseable"); }
    std::string artworkUrl() const;
};

struct Catalog {
    int schemaVersion = 0;
    int total = 0;
    bool nativeMode = false;
    std::string fallbackRegion;
    std::string resolvedStoreLang;
    std::string settledLocale;
    std::string warning;
    std::vector<Game> games;

    bool foreignAccountCatalog() const { return !nativeMode; }
    int launchableCount() const;
};

enum class WarningKind {
    None,
    SessionExpired,
    Other
};

enum class LaunchFailureKind {
    None,
    AuthorizationFailed,
    PsPlusRequired,
    PrivacySettings,
    NetworkError,
    PingTimeout,
    DatacenterUnavailable,
    Other
};

struct Datacenter {
    std::string name;
    int rttMs = 0;
    std::vector<int> rttSamples;
    int mtuIn = 0;
    int mtuOut = 0;
    int port = 0;
    std::string publicIp;
    int maxBandwidth = 0;
    bool measured = false;
};

std::vector<Datacenter> parseDatacenters(const std::string& json);
std::string serializeDatacenters(const std::vector<Datacenter>& datacenters);

bool parseCatalog(const std::string& json, Catalog& out);
WarningKind classifyWarning(const std::string& warning);
LaunchFailureKind classifyLaunchFailure(const std::string& errorMessage);

std::string platformBadge(const Game& game);
std::string categoryBadge(const Game& game);

} // namespace cloud

#endif // AKIRA_CLOUD_MODELS_HPP
