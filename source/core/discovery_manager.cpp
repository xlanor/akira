#include "core/discovery_manager.hpp"
#include "core/settings_manager.hpp"

#include <borealis.hpp>
#include <ctime>
#include <format>

#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <switch.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

#include <json-c/json.h>

#include "util/http.hpp"
#include "util/http_pool.hpp"

static void discovery_log_cb(ChiakiLogLevel level, const char* msg, void* user)
{
    if (!SettingsManager::getInstance()->getDebugDiscoveryLog())
        return;

    ChiakiLog* mainLog = static_cast<ChiakiLog*>(user);
    if (mainLog)
        chiaki_log(mainLog, level, "%s", msg);
}

#define PING_MS 500
#define HOSTS_MAX 16
#define DROP_PINGS 3

static void DiscoveryServiceCallback(ChiakiDiscoveryHost* discovered_hosts, size_t hosts_count, void* user)
{
    DiscoveryManager* dm = static_cast<DiscoveryManager*>(user);
    for (size_t i = 0; i < hosts_count; i++)
    {
        dm->discoveryCallback(&discovered_hosts[i]);
    }
}

DiscoveryManager* DiscoveryManager::getInstance()
{
    static DiscoveryManager* instance = new DiscoveryManager();
    return instance;
}

DiscoveryManager::DiscoveryManager()
{
    settings = SettingsManager::getInstance();
    log = settings->getLogger();

    if (log)
    {
        brls::Logger::info("DiscoveryManager created with logger");
    }
    else
    {
        brls::Logger::warning("DiscoveryManager created WITHOUT logger!");
    }

    memset(&service, 0, sizeof(service));
    memset(&discovery, 0, sizeof(discovery));
}

DiscoveryManager::~DiscoveryManager()
{
    if (serviceEnabled)
    {
        setServiceEnabled(false);
    }

    if (hostAddr)
    {
        free(hostAddr);
        hostAddr = nullptr;
    }
}

