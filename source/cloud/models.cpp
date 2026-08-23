#include "cloud/models.hpp"

#include "psn/models.hpp"

#include <json-c/json.h>

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cloud {

namespace {

bool parseGame(json_object* obj, Game& out)
{
    out.productId = psn::jsonString(obj, "productId");
    out.name = psn::jsonString(obj, "name");
    out.imageUrl = psn::jsonString(obj, "imageUrl");
    out.landscapeImageUrl = psn::jsonString(obj, "landscapeImageUrl");
    out.conceptId = psn::jsonString(obj, "conceptId");
    out.category = psn::jsonString(obj, "category");
    out.serviceType = psn::jsonString(obj, "serviceType");
    out.platform = psn::jsonString(obj, "platform");
    out.isOwned = psn::jsonBool(obj, "isOwned");
    out.streamServiceType = psn::jsonString(obj, "streamServiceType");
    out.streamIdentifier = psn::jsonString(obj, "streamIdentifier");
    out.entitlementId = psn::jsonString(obj, "entitlementId");
    out.storeProductId = psn::jsonString(obj, "storeProductId");
    out.conceptUrl = psn::jsonString(obj, "conceptUrl");
    out.plusCatalog = psn::jsonBool(obj, "plusCatalog");

    return !out.productId.empty() && !out.name.empty();
}

} // namespace

std::string Game::artworkUrl() const
{
    return !landscapeImageUrl.empty() ? landscapeImageUrl : imageUrl;
}

int Catalog::launchableCount() const
{
    int count = 0;
    for (const Game& game : games)
        count += game.launchable() ? 1 : 0;
    return count;
}

std::vector<Datacenter> parseDatacenters(const std::string& json)
{
    std::vector<Datacenter> out;
    if (json.empty())
        return out;

    psn::Json doc(json);
    json_object* root = doc.get();
    if (!root || !json_object_is_type(root, json_type_array))
        return out;

    size_t count = json_object_array_length(root);
    out.reserve(count);
    for (size_t i = 0; i < count; i++)
    {
        json_object* item = json_object_array_get_idx(root, i);
        if (!item || !json_object_is_type(item, json_type_object))
            continue;
        Datacenter dc;
        dc.name = psn::jsonString(item, "dataCenter");
        dc.rttMs = psn::jsonInt(item, "rtt");
        dc.mtuIn = psn::jsonInt(item, "mtu_in");
        dc.mtuOut = psn::jsonInt(item, "mtu_out");
        dc.port = psn::jsonInt(item, "port");
        dc.publicIp = psn::jsonString(item, "publicIp");
        dc.maxBandwidth = psn::jsonInt(item, "maxBandwidth");
        dc.measured = psn::jsonBool(item, "measured");

        json_object* rtts = nullptr;
        if (psn::jsonField(item, "rtts", &rtts) && json_object_is_type(rtts, json_type_array))
        {
            size_t rttCount = json_object_array_length(rtts);
            dc.rttSamples.reserve(rttCount);
            for (size_t j = 0; j < rttCount; j++)
            {
                json_object* rtt = json_object_array_get_idx(rtts, j);
                if (rtt)
                    dc.rttSamples.push_back(json_object_get_int(rtt));
            }
        }

        if (!dc.name.empty())
            out.push_back(std::move(dc));
    }

    std::sort(out.begin(), out.end(), [](const Datacenter& a, const Datacenter& b) {
        return a.rttMs < b.rttMs;
    });
    return out;
}

std::string serializeDatacenters(const std::vector<Datacenter>& datacenters)
{
    if (datacenters.empty())
        return "";

    json_object* arr = json_object_new_array();
    for (const Datacenter& dc : datacenters)
    {
        json_object* o = json_object_new_object();
        json_object_object_add(o, "dataCenter", json_object_new_string(dc.name.c_str()));
        json_object_object_add(o, "rtt", json_object_new_int(dc.rttMs));
        json_object* rtts = json_object_new_array();
        for (int sample : dc.rttSamples)
            json_object_array_add(rtts, json_object_new_int(sample));
        json_object_object_add(o, "rtts", rtts);
        json_object_object_add(o, "mtu_in", json_object_new_int(dc.mtuIn));
        json_object_object_add(o, "mtu_out", json_object_new_int(dc.mtuOut));
        json_object_object_add(o, "port", json_object_new_int(dc.port));
        json_object_object_add(o, "publicIp", json_object_new_string(dc.publicIp.c_str()));
        json_object_object_add(o, "maxBandwidth", json_object_new_int(dc.maxBandwidth));
        json_object_object_add(o, "measured", json_object_new_boolean(dc.measured));
        json_object_array_add(arr, o);
    }
    const char* s = json_object_to_json_string(arr);
    std::string result = s ? s : "[]";
    json_object_put(arr);
    return result;
}

bool parseCatalog(const std::string& json, Catalog& out)
{
    psn::Json doc(json);
    json_object* root = doc.get();
    if (!root)
        return false;

    out = Catalog{};
    out.schemaVersion = psn::jsonInt(root, "schemaVersion");
    out.total = psn::jsonInt(root, "total");
    out.nativeMode = psn::jsonBool(root, "nativeMode");
    out.fallbackRegion = psn::jsonString(root, "fallbackRegion");
    out.resolvedStoreLang = psn::jsonString(root, "resolvedStoreLang");
    out.settledLocale = psn::jsonString(root, "settledLocale");
    out.warning = psn::jsonString(root, "warning");

    json_object* gamesObj = nullptr;
    if (!psn::jsonField(root, "games", &gamesObj) || !json_object_is_type(gamesObj, json_type_array))
        return true;

    size_t count = json_object_array_length(gamesObj);
    out.games.reserve(count);
    for (size_t i = 0; i < count; i++)
    {
        json_object* item = json_object_array_get_idx(gamesObj, i);
        if (!item || !json_object_is_type(item, json_type_object))
            continue;

        Game game;
        if (parseGame(item, game))
            out.games.push_back(std::move(game));
    }

    std::vector<Game> deduped;
    deduped.reserve(out.games.size());
    std::unordered_map<std::string, size_t> byConcept;
    for (Game& game : out.games)
    {
        if (game.conceptId.empty())
        {
            deduped.push_back(std::move(game));
            continue;
        }

        auto it = byConcept.find(game.conceptId);
        if (it == byConcept.end())
        {
            byConcept.emplace(game.conceptId, deduped.size());
            deduped.push_back(std::move(game));
        }
        else if (!deduped[it->second].launchable() && game.launchable())
        {
            deduped[it->second] = std::move(game);
        }
    }
    out.games = std::move(deduped);

    return true;
}

WarningKind classifyWarning(const std::string& warning)
{
    if (warning.empty())
        return WarningKind::None;
    if (warning == "Your session has expired. Please log in again to see your owned games.")
        return WarningKind::SessionExpired;
    return WarningKind::Other;
}

LaunchFailureKind classifyLaunchFailure(const std::string& errorMessage)
{
    if (errorMessage.empty())
        return LaunchFailureKind::None;
    if (errorMessage == "AUTHORIZATION_FAILED")
        return LaunchFailureKind::AuthorizationFailed;
    if (errorMessage == "PS_PLUS_SUBSCRIPTION_REQUIRED")
        return LaunchFailureKind::PsPlusRequired;
    if (errorMessage.rfind("ACCOUNT_PRIVACY_SETTINGS", 0) == 0)
        return LaunchFailureKind::PrivacySettings;
    if (errorMessage == "PING_TIMEOUT")
        return LaunchFailureKind::PingTimeout;
    if (errorMessage.find("network error") != std::string::npos)
        return LaunchFailureKind::NetworkError;
    if (errorMessage.find("Selected datacenter") != std::string::npos)
        return LaunchFailureKind::DatacenterUnavailable;
    return LaunchFailureKind::Other;
}

std::string platformBadge(const Game& game)
{
    if (game.platform == "ps5")
        return "PS5";
    if (game.platform == "ps4")
        return "PS4";
    if (game.platform == "ps3")
        return "PS3";
    return "";
}

std::string categoryBadge(const Game& game)
{
    if (game.category == "owned")
        return "Owned";
    if (game.category == "streamable")
        return "Catalog";
    if (game.category == "purchaseable")
        return "Store";
    return "";
}

} // namespace cloud
