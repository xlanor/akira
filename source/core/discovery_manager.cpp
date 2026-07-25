#include "core/discovery_manager.hpp"
#include "core/settings_manager.hpp"

#include <borealis.hpp>
#include <ctime>
#include <format>

#include <cstring>
#include <cstdlib>
#include <switch.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

#include <json-c/json.h>

#include "util/http.hpp"

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

        struct sockaddr_in addr_broadcast = {};
        addr_broadcast.sin_family = AF_INET;
        addr_broadcast.sin_addr.s_addr = addresses.broadcast;
        options.broadcast_addrs = static_cast<struct sockaddr_storage*>(malloc(sizeof(struct sockaddr_storage)));
        memcpy(options.broadcast_addrs, &addr_broadcast, sizeof(addr_broadcast));
        options.broadcast_num = 1;

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
        auto it = hostsMap->find(data->hostName);

        Host* host;
        if (it != hostsMap->end()) {
            host = it->second.get();
        } else {
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
    std::string accountId;
    std::string onlineId;
    std::string accessToken;
    std::string refreshToken;
    int64_t expiresAt = 0;
    std::string duid;

    auto companionGet = [&host, port](const char* path) {
        HttpRequest request;
        request.url = std::format("http://{}:{}{}", host, port, path);
        request.timeoutSec = 10;
        request.connectTimeoutSec = 5;
        return httpPerform(request);
    };

    HttpResponse response = companionGet("/account");

    if (response.transportFailed())
    {
        onError(response.error);
        return;
    }

    if (response.status != 200)
    {
        onError(std::format("Account fetch HTTP error: {}", response.status));
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
            onError(json_object_get_string(error_obj));
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
        onError("Token fetch failed: " + response.error);
        return;
    }

    if (response.status != 200)
    {
        onError(std::format("Token fetch HTTP error: {}", response.status));
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
            onError(std::string("Token error: ") + json_object_get_string(error_obj));
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
        if (json_object_object_get_ex(parsed_json, "expires_at", &expires_at_obj))
        {
            expiresAt = json_object_get_int64(expires_at_obj);
        }
        json_object_put(parsed_json);
    }

    response = companionGet("/duid");

    if (response.transportFailed())
    {
        onError("DUID fetch failed: " + response.error);
        return;
    }

    if (response.status != 200)
    {
        onError(std::format("DUID fetch HTTP error: {}", response.status));
        return;
    }

    parsed_json = json_tokener_parse(response.body.c_str());
    if (parsed_json)
    {
        struct json_object* duid_obj;
        struct json_object* error_obj;

        if (json_object_object_get_ex(parsed_json, "error", &error_obj))
        {
            onError(std::string("DUID error: ") + json_object_get_string(error_obj));
            json_object_put(parsed_json);
            return;
        }

        if (json_object_object_get_ex(parsed_json, "duid", &duid_obj))
        {
            duid = json_object_get_string(duid_obj);
        }
        json_object_put(parsed_json);
    }

    onSuccess(onlineId, accountId, accessToken, refreshToken, expiresAt, duid);
}

static const char* PSN_CLIENT_ID = "ba495a24-818c-472b-b12d-ff231c1b5745";
static const char* PSN_CLIENT_SECRET = "mvaiZkRsAsI1IBkY";
static const char* PSN_TOKEN_URL = "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/token";
static const char* PSN_SCOPES = "psn:clientapp referenceDataService:countryConfig.read pushNotification:webSocket.desktop.connect sessionManager:remotePlaySession.system.update";
static const char* PSN_REDIRECT_URI = "https://remoteplay.dl.playstation.net/remoteplay/redirect";

