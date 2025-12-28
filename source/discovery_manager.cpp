#include "core/discovery_manager.hpp"
#include "core/settings_manager.hpp"

#include <borealis.hpp>
#include <ctime>

#include <cstring>
#include <cstdlib>
#include <switch.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

#include <curl/curl.h>
#include <json-c/json.h>

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

static size_t CurlWriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp)
{
    userp->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

DiscoveryManager* DiscoveryManager::instance = nullptr;

DiscoveryManager* DiscoveryManager::getInstance()
{
    if (!instance)
    {
        instance = new DiscoveryManager();
    }
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

        brls::Logger::info("Calling chiaki_discovery_service_init...");
        ChiakiErrorCode err = chiaki_discovery_service_init(&service, &options, log);
        if (err != CHIAKI_ERR_SUCCESS)
        {
            brls::Logger::error("Discovery service init FAILED: {}", chiaki_error_string(err));
            serviceEnabled = false;
            free(options.broadcast_addrs);
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
        Host* host = settings->getOrCreateHost(data->hostName);

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

        if (!data->hostName.empty())
        {
            host->hostName = data->hostName;
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

void DiscoveryManager::lookupPsnAccountId(
    const std::string& username,
    std::function<void(const std::string&)> onSuccess,
    std::function<void(const std::string&)> onError)
{
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        onError("Failed to initialize CURL");
        return;
    }

    char* encoded_username = curl_easy_escape(curl, username.c_str(), username.length());
    std::string url = "https://psn.flipscreen.games/search.php?username=" + std::string(encoded_username);
    curl_free(encoded_username);

    std::string response_data;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK)
    {
        onError(curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return;
    }

    struct json_object* parsed_json = json_tokener_parse(response_data.c_str());

    if (!parsed_json)
    {
        onError("Failed to parse JSON response");
        curl_easy_cleanup(curl);
        return;
    }

    struct json_object* encoded_id;
    if (json_object_object_get_ex(parsed_json, "encoded_id", &encoded_id))
    {
        onSuccess(json_object_get_string(encoded_id));
    }
    else
    {
        struct json_object* error;
        if (json_object_object_get_ex(parsed_json, "error", &error))
        {
            onError(json_object_get_string(error));
        }
        else
        {
            onError("Unknown error occurred");
        }
    }

    json_object_put(parsed_json);
    curl_easy_cleanup(curl);
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
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        onError("Failed to initialize CURL");
        return;
    }

    std::string accountId;
    std::string onlineId;
    std::string accessToken;
    std::string refreshToken;
    int64_t expiresAt = 0;
    std::string duid;

    std::string response_data;
    long http_code = 0;
    CURLcode res;

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    std::string accountUrl = "http://" + host + ":" + std::to_string(port) + "/account";
    curl_easy_setopt(curl, CURLOPT_URL, accountUrl.c_str());

    res = curl_easy_perform(curl);

    if (res != CURLE_OK)
    {
        onError(curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200)
    {
        onError("Account fetch HTTP error: " + std::to_string(http_code));
        curl_easy_cleanup(curl);
        return;
    }

    struct json_object* parsed_json = json_tokener_parse(response_data.c_str());
    if (parsed_json)
    {
        struct json_object* account_id_obj;
        struct json_object* online_id_obj;
        struct json_object* error_obj;

        if (json_object_object_get_ex(parsed_json, "error", &error_obj))
        {
            onError(json_object_get_string(error_obj));
            json_object_put(parsed_json);
            curl_easy_cleanup(curl);
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

    std::string tokenUrl = "http://" + host + ":" + std::to_string(port) + "/token";
    response_data.clear();
    curl_easy_setopt(curl, CURLOPT_URL, tokenUrl.c_str());

    res = curl_easy_perform(curl);

    if (res == CURLE_OK)
    {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code == 200)
        {
            parsed_json = json_tokener_parse(response_data.c_str());
            if (parsed_json)
            {
                struct json_object* access_token_obj;
                struct json_object* refresh_token_obj;
                struct json_object* expires_at_obj;

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
        }
    }

    std::string duidUrl = "http://" + host + ":" + std::to_string(port) + "/duid";
    response_data.clear();
    curl_easy_setopt(curl, CURLOPT_URL, duidUrl.c_str());

    res = curl_easy_perform(curl);

    if (res == CURLE_OK)
    {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code == 200)
        {
            parsed_json = json_tokener_parse(response_data.c_str());
            if (parsed_json)
            {
                struct json_object* duid_obj;
                if (json_object_object_get_ex(parsed_json, "duid", &duid_obj))
                {
                    duid = json_object_get_string(duid_obj);
                }
                json_object_put(parsed_json);
            }
        }
    }

    curl_easy_cleanup(curl);

    if (accountId.empty() && onlineId.empty() && accessToken.empty() && refreshToken.empty() && duid.empty())
    {
        onError("No credentials available from companion");
        return;
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
    std::function<void(const std::string&)> onError)
{
    std::string refreshToken = settings->getPsnRefreshToken();
    if (refreshToken.empty())
    {
        onError("No refresh token stored");
        return;
    }

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        onError("Failed to initialize CURL");
        return;
    }

    std::string credentials = std::string(PSN_CLIENT_ID) + ":" + PSN_CLIENT_SECRET;
    char* b64Credentials = nullptr;
    size_t b64Len = 0;

    size_t credLen = credentials.length();
    b64Len = ((4 * credLen / 3) + 3) & ~3;
    b64Credentials = new char[b64Len + 1];
    memset(b64Credentials, 0, b64Len + 1);

    static const char b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0, j = 0;
    for (i = 0; i < credLen - 2; i += 3) {
        b64Credentials[j++] = b64chars[(credentials[i] >> 2) & 0x3F];
        b64Credentials[j++] = b64chars[((credentials[i] & 0x3) << 4) | ((credentials[i + 1] >> 4) & 0xF)];
        b64Credentials[j++] = b64chars[((credentials[i + 1] & 0xF) << 2) | ((credentials[i + 2] >> 6) & 0x3)];
        b64Credentials[j++] = b64chars[credentials[i + 2] & 0x3F];
    }
    if (i < credLen) {
        b64Credentials[j++] = b64chars[(credentials[i] >> 2) & 0x3F];
        if (i == credLen - 1) {
            b64Credentials[j++] = b64chars[(credentials[i] & 0x3) << 4];
            b64Credentials[j++] = '=';
        } else {
            b64Credentials[j++] = b64chars[((credentials[i] & 0x3) << 4) | ((credentials[i + 1] >> 4) & 0xF)];
            b64Credentials[j++] = b64chars[(credentials[i + 1] & 0xF) << 2];
        }
        b64Credentials[j++] = '=';
    }

    std::string authHeader = "Authorization: Basic " + std::string(b64Credentials);
    delete[] b64Credentials;

    std::string postData = "grant_type=refresh_token"
        "&refresh_token=" + refreshToken +
        "&scope=" + std::string(PSN_SCOPES) +
        "&redirect_uri=" + std::string(PSN_REDIRECT_URI);

    std::string response_data;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, authHeader.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl, CURLOPT_URL, PSN_TOKEN_URL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);

    if (res != CURLE_OK)
    {
        onError(curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (http_code != 200)
    {
        brls::Logger::error("PSN token refresh failed with HTTP {}: {}", http_code, response_data);
        onError("HTTP error: " + std::to_string(http_code));
        curl_easy_cleanup(curl);
        return;
    }

    struct json_object* parsed_json = json_tokener_parse(response_data.c_str());
    if (!parsed_json)
    {
        onError("Failed to parse JSON response");
        curl_easy_cleanup(curl);
        return;
    }

    struct json_object* access_token_obj;
    struct json_object* refresh_token_obj;
    struct json_object* error_obj;

    if (json_object_object_get_ex(parsed_json, "error", &error_obj))
    {
        std::string errorMsg = json_object_get_string(error_obj);
        struct json_object* error_desc_obj;
        if (json_object_object_get_ex(parsed_json, "error_description", &error_desc_obj))
        {
            errorMsg += ": " + std::string(json_object_get_string(error_desc_obj));
        }
        onError(errorMsg);
        json_object_put(parsed_json);
        curl_easy_cleanup(curl);
        return;
    }

    std::string newAccessToken;
    std::string newRefreshToken;
    int expiresIn = 0;

    if (json_object_object_get_ex(parsed_json, "access_token", &access_token_obj))
    {
        newAccessToken = json_object_get_string(access_token_obj);
    }
    if (json_object_object_get_ex(parsed_json, "refresh_token", &refresh_token_obj))
    {
        newRefreshToken = json_object_get_string(refresh_token_obj);
    }

    struct json_object* expires_in_obj;
    if (json_object_object_get_ex(parsed_json, "expires_in", &expires_in_obj))
    {
        expiresIn = json_object_get_int(expires_in_obj);
    }

    json_object_put(parsed_json);
    curl_easy_cleanup(curl);

    if (newAccessToken.empty() || newRefreshToken.empty())
    {
        onError("Missing tokens in response");
        return;
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
    onSuccess();
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

void DiscoveryManager::refreshRemoteDevices()
{
    brls::Logger::info("DiscoveryManager::refreshRemoteDevices() called");

    if (!isPsnTokenValid())
    {
        brls::Logger::info("PSN token not valid, attempting to refresh...");

        std::string refreshToken = settings->getPsnRefreshToken();
        if (refreshToken.empty())
        {
            brls::Logger::warning("No PSN refresh token available, cannot discover remote devices");
            return;
        }

        refreshPsnToken(
            [this]() {
                brls::Logger::info("Token refresh successful, now fetching remote devices");
                fetchRemoteDevicesFromPsn();
            },
            [this](const std::string& error) {
                brls::Logger::error("Failed to refresh PSN token: {}", error);
                brls::Logger::info("Clearing invalid PSN token data from config");
                settings->clearPsnTokenData();
                settings->writeFile();
            }
        );
        return;
    }

    brls::Logger::info("PSN token is valid, fetching remote devices");
    fetchRemoteDevicesFromPsn();
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

    char uidHex[65] = {0};
    for (size_t j = 0; j < 32; j++)
    {
        snprintf(uidHex + (j * 2), 3, "%02x", device->device_uid[j]);
    }
    std::string deviceUid = uidHex;

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
        if (it != hostsMap->end() && it->second->hasRpKey())
        {
            localHost = it->second;
            if (localHost->getRemoteDuid().empty())
            {
                localHost->setRemoteDuid(deviceUid);
                brls::Logger::info("Updated local host '{}' with remote DUID", deviceName);
            }
        }

        std::string displayName = "[Remote] " + deviceName;
        Host* host = settings->getOrCreateHost(displayName);

        host->isRemoteHost = true;
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
