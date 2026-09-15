#include "cloud/service.hpp"

#include <borealis.hpp>
#include <borealis/core/i18n.hpp>

#include <chiaki/cloudcatalog.h>
#include <chiaki/cloudsession.h>
#include <json-c/json.h>

#include <cstdio>
#include <cstring>
#include <optional>
#include <sys/stat.h>
#include <unistd.h>

#include "core/host.hpp"
#include "core/settings_manager.hpp"
#include "core/trophy_manager.hpp"

using namespace brls::literals;

namespace cloud {

namespace {

struct CatalogFetchResult {
    Snapshot snapshot;
    bool ok = false;
};

struct ProvisionBridge {
    Service::ProgressCallback onProgress;
    Service::CancelCallback isCancelled;
};

std::map<std::string, bool> loadStreamabilityCache(const std::string& path)
{
    std::map<std::string, bool> values;
    json_object* root = json_object_from_file(path.c_str());
    if (!root || !json_object_is_type(root, json_type_object))
    {
        if (root)
            json_object_put(root);
        return values;
    }

    json_object_object_foreach(root, productId, value)
    {
        if (value && json_object_is_type(value, json_type_boolean))
            values[productId] = json_object_get_boolean(value);
    }
    json_object_put(root);
    return values;
}

void saveStreamabilityCache(const std::string& path, const std::map<std::string, bool>& values)
{
    json_object* root = json_object_new_object();
    for (const auto& [productId, streamable] : values)
        json_object_object_add(root, productId.c_str(), json_object_new_boolean(streamable));

    const std::string temp = path + ".tmp";
    if (json_object_to_file_ext(temp.c_str(), root, JSON_C_TO_STRING_PRETTY) == 0)
        rename(temp.c_str(), path.c_str());
    else
        unlink(temp.c_str());
    json_object_put(root);
}

void applyStreamability(Catalog& catalog, const std::map<std::string, bool>& values)
{
    for (Game& game : catalog.games)
    {
        if (game.serviceType != "pscloud")
            continue;
        auto it = values.find(game.productId);
        if (it != values.end())
            game.streamabilityStatus = it->second
                ? StreamabilityStatus::Streamable
                : StreamabilityStatus::NotStreamable;
    }
}

static void provisionProgress(const char* stage, void* user)
{
    auto* bridge = static_cast<ProvisionBridge*>(user);
    if (!bridge || !bridge->onProgress)
        return;
    bridge->onProgress(stage ? stage : "");
}

static bool provisionCancelled(void* user)
{
    auto* bridge = static_cast<ProvisionBridge*>(user);
    return bridge && bridge->isCancelled && bridge->isCancelled();
}

ChiakiServiceType chiakiServiceFor(const std::string& value)
{
    if (value == "pscloud")
        return CHIAKI_SERVICE_TYPE_PSCLOUD;
    if (value == "psnow")
        return CHIAKI_SERVICE_TYPE_PSNOW;
    return CHIAKI_SERVICE_TYPE_REMOTE_PLAY;
}

ChiakiTarget targetFor(const Game& game)
{
    if (game.platform == "ps5" || game.streamServiceType == "pscloud")
        return CHIAKI_TARGET_PS5_1;
    return CHIAKI_TARGET_PS4_10;
}

std::string privacyElementLabel(const std::string& el)
{
    if (el == "REAL_NAME") return "akira/cloud/priv_real_name"_i18n;
    if (el == "PRIVACY_SETTING_TRUENAME") return "akira/cloud/priv_truename"_i18n;
    if (el == "PRIVACY_SETTING_ACTIVITYSTREAM") return "akira/cloud/priv_activity"_i18n;
    if (el == "PRIVACY_SETTING_FRIENDSLIST") return "akira/cloud/priv_friends"_i18n;
    if (el == "PRIVACY_SETTING_SEARCH") return "akira/cloud/priv_search"_i18n;
    if (el == "PRIVACY_SETTING_RECOMMENDUSERS") return "akira/cloud/priv_recommend"_i18n;
    if (el == "PRIVACY_SETTING_BROADCAST") return "akira/cloud/priv_broadcast"_i18n;
    return el;
}

std::string privacyLaunchMessage(const std::string& sentinel)
{
    std::string intro = "akira/cloud/launch_privacy"_i18n;
    std::string action = "akira/cloud/launch_privacy_action"_i18n;

    auto pos = sentinel.find("missing_elements=");
    if (pos == std::string::npos)
        return intro + "\n\n" + action;
    pos += 17;
    auto end = sentinel.find('&', pos);
    std::string enc = sentinel.substr(pos, end == std::string::npos ? std::string::npos : end - pos);

    std::string norm;
    for (size_t i = 0; i < enc.size();)
    {
        if (i + 3 <= enc.size() && (enc.compare(i, 3, "%2C") == 0 || enc.compare(i, 3, "%2c") == 0))
        {
            norm += ',';
            i += 3;
        }
        else
        {
            norm += enc[i++];
        }
    }

    std::string list;
    size_t start = 0;
    while (start <= norm.size())
    {
        auto comma = norm.find(',', start);
        std::string el = norm.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!el.empty())
        {
            if (!list.empty())
                list += "\n";
            list += "\xE2\x80\xA2 " + privacyElementLabel(el);
        }
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }

