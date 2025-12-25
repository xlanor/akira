#include "core/discovery_manager.hpp"
#include "core/settings_manager.hpp"

#include <borealis.hpp>

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

        // Set up broadcast address
        struct sockaddr_in addr_broadcast = {};
        addr_broadcast.sin_family = AF_INET;
        addr_broadcast.sin_addr.s_addr = addresses.broadcast;
        options.broadcast_addrs = static_cast<struct sockaddr_storage*>(malloc(sizeof(struct sockaddr_storage)));
        memcpy(options.broadcast_addrs, &addr_broadcast, sizeof(addr_broadcast));
        options.broadcast_num = 1;

        // Base broadcast address (255.255.255.255)
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