void DiscoveryManager::setServiceEnabled(bool enable)
{
    brls::Logger::info("DiscoveryManager::setServiceEnabled({})", enable);

    if (serviceEnabled == enable)
    {
        brls::Logger::info("Discovery service already in requested state");
        return;
    }

    serviceEnabled = enable;

    if (enable)
    {
        NetworkAddresses addresses = getIPv4BroadcastAddr();
        brls::Logger::info("Broadcast addr: {:08x}, local: {:08x}", addresses.broadcast, addresses.local);

        ChiakiDiscoveryServiceOptions options;
        memset(&options, 0, sizeof(options));
        options.ping_ms = PING_MS;
        options.ping_initial_ms = PING_MS;
        options.hosts_max = HOSTS_MAX;
        options.host_drop_pings = DROP_PINGS;
        options.cb = DiscoveryServiceCallback;
        options.cb_user = this;

        std::vector<struct sockaddr_storage> targets;

        struct sockaddr_in addr_broadcast = {};
        addr_broadcast.sin_family = AF_INET;
        addr_broadcast.sin_addr.s_addr = addresses.broadcast;
        struct sockaddr_storage broadcastStore = {};
        memcpy(&broadcastStore, &addr_broadcast, sizeof(addr_broadcast));
        targets.push_back(broadcastStore);

        auto* hostsForDiscovery = settings->getHostsMap();
        if (hostsForDiscovery) {
            for (auto& entry : *hostsForDiscovery) {
                Host* h = entry.second.get();
                if (!h || !h->isManual())
                    continue;
                std::string addr = h->getHostAddr();
                if (addr.empty())
                    continue;
                struct sockaddr_in unicast = {};
                unicast.sin_family = AF_INET;
                if (inet_pton(AF_INET, addr.c_str(), &unicast.sin_addr) == 1) {
                    struct sockaddr_storage unicastStore = {};
                    memcpy(&unicastStore, &unicast, sizeof(unicast));
                    targets.push_back(unicastStore);
                    brls::Logger::info("Discovery: added manual unicast target {}", addr);
                }
            }
        }

        std::string subnetsCfg = settings->getDiscoverySubnets();
        size_t start = 0;
        while (start < subnetsCfg.size()) {
            size_t end = subnetsCfg.find_first_of(", \t\n", start);
            std::string token = subnetsCfg.substr(start, end == std::string::npos ? std::string::npos : end - start);
            start = (end == std::string::npos) ? subnetsCfg.size() : end + 1;
            if (token.empty())
                continue;

            uint32_t bcast = 0;
            size_t slash = token.find('/');
            if (slash != std::string::npos) {
                std::string ipPart = token.substr(0, slash);
                int prefix = atoi(token.substr(slash + 1).c_str());
                struct in_addr ina = {};
                if (inet_pton(AF_INET, ipPart.c_str(), &ina) == 1 && prefix >= 0 && prefix <= 32) {
                    uint32_t hostOrder = ntohl(ina.s_addr);
                    uint32_t mask = (prefix == 0) ? 0 : (0xFFFFFFFFu << (32 - prefix));
                    bcast = htonl((hostOrder & mask) | (~mask));
                }
            } else {
                struct in_addr ina = {};
                if (inet_pton(AF_INET, token.c_str(), &ina) == 1)
                    bcast = ina.s_addr;
            }

            if (bcast != 0) {
                struct sockaddr_in subnetAddr = {};
                subnetAddr.sin_family = AF_INET;
                subnetAddr.sin_addr.s_addr = bcast;
                struct sockaddr_storage subnetStore = {};
                memcpy(&subnetStore, &subnetAddr, sizeof(subnetAddr));
                targets.push_back(subnetStore);
                brls::Logger::info("Discovery: added subnet target {}", token);
            }
        }

        options.broadcast_addrs = static_cast<struct sockaddr_storage*>(malloc(targets.size() * sizeof(struct sockaddr_storage)));
        memcpy(options.broadcast_addrs, targets.data(), targets.size() * sizeof(struct sockaddr_storage));
        options.broadcast_num = targets.size();

        struct sockaddr_in in_addr = {};
        in_addr.sin_family = AF_INET;
        in_addr.sin_addr.s_addr = 0xffffffff;
        struct sockaddr_storage addr;
        memcpy(&addr, &in_addr, sizeof(in_addr));
        options.send_addr = &addr;
        options.send_addr_size = sizeof(in_addr);
        options.send_host = nullptr;

        chiaki_log_init(&discoveryLog, CHIAKI_LOG_ALL, discovery_log_cb, log);

        brls::Logger::info("Calling chiaki_discovery_service_init...");
        ChiakiErrorCode err = chiaki_discovery_service_init(&service, &options, &discoveryLog);
        free(options.broadcast_addrs);
        if (err != CHIAKI_ERR_SUCCESS)
        {
            brls::Logger::error("Discovery service init FAILED: {}", chiaki_error_string(err));
            serviceEnabled = false;
            return;
        }
        brls::Logger::info("Discovery service started successfully!");
    }
    else
    {
        if (remoteDiscoveryEnabled.load())
        {
            brls::Logger::info("Stopping remote discovery thread...");
            remoteDiscoveryEnabled.store(false);
            chiaki_bool_pred_cond_signal(&remoteStopCond);
            chiaki_thread_join(&remoteDiscoveryThread, nullptr);
            chiaki_bool_pred_cond_fini(&remoteStopCond);
            brls::Logger::info("Remote discovery thread stopped");
        }

        chiaki_discovery_service_fini(&service);
    }
}

NetworkAddresses DiscoveryManager::getIPv4BroadcastAddr()
{
    NetworkAddresses result = {0, 0};

    uint32_t current_addr = 0;
    uint32_t subnet_mask = 0;

    Result rc = nifmInitialize(NifmServiceType_User);
    if (R_SUCCEEDED(rc))
    {
        rc = nifmGetCurrentIpConfigInfo(&current_addr, &subnet_mask, NULL, NULL, NULL);
        nifmExit();
    }
    else
    {
        brls::Logger::error("Failed to get nintendo nifmGetCurrentIpConfigInfo");
        return result;
    }

    result.broadcast = current_addr | (~subnet_mask);
    result.local = current_addr;

    return result;
}