    if (list.empty())
        return intro + "\n\n" + action;
    return intro + "\n\n" + list + "\n\n" + action;
}

std::string launchErrorText(LaunchFailureKind kind, const std::string& raw)
{
    switch (kind)
    {
        case LaunchFailureKind::AuthorizationFailed:
            return "akira/cloud/launch_not_enabled"_i18n;
        case LaunchFailureKind::PsPlusRequired:
            return "akira/cloud/launch_ps_plus"_i18n;
        case LaunchFailureKind::GameNotStreamable:
            return "akira/cloud/launch_unavailable"_i18n;
        case LaunchFailureKind::PrivacySettings:
            return privacyLaunchMessage(raw);
        case LaunchFailureKind::NetworkError:
            return "akira/cloud/launch_network"_i18n;
        case LaunchFailureKind::PingTimeout:
            return "akira/cloud/launch_busy"_i18n;
        case LaunchFailureKind::DatacenterUnavailable:
            return raw;
        case LaunchFailureKind::Other:
            return raw.empty() ? "akira/cloud/launch_failed"_i18n : raw;
        case LaunchFailureKind::None:
        default:
            return "akira/cloud/launch_failed"_i18n;
    }
}

Status statusForCatalog(const Catalog& catalog, PlusMembership plusMembership)
{
    Status status;
    status.gameCount = catalog.launchableCount();
    status.plusMembership = plusMembership;

    WarningKind warningKind = classifyWarning(catalog.warning);
    if (warningKind == WarningKind::SessionExpired)
    {
        status.availability = Availability::Warning;
        status.title = "akira/cloud/status_relink_title"_i18n;
        status.detail = "akira/cloud/status_relink_detail"_i18n;
        status.canBrowse = true;
        status.canPair = true;
        status.degraded = true;
        return status;
    }

    if (warningKind == WarningKind::Other)
    {
        status.availability = Availability::Warning;
        status.title = "akira/cloud/status_warning_title"_i18n;
        status.detail = catalog.warning;
        status.canBrowse = true;
        status.degraded = true;
        return status;
    }

    if (status.gameCount == 0)
    {
        status.availability = Availability::Empty;
        status.title = "akira/cloud/status_empty_title"_i18n;
        status.detail = "akira/cloud/status_empty_detail"_i18n;
        status.canBrowse = true;
        return status;
    }

    if (plusMembership == PlusMembership::None)
    {
        status.availability = Availability::SubscriptionRequired;
        status.title = "akira/cloud/status_subscription_title"_i18n;
        status.detail = "akira/cloud/status_subscription_detail"_i18n;
        status.canBrowse = true;
        status.degraded = true;
        return status;
    }

    if (!catalog.nativeMode)
    {
        status.availability = Availability::Warning;
        status.title = "akira/cloud/status_fallback_title"_i18n;
        status.detail = "akira/cloud/status_fallback_detail"_i18n;
        status.canBrowse = true;
        status.degraded = true;
        return status;
    }

    // CloudPad deliberately does not infer Premium from the PSN profile's
    // generic isPlus bit. Sony validates access for the selected title during
    // Kamaji/Gaikai provisioning, so a loaded catalog is only a catalog.
    status.availability = Availability::CatalogAvailable;
    status.title = brls::getStr("akira/cloud/status_catalog_title", status.gameCount);
    status.detail = "akira/cloud/status_catalog_detail"_i18n;
    status.canBrowse = true;
    return status;
}

CatalogFetchResult fetchCatalogBlocking(SettingsManager* settings, const Profile& profile,
    const std::string& locale, const std::string& cacheDir, bool force,
    PlusMembership plusMembership)
{
    CatalogFetchResult result;

    ChiakiCloudCatalogConfig cfg = {};
    cfg.npsso = profile.npsso.empty() ? nullptr : profile.npsso.c_str();
    cfg.locale = locale.c_str();
    cfg.cache_dir = cacheDir.c_str();
    cfg.force_refresh = force;

    ChiakiCloudCatalogResult raw = {};
    ChiakiErrorCode err = chiaki_cloudcatalog_fetch_unified(&cfg, &raw, settings->getLogger());
    (void)err;

    if (raw.json && parseCatalog(raw.json, result.snapshot.catalog))
    {
        const Catalog& catalog = result.snapshot.catalog;
        result.snapshot.status = statusForCatalog(result.snapshot.catalog, plusMembership);
        result.ok = true;

        /*
         * Whether there is a catalog, not whether it came from a storefront.
         *
         * nativeMode is not a health signal. It records whether PSN's storefront
         * answered for this account's region, and downstream it selects exactly
         * one step of the merge: the streamability gate, applied in native mode
         * only. A region it does not cover takes the public APOLLOROOT fallback
         * walk, which the library documents as graceful degradation rather than
         * failure. Reading it as "do we have a catalog" made such a region look
         * like an account with no cloud titles while the fallback sat on eight
         * hundred of them.
         */
        result.snapshot.hasCatalog = !catalog.games.empty();
    }
    else
    {
        result.snapshot.hasCatalog = false;
        result.snapshot.status.availability = Availability::Error;
        result.snapshot.status.title = "akira/cloud/status_error_title"_i18n;
        result.snapshot.status.detail = raw.error_message && raw.error_message[0]
            ? raw.error_message
            : "akira/cloud/status_error_detail"_i18n;
    }

    chiaki_cloudcatalog_result_fini(&raw);
    return result;
}

} // namespace