void DiscoveryManager::refreshPsnToken(
    std::function<void()> onSuccess,
    PsnTokenErrorCallback onError)
{
    {
        std::lock_guard<std::mutex> lock(psnRefreshMutex);
        psnRefreshQueued++;
    }

    psnWorker.post([this, onSuccess = std::move(onSuccess), onError = std::move(onError)]() {
        PsnResult result = refreshPsnTokenBlocking();

        {
            std::lock_guard<std::mutex> lock(psnRefreshMutex);
            psnRefreshQueued--;
        }

        brls::sync([result, onSuccess, onError]() {
            if (result.success)
            {
                if (onSuccess)
                    onSuccess();
            }
            else if (onError)
            {
                onError(result.error, result.message);
            }
        });
    });
}

PsnResult DiscoveryManager::refreshPsnTokenBlocking()
{
    std::unique_lock<std::mutex> lock(psnRefreshMutex);

    if (psnRefreshInFlight)
    {
        brls::Logger::info("PSN token refresh already in flight, awaiting its result");
        psnRefreshCond.wait(lock, [this]() { return !psnRefreshInFlight; });
        return psnLastRefreshResult;
    }

    psnRefreshInFlight = true;
    lock.unlock();

    PsnResult result = performPsnTokenRefresh();

    lock.lock();
    psnLastRefreshResult = result;
    psnRefreshInFlight = false;
    psnRefreshReadyAt = std::chrono::steady_clock::now() +
        std::chrono::seconds(result.success ? PSN_TOKEN_COOLDOWN_S : PSN_FAILED_COOLDOWN_S);
    psnRefreshCond.notify_all();

    return result;
}

static PsnActionStatus psnActionStatus(bool busy, std::chrono::steady_clock::time_point readyAt)
{
    if (busy)
        return {PsnActionState::Busy, 0};

    auto now = std::chrono::steady_clock::now();
    if (readyAt <= now)
        return {PsnActionState::Ready, 0};

    auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(readyAt - now).count();
    return {PsnActionState::CoolingDown, static_cast<int>((remainingMs + 999) / 1000)};
}

PsnActionStatus DiscoveryManager::getTokenRefreshStatus() const
{
    std::lock_guard<std::mutex> lock(psnRefreshMutex);
    return psnActionStatus(psnRefreshInFlight || psnRefreshQueued > 0, psnRefreshReadyAt);
}

PsnActionStatus DiscoveryManager::getRemoteRefreshStatus() const
{
    std::lock_guard<std::mutex> lock(remoteRefreshMutex);
    return psnActionStatus(remoteRefreshInFlight, remoteRefreshReadyAt);
}