std::string DiscoveryManager::getLocalSubnetCidr()
{
    uint32_t current_addr = 0;
    uint32_t subnet_mask = 0;

    Result rc = nifmInitialize(NifmServiceType_User);
    if (R_SUCCEEDED(rc))
    {
        nifmGetCurrentIpConfigInfo(&current_addr, &subnet_mask, NULL, NULL, NULL);
        nifmExit();
    }
    else
    {
        return "";
    }

    if (current_addr == 0)
        return "";

    uint32_t network = current_addr & subnet_mask;
    int prefix = __builtin_popcount(subnet_mask);

    struct in_addr netAddr = {};
    netAddr.s_addr = network;
    char buf[INET_ADDRSTRLEN] = {};
    if (!inet_ntop(AF_INET, &netAddr, buf, sizeof(buf)))
        return "";

    return std::string(buf) + "/" + std::to_string(prefix);
}

int DiscoveryManager::sendDiscovery(struct sockaddr* addr, size_t addrLen)
{
    if (!addr)
    {
        brls::Logger::error("Null sockaddr");
        return 1;
    }

    ChiakiDiscoveryPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.cmd = CHIAKI_DISCOVERY_CMD_SRCH;

    chiaki_discovery_send(&discovery, &packet, addr, addrLen);
    return 0;
}

int DiscoveryManager::sendDiscovery(const char* ipAddress)
{
    struct addrinfo* host_addrinfos;
    int r = getaddrinfo(ipAddress, NULL, NULL, &host_addrinfos);
    if (r != 0)
    {
        brls::Logger::error("getaddrinfo failed");
        return 1;
    }

    for (struct addrinfo* ai = host_addrinfos; ai; ai = ai->ai_next)
    {
        if (ai->ai_protocol != IPPROTO_UDP)
            continue;
        if (ai->ai_family != AF_INET)
            continue;

        hostAddrLen = ai->ai_addrlen;
        if (hostAddr)
        {
            free(hostAddr);
        }
        hostAddr = static_cast<struct sockaddr*>(malloc(hostAddrLen));
        if (!hostAddr)
            break;
        memcpy(hostAddr, ai->ai_addr, hostAddrLen);
    }

    freeaddrinfo(host_addrinfos);

    if (!hostAddr)
    {
        brls::Logger::error("Failed to get addr for hostname");
        return 1;
    }
    return sendDiscovery(hostAddr, hostAddrLen);
}

int DiscoveryManager::sendDiscovery()
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = getIPv4BroadcastAddr().broadcast;

    hostAddrLen = sizeof(sockaddr_in);
    if (hostAddr)
    {
        free(hostAddr);
    }
    hostAddr = static_cast<struct sockaddr*>(malloc(hostAddrLen));
    memcpy(hostAddr, &addr, hostAddrLen);

    return sendDiscovery(hostAddr, hostAddrLen);
}