Service& Service::instance()
{
    static Service instance;
    return instance;
}

Service::Service()
{
    settings = SettingsManager::getInstance();
}

Snapshot Service::defaultSnapshotForProfile(bool hasProfile, bool paired) const
{
    Snapshot snapshot;
    if (!hasProfile)
    {
        snapshot.status.availability = Availability::NoProfile;
        snapshot.status.title = "akira/cloud/status_no_profile_title"_i18n;
        snapshot.status.detail = "akira/cloud/status_no_profile_detail"_i18n;
        return snapshot;
    }

    if (!paired)
    {
        snapshot.status.availability = Availability::NeedsPairing;
        snapshot.status.title = "akira/cloud/status_pair_title"_i18n;
        snapshot.status.detail = "akira/cloud/status_pair_detail"_i18n;
        snapshot.status.canPair = true;
        return snapshot;
    }

    snapshot.status.availability = Availability::Checking;
    snapshot.status.title = "akira/cloud/status_checking_title"_i18n;
    snapshot.status.detail = "akira/cloud/status_checking_detail"_i18n;
    return snapshot;
}

Snapshot Service::defaultSnapshotForActiveProfile() const
{
    const Profile* profile = settings->getActiveProfile();
    if (!profile)
        return defaultSnapshotForProfile(false, false);
    return defaultSnapshotForProfile(true, !profile->npsso.empty());
}

Snapshot Service::snapshotForActiveProfile() const
{
    const Profile* profile = settings->getActiveProfile();
    if (!profile)
        return defaultSnapshotForProfile(false, false);

    std::lock_guard<std::mutex> lock(mutex);
    auto it = entries.find(profile->id);
    if (it == entries.end())
        return defaultSnapshotForProfile(true, !profile->npsso.empty());

    return it->second.snapshot;
}

void Service::markActiveProfileDirty()
{
    const Profile* profile = settings->getActiveProfile();
    if (!profile)
        return;

    std::lock_guard<std::mutex> lock(mutex);
    auto it = entries.find(profile->id);
    if (it != entries.end() && !it->second.refreshing)
        entries.erase(it);
}

std::string Service::consoleLocale() const
{
    std::string locale = settings->getDebugLocale();
    if (locale.empty())
        locale = brls::Application::getLocale();
    if (locale.empty())
        locale = "en-US";
    return locale;
}

/*
 * The locale the library settled on last time, which is not the one we asked
 * for: it re-bases the request on the account's own country and returns the
 * result. Handing that back is how an account in one region and a console set
 * to another stop arguing every fetch.
 */
std::string Service::catalogLocale() const
{
    std::string stored = settings->getCloudStoreLocale();
    return stored.empty() ? consoleLocale() : stored;
}

