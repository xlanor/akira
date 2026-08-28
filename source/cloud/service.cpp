#include "cloud/service.hpp"

#include <borealis.hpp>
#include <borealis/core/i18n.hpp>

#include <chiaki/cloudcatalog.h>
#include <chiaki/cloudsession.h>

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#include "core/host.hpp"
#include "core/settings_manager.hpp"

using namespace brls::literals;

namespace cloud {

namespace {

struct CatalogFetchResult {
    Snapshot snapshot;
    bool ok = false;
};

struct ProvisionBridge {
    Service::ProgressCallback onProgress;
};

static void provisionProgress(const char* stage, void* user)
{
    auto* bridge = static_cast<ProvisionBridge*>(user);
    if (!bridge || !bridge->onProgress)
        return;
    bridge->onProgress(stage ? stage : "");
}

static bool provisionCancelled(void*)
{
    return false;
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

Status statusForCatalog(const Catalog& catalog)
{
    Status status;
    status.gameCount = catalog.launchableCount();

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

    if (status.gameCount > 0)
    {
        status.availability = Availability::Ready;
        status.title = brls::getStr("akira/cloud/status_ready_title", status.gameCount);
        status.detail = "akira/cloud/status_ready_detail"_i18n;
        status.canBrowse = true;
        return status;
    }

    status.availability = Availability::Empty;
    status.title = "akira/cloud/status_empty_title"_i18n;
    status.detail = "akira/cloud/status_empty_detail"_i18n;
    status.canBrowse = true;
    return status;
}

CatalogFetchResult fetchCatalogBlocking(SettingsManager* settings, const Profile& profile,
    const std::string& locale, const std::string& cacheDir, bool force)
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
        result.snapshot.status = statusForCatalog(catalog);
        result.ok = true;

        bool expired = classifyWarning(catalog.warning) == WarningKind::SessionExpired;
        result.snapshot.hasCatalog = catalog.nativeMode || !catalog.games.empty();
        if (!catalog.nativeMode && !expired && !catalog.games.empty())
        {
            result.snapshot.status = Status{};
            result.snapshot.status.availability = Availability::Warning;
            result.snapshot.status.title = "akira/cloud/status_foreign_title"_i18n;
            result.snapshot.status.detail = "akira/cloud/status_foreign_detail"_i18n;
            result.snapshot.status.canBrowse = true;
            result.snapshot.status.degraded = true;
            result.snapshot.status.gameCount = catalog.launchableCount();
        }
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

std::string Service::selectedLocale() const
{
    std::string locale = settings->getDebugLocale();
    if (locale.empty())
    {
        const Profile* profile = settings->getActiveProfile();
        if (profile)
            locale = profile->cloudStoreLocale;
    }
    if (locale.empty())
        locale = brls::Application::getLocale();
    if (locale.empty())
        locale = "en-US";
    return locale;
}

std::string Service::cacheRoot() const
{
    return "sdmc:/switch/akira/cache/cloud";
}

std::string Service::cacheDirForProfile(int64_t profileId) const
{
    return cacheRoot() + "/profile-" + std::to_string(profileId);
}

void Service::ensureCacheDirsForProfile(int64_t profileId) const
{
    mkdir("sdmc:/switch/akira/cache", 0755);
    mkdir(cacheRoot().c_str(), 0755);
    std::string profileDir = cacheDirForProfile(profileId);
    mkdir(profileDir.c_str(), 0755);
}

void Service::clearCacheForActiveProfile()
{
    const Profile* profile = settings->getActiveProfile();
    if (!profile)
        return;

    const int64_t profileId = profile->id;
    const std::string dir = cacheDirForProfile(profileId);

    int removed = 0;
    if (DIR* handle = opendir(dir.c_str()))
    {
        while (struct dirent* entry = readdir(handle))
        {
            if (entry->d_name[0] == '.')
                continue;
            std::string path = dir + "/" + entry->d_name;
            if (std::remove(path.c_str()) == 0)
                removed++;
        }
        closedir(handle);
    }

    settings->clearCloudStoreResolution(profileId);
    settings->writeFile();

    brls::Logger::info("CloudService: cleared {} cache file(s) and store resolution for profile {}",
        removed, profileId);

    std::lock_guard<std::mutex> lock(mutex);
    Entry& entry = entries[profileId];
    entry.snapshot = defaultSnapshotForProfile(true, !profile->npsso.empty());
    entry.generation++;
}

void Service::storeSnapshot(int64_t profileId, const Snapshot& snapshot)
{
    std::lock_guard<std::mutex> lock(mutex);
    entries[profileId].snapshot = snapshot;
    entries[profileId].refreshing = false;
}

void Service::storeLaunchError(int64_t profileId, const std::string& errorMessage)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto& entry = entries[profileId];
    entry.snapshot.status.availability = Availability::LaunchBlocked;
    entry.snapshot.status.title = "akira/cloud/status_blocked_title"_i18n;
    entry.snapshot.status.detail = errorMessage;
    entry.snapshot.status.canBrowse = true;
    entry.snapshot.status.canPair = true;
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
    const std::string locale = selectedLocale();
    const std::string cacheDir = cacheDirForProfile(profileId);
    const std::string npsso = profile->npsso;

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

    ensureCacheDirsForProfile(profileId);

    brls::async([this, profileId, locale, cacheDir, force, npsso]() {
        Profile copy;
        copy.id = profileId;
        copy.npsso = npsso;

        CatalogFetchResult fetched = fetchCatalogBlocking(settings, copy, locale, cacheDir, force);

        std::vector<SnapshotCallback> callbacks;
        {
            std::lock_guard<std::mutex> lock(mutex);
            Entry& entry = entries[profileId];
            entry.snapshot = fetched.snapshot;
            entry.refreshing = false;
            callbacks.swap(entry.pending);
        }

        if (fetched.ok)
        {
            const Catalog& catalog = fetched.snapshot.catalog;
            std::string settled = catalog.settledLocale;
            std::string country = catalog.fallbackRegion;
            std::string lang = catalog.resolvedStoreLang;
            brls::sync([this, profileId, settled, country, lang]() {
                if (settings->noteCloudStoreResolution(profileId, settled, country, lang))
                {
                    brls::Logger::info("CloudService: store resolution stored (locale='{}' country='{}' lang='{}')",
                        settled, country, lang);
                    settings->writeFile();
                }
            });
        }

        Snapshot snapshot = fetched.snapshot;
        brls::sync([callbacks, snapshot]() {
            for (const SnapshotCallback& cb : callbacks)
                if (cb)
                    cb(snapshot);
        });
    });
}

void Service::launchGame(const Game& game, HostCallback onSuccess, ErrorCallback onError,
    ProgressCallback onProgress, bool forceSkipAttrCheck)
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
    const std::string locale = selectedLocale();
    const std::string cacheDir = cacheDirForProfile(profileId);
    const std::string storedStoreCountry = profile->cloudResolvedStoreCountry;
    const std::string storedStoreLang = profile->cloudResolvedStoreLang;