void DiscoveryManager::discoveryCallback(ChiakiDiscoveryHost* discoveredHost)
{
    struct DiscoveredHostData {
        std::string hostName;
        std::string hostAddr;
        std::string hostId;
        std::string systemVersion;
        std::string deviceDiscoveryProtocolVersion;
        ChiakiDiscoveryHostState state;
        bool hasSystemVersion;
    };

    auto data = std::make_shared<DiscoveredHostData>();
    data->hostName = discoveredHost->host_name ? discoveredHost->host_name : "Unknown";
    data->hostAddr = discoveredHost->host_addr ? discoveredHost->host_addr : "";
    data->hostId = discoveredHost->host_id ? discoveredHost->host_id : "";
    data->systemVersion = discoveredHost->system_version ? discoveredHost->system_version : "";
    data->deviceDiscoveryProtocolVersion = discoveredHost->device_discovery_protocol_version ? discoveredHost->device_discovery_protocol_version : "";
    data->state = discoveredHost->state;
    data->hasSystemVersion = discoveredHost->system_version && discoveredHost->device_discovery_protocol_version;

    ChiakiTarget target = CHIAKI_TARGET_PS4_UNKNOWN;
    if (data->hasSystemVersion)
    {
        target = chiaki_discovery_host_system_version_target(discoveredHost);
    }

    brls::Logger::info("--");
    brls::Logger::info("Discovered Host:");
    brls::Logger::info("State:                             {}", chiaki_discovery_host_state_string(data->state));
    if (data->hasSystemVersion)
    {
        brls::Logger::info("System Version:                    {}", data->systemVersion);
        brls::Logger::info("Device Discovery Protocol Version: {}", data->deviceDiscoveryProtocolVersion);
        brls::Logger::info("PlayStation ChiakiTarget Version:  {}", static_cast<int>(target));
    }
    if (!data->hostAddr.empty())
    {
        brls::Logger::info("Host Addr:                         {}", data->hostAddr);
    }
    if (!data->hostName.empty())
    {
        brls::Logger::info("Host Name:                         {}", data->hostName);
    }
    if (!data->hostId.empty())
    {
        brls::Logger::info("Host ID:                           {}", data->hostId);
    }
    brls::Logger::info("--");

    brls::sync([this, data, target]() {
        auto* hostsMap = settings->getHostsMap();

        Host* host = nullptr;
        auto it = hostsMap->find(data->hostName);
        if (it != hostsMap->end()) {
            host = it->second.get();
        } else if (!data->hostAddr.empty()) {
            for (auto& entry : *hostsMap) {
                if (entry.second && entry.second->getHostAddr() == data->hostAddr) {
                    host = entry.second.get();
                    break;
                }
            }
        }
        if (!host) {
            host = settings->getOrCreateHost(data->hostName);
            host->setHostType(HostType::Auto);
        }

        host->state = data->state;
        host->discovered = true;

        if (data->hasSystemVersion)
        {
            host->setChiakiTarget(target);
        }

        if (!data->hostAddr.empty())
        {
            host->hostAddr = data->hostAddr;
        }

        if (!data->hostId.empty())
        {
            host->hostId = data->hostId;
        }

        if (onHostDiscovered)
        {
            onHostDiscovered(host);
        }
    });
}