std::string Service::streamLanguage() const
{
    std::string chosen = settings->getCloudGameLanguage();
    return chosen.empty() ? catalogLocale() : chosen;
}

/*
 * A settled locale outlives the console locale that produced it, so a system
 * language change would otherwise be invisible here - the cache is keyed by
 * filename alone and would serve the old region for the rest of its day.
 */
std::string Service::reconcileCatalogLocale(int64_t profileId) const
{
    const std::string source = consoleLocale();
    const std::string recorded = settings->getCloudStoreLocaleSource();
    if (recorded == source)
        return catalogLocale();

    /*
     * Nothing recorded means a config written before this was tracked, and
     * those fetches were made with the console locale - the one we are holding.
     * So the cache matches; only the note about it is missing.
     */
    if (recorded.empty())
    {
        settings->setCloudStoreLocaleSource(source);
        settings->writeFile();
        return catalogLocale();
    }

    brls::Logger::info("Cloud: console locale is now {}, dropping the cached catalog", source);
    chiaki_cloudcatalog_invalidate_cache(cacheDirForProfile(profileId).c_str());
    settings->setCloudStoreLocaleSource(source);
    settings->setCloudStoreLocale(source);
    settings->writeFile();
    return source;
}

void Service::noteSettledLocale(const std::string& settled) const
{
    if (settled.empty() || settled == settings->getCloudStoreLocale())
        return;
    settings->setCloudStoreLocale(settled);
    if (settings->getCloudStoreLocaleSource().empty())
        settings->setCloudStoreLocaleSource(consoleLocale());
    settings->writeFile();
}

void Service::clearCatalogCache()
{
    const Profile* profile = settings->getActiveProfile();
    if (!profile)
        return;

    const int64_t profileId = profile->id;
    chiaki_cloudcatalog_invalidate_cache(cacheDirForProfile(profileId).c_str());
    unlink(streamabilityCachePath(profileId).c_str());

    std::lock_guard<std::mutex> lock(mutex);
    auto it = entries.find(profileId);
    if (it != entries.end() && !it->second.refreshing)
        entries.erase(it);
}

std::string Service::cacheRoot() const
{
    return "sdmc:/switch/akira/cache/cloud";
}

std::string Service::cacheDirForProfile(int64_t profileId) const
{
    return cacheRoot() + "/profile-" + std::to_string(profileId);
}

std::string Service::streamabilityCachePath(int64_t profileId) const
{
    return cacheDirForProfile(profileId) + "/streamability.json";
}

void Service::ensureCacheDirsForProfile(int64_t profileId) const
{
    mkdir("sdmc:/switch/akira/cache", 0755);
    mkdir(cacheRoot().c_str(), 0755);
    std::string profileDir = cacheDirForProfile(profileId);
    mkdir(profileDir.c_str(), 0755);
}

void Service::storeSnapshot(int64_t profileId, const Snapshot& snapshot)
{
    std::lock_guard<std::mutex> lock(mutex);
    entries[profileId].snapshot = snapshot;
    entries[profileId].refreshing = false;
}

void Service::storeLaunchOutcome(int64_t profileId, const std::string& productId, bool streamable)
{
    std::map<std::string, bool> cached = loadStreamabilityCache(streamabilityCachePath(profileId));
    std::map<std::string, bool> updated;

    {
        std::lock_guard<std::mutex> lock(mutex);
        Entry& entry = entries[profileId];
        if (!entry.streamabilityLoaded)
        {
            entry.streamability = std::move(cached);
            entry.streamabilityLoaded = true;
        }
        entry.streamability[productId] = streamable;
        applyStreamability(entry.snapshot.catalog, entry.streamability);
        updated = entry.streamability;
    }

    saveStreamabilityCache(streamabilityCachePath(profileId), updated);
}