PsnResult DiscoveryManager::performPsnTokenRefresh()
{
    std::string refreshToken = settings->getPsnRefreshToken();
    if (refreshToken.empty())
    {
        return {false, PsnAuthError::Invalid, "No refresh token stored"};
    }

    HttpRequest request;
    request.url = PSN_TOKEN_URL;
    request.basicUser = PSN_CLIENT_ID;
    request.basicPassword = PSN_CLIENT_SECRET;
    request.headers = {"Content-Type: application/x-www-form-urlencoded"};
    request.postFields = "grant_type=refresh_token"
        "&refresh_token=" + refreshToken +
        "&scope=" + std::string(PSN_SCOPES) +
        "&redirect_uri=" + std::string(PSN_REDIRECT_URI);
    request.post = true;
    request.timeoutSec = 30;

    HttpResponse response = httpPerform(request);

    if (response.transportFailed())
    {
        brls::Logger::error("PSN token refresh transport failure: {}", response.error);
        return {false, PsnAuthError::Transient, response.error};
    }

    struct json_object* parsed_json = json_tokener_parse(response.body.c_str());

    std::string errorCode;
    std::string errorMessage;
    struct json_object* error_obj;

    if (parsed_json && json_object_object_get_ex(parsed_json, "error", &error_obj))
    {
        errorCode = json_object_get_string(error_obj);
        errorMessage = errorCode;

        struct json_object* error_desc_obj;
        if (json_object_object_get_ex(parsed_json, "error_description", &error_desc_obj))
        {
            errorMessage += ": " + std::string(json_object_get_string(error_desc_obj));
        }
    }

    if (response.status != 200)
    {
        if (parsed_json)
            json_object_put(parsed_json);

        brls::Logger::error("PSN token refresh failed with HTTP {}: {}", response.status, response.body);

        bool tokenRejected = (response.status == 400 || response.status == 401) && errorCode == "invalid_grant";
        if (errorMessage.empty())
            errorMessage = std::format("HTTP error: {}", response.status);

        return {false, tokenRejected ? PsnAuthError::Invalid : PsnAuthError::Transient, errorMessage};
    }

    if (!parsed_json)
    {
        return {false, PsnAuthError::Transient, "Failed to parse JSON response"};
    }

    if (!errorCode.empty())
    {
        json_object_put(parsed_json);
        return {false, PsnAuthError::Transient, errorMessage};
    }

    std::string newAccessToken;
    std::string newRefreshToken;
    int expiresIn = 0;

    struct json_object* access_token_obj;
    struct json_object* refresh_token_obj;
    struct json_object* expires_in_obj;

    if (json_object_object_get_ex(parsed_json, "access_token", &access_token_obj))
    {
        newAccessToken = json_object_get_string(access_token_obj);
    }
    if (json_object_object_get_ex(parsed_json, "refresh_token", &refresh_token_obj))
    {
        newRefreshToken = json_object_get_string(refresh_token_obj);
    }
    if (json_object_object_get_ex(parsed_json, "expires_in", &expires_in_obj))
    {
        expiresIn = json_object_get_int(expires_in_obj);
    }

    json_object_put(parsed_json);

    if (newAccessToken.empty() || newRefreshToken.empty())
    {
        return {false, PsnAuthError::Transient, "Missing tokens in response"};
    }

    settings->setPsnAccessToken(newAccessToken);
    settings->setPsnRefreshToken(newRefreshToken);

    if (expiresIn > 0)
    {
        int64_t expiresAt = static_cast<int64_t>(std::time(nullptr)) + expiresIn;
        settings->setPsnTokenExpiresAt(expiresAt);
    }

    settings->writeFile();

    brls::Logger::info("PSN token refreshed successfully");
    return {true, PsnAuthError::Transient, ""};
}

bool DiscoveryManager::isPsnTokenValid() const
{
    std::string accessToken = settings->getPsnAccessToken();
    if (accessToken.empty())
    {
        return false;
    }

    int64_t expiresAt = settings->getPsnTokenExpiresAt();
    if (expiresAt <= 0)
    {
        return false;
    }

    int64_t now = static_cast<int64_t>(std::time(nullptr));
    return (expiresAt - 60) > now;
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

    psnWorker.post([this]() { runRemoteDeviceRefresh(); });
}

void DiscoveryManager::runRemoteDeviceRefresh()
{
    if (!isPsnTokenValid())
    {
        brls::Logger::info("PSN token not valid, attempting to refresh...");

        std::string refreshToken = settings->getPsnRefreshToken();
        if (refreshToken.empty())
        {
            brls::Logger::warning("No PSN refresh token available, cannot discover remote devices");
            finishRemoteDeviceRefresh({false, PsnAuthError::Invalid, "No refresh token stored"});
            return;
        }

        PsnResult result = refreshPsnTokenBlocking();
        if (!result.success)
        {
            if (result.error == PsnAuthError::Invalid)
            {
                brls::Logger::error("PSN refresh token rejected ({}), clearing stored PSN token data", result.message);
                settings->clearPsnTokenData();
                settings->writeFile();
            }
            else
            {
                brls::Logger::error("PSN token refresh failed transiently ({}), keeping stored tokens", result.message);
            }

            finishRemoteDeviceRefresh({false, result.error, result.message});
            return;
        }

        brls::Logger::info("Token refresh successful, now fetching remote devices");
    }
    else
    {
        brls::Logger::info("PSN token is valid, fetching remote devices");
    }

    fetchRemoteDevicesFromPsn();
    finishRemoteDeviceRefresh({true, PsnAuthError::Transient, ""});
}

void DiscoveryManager::finishRemoteDeviceRefresh(const PsnResult& result)
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