void DiscoveryManager::fetchCompanionCredentials(
    const std::string& host,
    int port,
    std::function<void(
        const std::string& onlineId,
        const std::string& accountId,
        const std::string& accessToken,
        const std::string& refreshToken,
        int64_t expiresAt,
        const std::string& duid
    )> onSuccess,
    std::function<void(const std::string&)> onError)
{
    HttpPool::instance().submit([host, port, onSuccess = std::move(onSuccess),
                                 onError = std::move(onError)](HttpSession& session) {
    auto fail = [onError](const std::string& message) {
        brls::sync([onError, message]() { if (onError) onError(message); });
    };

    std::string accountId;
    std::string onlineId;
    std::string accessToken;
    std::string refreshToken;
    int64_t expiresAt = 0;
    std::string duid;

    auto companionGet = [&session, &host, port](const char* path) {
        HttpRequest request;
        request.url = std::format("http://{}:{}{}", host, port, path);
        request.timeoutSec = 10;
        request.connectTimeoutSec = 5;
        return session.perform(request);
    };

    HttpResponse response = companionGet("/account");

    if (response.transportFailed())
    {
        fail(response.error);
        return;
    }

    if (response.status != 200)
    {
        fail(std::format("Account fetch HTTP error: {}", response.status));
        return;
    }

    struct json_object* parsed_json = json_tokener_parse(response.body.c_str());
    if (parsed_json)
    {
        struct json_object* account_id_obj;
        struct json_object* online_id_obj;
        struct json_object* error_obj;

        if (json_object_object_get_ex(parsed_json, "error", &error_obj))
        {
            fail(json_object_get_string(error_obj));
            json_object_put(parsed_json);
            return;
        }

        if (json_object_object_get_ex(parsed_json, "account_id", &account_id_obj))
        {
            accountId = json_object_get_string(account_id_obj);
        }
        if (json_object_object_get_ex(parsed_json, "online_id", &online_id_obj))
        {
            onlineId = json_object_get_string(online_id_obj);
        }
        json_object_put(parsed_json);
    }

    response = companionGet("/token");

    if (response.transportFailed())
    {
        fail("Token fetch failed: " + response.error);
        return;
    }

    if (response.status != 200)
    {
        fail(std::format("Token fetch HTTP error: {}", response.status));
        return;
    }

    parsed_json = json_tokener_parse(response.body.c_str());
    if (parsed_json)
    {
        struct json_object* access_token_obj;
        struct json_object* refresh_token_obj;
        struct json_object* expires_at_obj;
        struct json_object* error_obj;

        if (json_object_object_get_ex(parsed_json, "error", &error_obj))
        {
            fail(std::string("Token error: ") + json_object_get_string(error_obj));
            json_object_put(parsed_json);
            return;
        }

        if (json_object_object_get_ex(parsed_json, "access_token", &access_token_obj))
        {
            accessToken = json_object_get_string(access_token_obj);
        }
        if (json_object_object_get_ex(parsed_json, "refresh_token", &refresh_token_obj))
        {
            refreshToken = json_object_get_string(refresh_token_obj);
        }
        struct json_object* mobile_obj;
        if (json_object_object_get_ex(parsed_json, "psn_mobile_sso_access_token", &mobile_obj))
        {
            SettingsManager* settings = SettingsManager::getInstance();
            settings->setPsnMobileSsoAccessToken(json_object_get_string(mobile_obj));

            if (json_object_object_get_ex(parsed_json, "psn_mobile_sso_refresh_token", &mobile_obj))
                settings->setPsnMobileSsoRefreshToken(json_object_get_string(mobile_obj));

            if (json_object_object_get_ex(parsed_json, "psn_mobile_sso_expires_at", &mobile_obj))
                settings->setPsnMobileSsoExpiresAt(json_object_get_int64(mobile_obj));

            brls::Logger::info("Companion supplied PSN mobile SSO credentials");
        }
        else
        {
            brls::Logger::info("Companion supplied no PSN mobile SSO credentials; game progression will be unavailable");
        }

        if (json_object_object_get_ex(parsed_json, "expires_at", &expires_at_obj))
        {
            expiresAt = json_object_get_int64(expires_at_obj);
        }
        json_object_put(parsed_json);
    }

    response = companionGet("/duid");

    if (response.transportFailed())
    {
        fail("DUID fetch failed: " + response.error);
        return;
    }

    if (response.status != 200)
    {
        fail(std::format("DUID fetch HTTP error: {}", response.status));
        return;
    }

    parsed_json = json_tokener_parse(response.body.c_str());
    if (parsed_json)
    {
        struct json_object* duid_obj;
        struct json_object* error_obj;

        if (json_object_object_get_ex(parsed_json, "error", &error_obj))
        {
            fail(std::string("DUID error: ") + json_object_get_string(error_obj));
            json_object_put(parsed_json);
            return;
        }

        if (json_object_object_get_ex(parsed_json, "duid", &duid_obj))
        {
            duid = json_object_get_string(duid_obj);
        }
        json_object_put(parsed_json);
    }

    brls::sync([onSuccess, onlineId, accountId, accessToken, refreshToken, expiresAt, duid]() {
        if (onSuccess)
            onSuccess(onlineId, accountId, accessToken, refreshToken, expiresAt, duid);
    });
    });
}

psn::ActionStatus DiscoveryManager::getRemoteRefreshStatus() const
{
    std::lock_guard<std::mutex> lock(remoteRefreshMutex);
    return psn::actionStatus(remoteRefreshInFlight, remoteRefreshReadyAt);
}