    brls::async([this, game, profileId, npsso, locale, cacheDir, storedStoreCountry, storedStoreLang,
                    skipAttrCheck, onSuccess, onError, onProgress]() {
        Profile profileCopy;
        profileCopy.id = profileId;
        profileCopy.npsso = npsso;

        CatalogFetchResult catalogResult = fetchCatalogBlocking(settings, profileCopy, locale, cacheDir, false);
        if (!catalogResult.ok || !catalogResult.snapshot.hasCatalog)
        {
            std::string message = catalogResult.snapshot.status.detail.empty()
                ? "akira/cloud/status_error_detail"_i18n
                : catalogResult.snapshot.status.detail;
            if (onError)
                brls::sync([onError, message]() { onError(message); });
            return;
        }

        ProvisionBridge bridge{onProgress};
        const bool pscloud = game.streamServiceType == "pscloud";
        const std::string forcedDatacenter = settings->getCloudDatacenter(pscloud);
        const std::string priorDatacenters = serializeDatacenters(settings->getCloudDatacenters(pscloud));

        ChiakiCloudProvisionConfig cfg = {};
        cfg.service_type = game.streamServiceType.c_str();
        cfg.game_identifier = game.streamIdentifier.c_str();
        cfg.game_name = game.name.c_str();
        cfg.npsso = npsso.c_str();
        const std::string storeCountry = storedStoreCountry.empty()
            ? catalogResult.snapshot.catalog.fallbackRegion
            : storedStoreCountry;
        const std::string storeLang = storedStoreLang.empty()
            ? catalogResult.snapshot.catalog.resolvedStoreLang
            : storedStoreLang;

        brls::Logger::info("CloudLaunch: resolving in store {}/{} (catalog native={})",
            storeCountry.empty() ? "US" : storeCountry.c_str(),
            storeLang.empty() ? "en" : storeLang.c_str(),
            catalogResult.snapshot.catalog.nativeMode);

        cfg.store_country = storeCountry.c_str();
        cfg.store_lang = storeLang.c_str();
        cfg.owned_entitlement_id = game.entitlementId.c_str();
        cfg.owned_platform = game.platform.c_str();
        cfg.catalog_is_foreign = catalogResult.snapshot.catalog.foreignAccountCatalog();
        cfg.skip_account_attr_check = skipAttrCheck;
        cfg.forced_datacenter = forcedDatacenter.c_str();
        cfg.prior_datacenters_json = priorDatacenters.c_str();
        cfg.game_language = locale.c_str();
        cfg.resolution = settings->getCloudVideoResolution();
        cfg.bitrate_kbps = settings->getCloudVideoBitrate();
        cfg.progress = provisionProgress;
        cfg.is_cancelled = provisionCancelled;
        cfg.user = &bridge;

        ChiakiCloudProvisionResult result = {};
        ChiakiErrorCode err = chiaki_cloud_provision_session(&cfg, &result, settings->getLogger());
        (void)err;

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
            std::string message = launchErrorText(classifyLaunchFailure(raw), raw);
            storeLaunchError(profileId, message);
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

        {
            std::lock_guard<std::mutex> lock(mutex);
            auto& entry = entries[profileId];
            entry.snapshot.status = statusForCatalog(catalogResult.snapshot.catalog);
            entry.snapshot.catalog = catalogResult.snapshot.catalog;
            entry.snapshot.hasCatalog = true;
        }

        chiaki_cloud_provision_result_fini(&result);

        brls::Logger::info("CloudLaunch: provision success, dispatching onSuccess");
        if (onSuccess)
            brls::sync([onSuccess, host]() { onSuccess(host); });
    });
}

} // namespace cloud