void Service::refreshActiveProfile(bool force, SnapshotCallback onDone)
{
    const Profile* profile = settings->getActiveProfile();
    if (!profile)
    {
        Snapshot snapshot = defaultSnapshotForProfile(false, false);
        if (onDone)
            brls::sync([onDone, snapshot]() { onDone(snapshot); });
        return;
    }

    if (profile->npsso.empty())
    {
        Snapshot snapshot = defaultSnapshotForProfile(true, false);
        storeSnapshot(profile->id, snapshot);
        if (onDone)
            brls::sync([onDone, snapshot]() { onDone(snapshot); });
        return;
    }

    const int64_t profileId = profile->id;
    const std::string cacheDir = cacheDirForProfile(profileId);
    const std::string npsso = profile->npsso;
    const std::string locale = reconcileCatalogLocale(profileId);

    {
        std::lock_guard<std::mutex> lock(mutex);
        Entry& entry = entries[profileId];
        if (entry.refreshing && !force)
        {
            if (onDone)
                entry.pending.push_back(std::move(onDone));
            return;
        }

        entry.refreshing = true;
        entry.generation++;
        if (!entry.snapshot.hasCatalog)
            entry.snapshot = defaultSnapshotForProfile(true, true);
        if (onDone)
            entry.pending.push_back(std::move(onDone));
    }

    auto fetchCatalog = [this, profileId, locale, cacheDir, force, npsso](std::optional<bool> hasPlus) {
        PlusMembership plusMembership = PlusMembership::Unknown;
        {
            std::lock_guard<std::mutex> lock(mutex);
            Entry& entry = entries[profileId];
            if (hasPlus.has_value())
                entry.plusMembership = observePlusMembership(*hasPlus);
            plusMembership = entry.plusMembership;
        }

        ensureCacheDirsForProfile(profileId);

        brls::async([this, profileId, locale, cacheDir, force, npsso, plusMembership]() {
            Profile copy;
            copy.id = profileId;
            copy.npsso = npsso;

            CatalogFetchResult fetched = fetchCatalogBlocking(
                settings, copy, locale, cacheDir, force, plusMembership);
            std::map<std::string, bool> streamability =
                loadStreamabilityCache(streamabilityCachePath(profileId));
            applyStreamability(fetched.snapshot.catalog, streamability);

            std::vector<SnapshotCallback> callbacks;
            {
                std::lock_guard<std::mutex> lock(mutex);
                Entry& entry = entries[profileId];
                entry.snapshot = fetched.snapshot;
                entry.streamability = std::move(streamability);
                entry.streamabilityLoaded = true;
                entry.refreshing = false;
                callbacks.swap(entry.pending);
            }

            Snapshot snapshot = fetched.snapshot;
            brls::sync([this, callbacks, snapshot]() {
                noteSettledLocale(snapshot.catalog.settledLocale);
                for (const SnapshotCallback& cb : callbacks)
                    if (cb)
                        cb(snapshot);
            });
        });
    };

    TrophyManager::getInstance()->fetchProfile(false,
        [fetchCatalog](const psn::PsnProfile& psnProfile) {
            fetchCatalog(psnProfile.isPlus);
        },
        [fetchCatalog](psn::Status, const std::string&) {
            fetchCatalog(std::nullopt);
        });
}