void DiscoveryManager::refreshRemoteDevices(RemoteRefreshCallback onComplete, bool userInitiated)
{
    brls::Logger::info("DiscoveryManager::refreshRemoteDevices() called");

    {
        std::lock_guard<std::mutex> lock(remoteRefreshMutex);

        if (onComplete)
            remoteRefreshWaiters.push_back(std::move(onComplete));

        if (userInitiated)
            remoteRefreshUserRequested = true;

        if (remoteRefreshInFlight)
        {
            brls::Logger::info("Remote device refresh already running, joining in-flight request");
            return;
        }

        remoteRefreshInFlight = true;
    }

    HttpPool::instance().submit([this](HttpSession& session) { runRemoteDeviceRefresh(session); });
}

void DiscoveryManager::runRemoteDeviceRefresh(HttpSession& session)
{
    psn::Error sessionError = psn::Auth::instance().ensureSession(session);
    if (!sessionError.ok())
    {
        brls::Logger::warning("Cannot discover remote devices: {}", sessionError.message);

        psn::AuthError kind = sessionError.status == psn::Status::Offline
            ? psn::AuthError::Transient
            : psn::AuthError::Invalid;

        finishRemoteDeviceRefresh({false, kind, sessionError.message});
        return;
    }

    fetchRemoteDevicesFromPsn();
    finishRemoteDeviceRefresh({true, psn::AuthError::Transient, ""});
}

void DiscoveryManager::finishRemoteDeviceRefresh(const psn::AuthResult& result)
{
    std::vector<RemoteRefreshCallback> waiters;

    {
        std::lock_guard<std::mutex> lock(remoteRefreshMutex);
        waiters.swap(remoteRefreshWaiters);
        remoteRefreshInFlight = false;

        if (remoteRefreshUserRequested)
        {
            remoteRefreshReadyAt = std::chrono::steady_clock::now() +
                std::chrono::seconds(result.success ? PSN_REMOTE_COOLDOWN_S : PSN_FAILED_COOLDOWN_S);
            remoteRefreshUserRequested = false;
        }
    }

    if (waiters.empty())
        return;

    brls::sync([waiters, result]() {
        for (const auto& waiter : waiters)
            waiter(result);
    });
}

void DiscoveryManager::fetchRemoteDevicesFromPsn()
{
    std::string accessToken = settings->getPsnAccessToken();
    if (accessToken.empty())
    {
        brls::Logger::error("No access token available for remote device discovery");
        return;
    }

    brls::Logger::info("Querying PSN for remote devices...");

    ChiakiHolepunchDeviceInfo* ps5Devices = nullptr;
    size_t ps5Count = 0;
    ChiakiErrorCode ps5Err = chiaki_holepunch_list_devices(
        accessToken.c_str(),
        CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5,
        &ps5Devices,
        &ps5Count,
        log
    );

    if (ps5Err == CHIAKI_ERR_SUCCESS)
    {
        brls::Logger::info("Found {} PS5 remote device(s)", ps5Count);
        for (size_t i = 0; i < ps5Count; i++)
        {
            processRemoteDevice(&ps5Devices[i], CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5);
        }
        chiaki_holepunch_free_device_list(&ps5Devices);
    }
    else
    {
        brls::Logger::error("Failed to list PS5 devices: {}", chiaki_error_string(ps5Err));
    }

    ChiakiHolepunchDeviceInfo* ps4Devices = nullptr;
    size_t ps4Count = 0;
    ChiakiErrorCode ps4Err = chiaki_holepunch_list_devices(
        accessToken.c_str(),
        CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS4,
        &ps4Devices,
        &ps4Count,
        log
    );

    if (ps4Err == CHIAKI_ERR_SUCCESS)
    {
        brls::Logger::info("Found {} PS4 remote device(s)", ps4Count);
        for (size_t i = 0; i < ps4Count; i++)
        {
            processRemoteDevice(&ps4Devices[i], CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS4);
        }
        chiaki_holepunch_free_device_list(&ps4Devices);
    }
    else
    {
        brls::Logger::error("Failed to list PS4 devices: {}", chiaki_error_string(ps4Err));
    }
}

void DiscoveryManager::processRemoteDevice(ChiakiHolepunchDeviceInfo* device, ChiakiHolepunchConsoleType consoleType)
{
    if (!device)
        return;

    std::string deviceName = device->device_name;
    bool remotePlayEnabled = device->remoteplay_enabled;

    std::string deviceUid;
    deviceUid.reserve(64);
    for (size_t j = 0; j < 32; j++)
    {
        std::format_to(std::back_inserter(deviceUid), "{:02x}", device->device_uid[j]);
    }

    brls::Logger::info("Remote device: name='{}', uid='{}', type={}, remoteplay={}",
        deviceName, deviceUid,
        consoleType == CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5 ? "PS5" : "PS4",
        remotePlayEnabled ? "enabled" : "disabled");

    if (!remotePlayEnabled)
    {
        brls::Logger::info("Skipping device '{}' - remote play not enabled", deviceName);
        return;
    }

    brls::sync([this, deviceName, deviceUid, consoleType]() {
        auto* hostsMap = settings->getHostsMap();
        Host* localHost = nullptr;

        auto it = hostsMap->find(deviceName);
        if (it != hostsMap->end() && it->second->hasRpKey() && !it->second->isRemote())
        {
            localHost = it->second.get();
            if (localHost->getRemoteDuid().empty())
            {
                localHost->setRemoteDuid(deviceUid);
                brls::Logger::info("Updated local host '{}' with remote DUID", deviceName);
            }
        }

        std::string remoteName = deviceName + " (Remote)";
        Host* host = settings->getOrCreateHost(remoteName);

        host->setHostType(HostType::Remote);
        host->discovered = true;
        host->setRemoteDuid(deviceUid);

        if (consoleType == CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5)
            host->setChiakiTarget(CHIAKI_TARGET_PS5_1);
        else
            host->setChiakiTarget(CHIAKI_TARGET_PS4_10);

        if (localHost)
        {
            host->copyRegistrationFrom(localHost);
            host->setNeedsLink(false);
        }
        else
        {
            host->setNeedsLink(true);
            brls::Logger::info("Remote device '{}' needs linking to a local host", deviceName);
        }

        host->state = CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN;

        settings->writeFile();

        brls::Logger::info("Calling onHostDiscovered for '{}'", host->getHostName());
        if (onHostDiscovered)
            onHostDiscovered(host);
        else
            brls::Logger::warning("onHostDiscovered callback is null");
    });
}

void* DiscoveryManager::remoteDiscoveryThreadFunc(void* user)
{
    DiscoveryManager* dm = static_cast<DiscoveryManager*>(user);
    dm->runRemoteDiscoveryLoop();
    return nullptr;
}

void DiscoveryManager::runRemoteDiscoveryLoop()
{
    brls::Logger::info("Remote discovery loop started");

    ChiakiErrorCode err = chiaki_bool_pred_cond_lock(&remoteStopCond);
    if (err != CHIAKI_ERR_SUCCESS)
    {
        brls::Logger::error("Failed to lock remote discovery condition");
        return;
    }

    err = chiaki_bool_pred_cond_timedwait(&remoteStopCond, 5000);

    while (err == CHIAKI_ERR_TIMEOUT && remoteDiscoveryEnabled.load())
    {
        std::string refreshToken = settings->getPsnRefreshToken();
        if (!refreshToken.empty())
        {
            brls::Logger::debug("Remote discovery: checking PSN devices...");
            refreshRemoteDevices();
        }

        err = chiaki_bool_pred_cond_timedwait(&remoteStopCond, REMOTE_DISCOVERY_INTERVAL_MS);
    }

    chiaki_bool_pred_cond_unlock(&remoteStopCond);
    brls::Logger::info("Remote discovery loop exiting");
}