void Service::launchGame(const Game& game, HostCallback onSuccess, ErrorCallback onError,
    ProgressCallback onProgress, bool forceSkipAttrCheck, CancelCallback isCancelled)
{
    const Profile* profile = settings->getActiveProfile();
    if (!profile || profile->npsso.empty())
    {
        if (onError)
            onError("akira/cloud/status_pair_detail"_i18n);
        return;
    }

    const bool skipAttrCheck = forceSkipAttrCheck;
    const int64_t profileId = profile->id;
    const std::string npsso = profile->npsso;
    const std::string locale = catalogLocale();
    const std::string gameLanguage = streamLanguage();
    const std::string cacheDir = cacheDirForProfile(profileId);

    bool noPlus = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = entries.find(profileId);
        noPlus = it != entries.end() && it->second.plusMembership == PlusMembership::None;
    }
    if (noPlus)
    {
        if (onError)
            onError("akira/cloud/status_subscription_detail"_i18n);
        return;
    }

    brls::async([this, game, profileId, npsso, locale, gameLanguage, cacheDir, skipAttrCheck,
                 onSuccess, onError, onProgress, isCancelled]() {
        if (isCancelled && isCancelled())
            return;

        Profile profileCopy;
        profileCopy.id = profileId;
        profileCopy.npsso = npsso;

        PlusMembership plusMembership = PlusMembership::Unknown;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = entries.find(profileId);
            if (it != entries.end())
                plusMembership = it->second.plusMembership;
        }
        CatalogFetchResult catalogResult = fetchCatalogBlocking(
            settings, profileCopy, locale, cacheDir, false, plusMembership);
        if (!catalogResult.ok || !catalogResult.snapshot.hasCatalog)
        {
            std::string message = catalogResult.snapshot.status.detail.empty()
                ? "akira/cloud/status_error_detail"_i18n
                : catalogResult.snapshot.status.detail;
            if (onError)
                brls::sync([onError, message]() { onError(message); });
            return;
        }

        if (isCancelled && isCancelled())
            return;

        ProvisionBridge bridge{onProgress, isCancelled};
        const bool pscloud = game.streamServiceType == "pscloud";
        const std::string forcedDatacenter = settings->getCloudDatacenter(pscloud);
        const std::string priorDatacenters = serializeDatacenters(settings->getCloudDatacenters(pscloud));

        ChiakiCloudProvisionConfig cfg = {};
        cfg.service_type = game.streamServiceType.c_str();
        cfg.game_identifier = game.streamIdentifier.c_str();
        cfg.game_name = game.name.c_str();
        cfg.npsso = npsso.c_str();
        cfg.store_country = catalogResult.snapshot.catalog.fallbackRegion.c_str();
        cfg.store_lang = catalogResult.snapshot.catalog.resolvedStoreLang.c_str();
        cfg.owned_entitlement_id = game.entitlementId.c_str();
        cfg.owned_platform = game.platform.c_str();
        cfg.catalog_is_foreign = catalogResult.snapshot.catalog.foreignAccountCatalog();
        cfg.skip_account_attr_check = skipAttrCheck;
        cfg.forced_datacenter = forcedDatacenter.c_str();
        cfg.prior_datacenters_json = priorDatacenters.c_str();
        cfg.game_language = gameLanguage.c_str();
        cfg.resolution = settings->getCloudVideoResolution(pscloud);
        cfg.bitrate_kbps = settings->getCloudVideoBitrate(pscloud);
        cfg.progress = provisionProgress;
        cfg.is_cancelled = provisionCancelled;
        cfg.user = &bridge;

        ChiakiCloudProvisionResult result = {};
        ChiakiErrorCode err = chiaki_cloud_provision_session(&cfg, &result, settings->getLogger());
        (void)err;

        if (isCancelled && isCancelled())
        {
            chiaki_cloud_provision_result_fini(&result);
            return;
        }

        brls::Logger::info("CloudLaunch: provision returned err={} server={}:{} spec={} pings={}",
            static_cast<int>(result.err),
            result.server_ip ? result.server_ip : "(null)",
            result.server_port,
            result.launch_spec ? "yes" : "no",
            result.datacenter_pings ? "yes" : "no");

        if (result.datacenter_pings)
        {
            std::vector<Datacenter> pings = parseDatacenters(result.datacenter_pings);
            brls::sync([this, pscloud, pings]() {
                settings->setCloudDatacenters(pscloud, pings);
                settings->writeFile();
            });
        }

        if (result.err != CHIAKI_ERR_SUCCESS)
        {
            std::string raw = result.error_message ? result.error_message : "";
            LaunchFailureKind kind = classifyLaunchFailure(raw);
            std::string message = launchErrorText(kind, raw);
            if (game.serviceType == "pscloud")
                storeLaunchOutcome(profileId, game.productId, false);
            chiaki_cloud_provision_result_fini(&result);
            if (onError)
                brls::sync([onError, message]() { onError(message); });
            return;
        }

        auto host = std::make_shared<Host>(game.name);
        host->setHostAddr(result.server_ip);
        host->setState(CHIAKI_DISCOVERY_HOST_STATE_READY);
        host->setChiakiTarget(targetFor(game));
        Host::CloudSessionConfig cloudCfg;
        cloudCfg.serviceType = chiakiServiceFor(game.streamServiceType);
        cloudCfg.host = result.server_ip;
        cloudCfg.port = static_cast<uint16_t>(result.server_port);
        cloudCfg.launchSpec = result.launch_spec ? result.launch_spec : "";
        cloudCfg.handshakeKey = result.handshake_key ? result.handshake_key : "";
        cloudCfg.sessionId = result.session_id ? result.session_id : "";
        cloudCfg.psnWrapperType = result.psn_wrapper_type;
        cloudCfg.mtuIn = result.mtu_in;
        cloudCfg.mtuOut = result.mtu_out;
        cloudCfg.rttUs = result.rtt_us;
        cloudCfg.entitlementId = result.entitlement_id;
        cloudCfg.platform = result.platform;
        host->setCloudSessionConfig(cloudCfg);

        if (game.serviceType == "pscloud")
            storeLaunchOutcome(profileId, game.productId, true);

        chiaki_cloud_provision_result_fini(&result);

        brls::Logger::info("CloudLaunch: provision success, dispatching onSuccess");
        if (onSuccess)
            brls::sync([onSuccess, host]() { onSuccess(host); });
    });
}

} // namespace cloud
