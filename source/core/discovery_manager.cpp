#include "core/discovery_manager.hpp"
#include "core/discovery_sweep.hpp"
#include "core/settings_manager.hpp"

#include <borealis.hpp>
#include <ctime>
#include <format>

#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <vector>
#include <switch.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

#include <json-c/json.h>

#include "util/http.hpp"
#include "util/http_pool.hpp"

static std::mutex g_discoveryLogMutex;
static std::deque<std::string> g_discoveryLogLines;
static std::atomic<uint64_t> g_discoveryLogVersion{0};
static constexpr size_t DISCOVERY_LOG_MAX_LINES = 300;

void DiscoveryManager::appendDiscoveryLog(const std::string& line)
{
    std::lock_guard<std::mutex> lock(g_discoveryLogMutex);
    g_discoveryLogLines.push_back(line);
    while (g_discoveryLogLines.size() > DISCOVERY_LOG_MAX_LINES)
        g_discoveryLogLines.pop_front();
    g_discoveryLogVersion.fetch_add(1, std::memory_order_relaxed);
}

uint64_t DiscoveryManager::getDiscoveryLogVersion()
{
    return g_discoveryLogVersion.load(std::memory_order_relaxed);
}

std::vector<std::string> DiscoveryManager::getDiscoveryLogSnapshot()
{
    std::lock_guard<std::mutex> lock(g_discoveryLogMutex);
    return std::vector<std::string>(g_discoveryLogLines.begin(), g_discoveryLogLines.end());
}

void DiscoveryManager::clearDiscoveryLog()
{
    std::lock_guard<std::mutex> lock(g_discoveryLogMutex);
    g_discoveryLogLines.clear();
    g_discoveryLogVersion.fetch_add(1, std::memory_order_relaxed);
}

static void discovery_log_cb(ChiakiLogLevel level, const char* msg, void* user)
{
    if (!SettingsManager::getInstance()->getDebugDiscoveryLog())
        return;

    if (msg)
        DiscoveryManager::appendDiscoveryLog(msg);

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
    std::vector<std::string> liveIds;
    for (size_t i = 0; i < hosts_count; i++)
    {
        dm->discoveryCallback(&discovered_hosts[i]);
        if (discovered_hosts[i].host_id)
            liveIds.emplace_back(discovered_hosts[i].host_id);
    }
    dm->reconcileDiscoveredHosts(liveIds);
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
    if (focusSubscribed)
    {
        brls::Application::getWindowFocusChangedEvent()->unsubscribe(focusSubscription);
        focusSubscribed = false;
    }

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

void DiscoveryManager::ensureFocusSubscription()
{
    if (focusSubscribed)
        return;

    focusSubscription = brls::Application::getWindowFocusChangedEvent()->subscribe([this](bool focused) {
        if (!focused || !serviceEnabled)
            return;

        brls::Logger::info("Discovery: applet back in focus, restarting service on fresh sockets");
        setServiceEnabled(false);
        setServiceEnabled(true);
    });
    focusSubscribed = true;
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

        std::vector<uint32_t> flatTargets;
        std::vector<std::string> labels;

        std::string subnetsCfg = settings->getDiscoverySubnets();
        size_t start = 0;
        while (start < subnetsCfg.size()) {
            size_t end = subnetsCfg.find_first_of(", \t\n", start);
            std::string token = subnetsCfg.substr(start, end == std::string::npos ? std::string::npos : end - start);
            start = (end == std::string::npos) ? subnetsCfg.size() : end + 1;
            if (token.empty())
                continue;

            uint32_t base = 0;
            int prefix = 0;
            if (!akira::discovery::parseSweepCidr(token, base, prefix))
                continue;

            if (akira::discovery::subnetContainsLocal(addresses.local, base, prefix)) {
                brls::Logger::info("Discovery: subnet {} is local, covered by broadcast", token);
                continue;
            }

            uint32_t firstOff = 0;
            uint32_t lastOff = 0;
            akira::discovery::sweepHostOffsets(prefix, firstOff, lastOff);
            for (uint32_t off = firstOff; off <= lastOff && flatTargets.size() < static_cast<size_t>(SWEEP_MAX_TARGETS); off++)
                flatTargets.push_back(akira::discovery::sweepAddrNet(base, off));

            if (flatTargets.size() >= static_cast<size_t>(SWEEP_MAX_TARGETS))
                brls::Logger::warning("Discovery: sweep target cap {} reached, remaining hosts skipped", SWEEP_MAX_TARGETS);
            labels.push_back(akira::discovery::normalizeSweepCidr(token));
            brls::Logger::info("Discovery: added foreign sweep subnet {} ({} hosts)", token, akira::discovery::sweepHostCount(prefix));
        }

        {
            std::lock_guard<std::mutex> lk(sweepMutex);
            sweepTargets = std::move(flatTargets);
            sweepSubnetLabels = std::move(labels);
            liveForeignHosts.clear();
            sweepCurrentTarget.clear();
            sweepChunkIndex = 0;
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

        ensureFocusSubscription();

        bool haveSweep = false;
        {
            std::lock_guard<std::mutex> lk(sweepMutex);
            haveSweep = !sweepTargets.empty();
        }
        if (haveSweep && !sweepEnabled.load())
        {
            if (chiaki_bool_pred_cond_init(&sweepStopCond) == CHIAKI_ERR_SUCCESS)
            {
                sweepEnabled.store(true);
                if (chiaki_thread_create(&sweepThread, sweepThreadFunc, this) != CHIAKI_ERR_SUCCESS)
                {
                    sweepEnabled.store(false);
                    chiaki_bool_pred_cond_fini(&sweepStopCond);
                    brls::Logger::error("Failed to start discovery sweep thread");
                }
                else
                {
                    brls::Logger::info("Discovery sweep thread started");
                }
            }
        }
    }
    else
    {
        if (sweepEnabled.load())
        {
            brls::Logger::info("Stopping discovery sweep thread...");
            sweepEnabled.store(false);
            chiaki_bool_pred_cond_signal(&sweepStopCond);
            chiaki_thread_join(&sweepThread, nullptr);
            chiaki_bool_pred_cond_fini(&sweepStopCond);
            brls::Logger::info("Discovery sweep thread stopped");
        }

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

        {
            std::lock_guard<std::mutex> lk(sweepMutex);
            sweepTargets.clear();
            sweepSubnetLabels.clear();
            liveForeignHosts.clear();
            sweepCurrentTarget.clear();
            sweepChunkIndex = 0;
        }
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

    if (!data->hostAddr.empty())
    {
        struct in_addr hostIna = {};
        if (inet_pton(AF_INET, data->hostAddr.c_str(), &hostIna) == 1)
        {
            uint32_t addrNet = hostIna.s_addr;
            bool isSweepTarget = false;
            {
                std::lock_guard<std::mutex> lk(sweepMutex);
                for (uint32_t t : sweepTargets)
                {
                    if (t == addrNet)
                    {
                        isSweepTarget = true;
                        break;
                    }
                }
            }
            if (isSweepTarget)
                noteForeignResponder(addrNet);
        }
    }

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
        brls::sync([this, deviceName, deviceUid]() {
            auto* hostsMap = settings->getHostsMap();
            if (!hostsMap)
                return;
            for (auto& entry : *hostsMap)
            {
                Host* h = entry.second.get();
                if (!h)
                    continue;
                if (h->getHostName() == deviceName || h->getRemoteDuid() == deviceUid)
                    h->setPsnRemotePlayDisabled(true);
            }
        });
        return;
    }

    brls::sync([this, deviceName, deviceUid, consoleType]() {
        auto* hostsMap = settings->getHostsMap();
        Host* localHost = nullptr;

        for (auto& entry : *hostsMap)
        {
            Host* h = entry.second.get();
            if (h && (h->getHostName() == deviceName || h->getRemoteDuid() == deviceUid))
                h->setPsnRemotePlayDisabled(false);
        }

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

void* DiscoveryManager::sweepThreadFunc(void* user)
{
    DiscoveryManager* dm = static_cast<DiscoveryManager*>(user);
    dm->runSweepLoop();
    return nullptr;
}

void DiscoveryManager::runSweepLoop()
{
    brls::Logger::info("Discovery sweep loop started");

    ChiakiErrorCode err = chiaki_bool_pred_cond_lock(&sweepStopCond);
    if (err != CHIAKI_ERR_SUCCESS)
    {
        brls::Logger::error("Failed to lock sweep condition");
        return;
    }

    int tick = 0;
    err = chiaki_bool_pred_cond_timedwait(&sweepStopCond, 2000);

    while (err == CHIAKI_ERR_TIMEOUT && sweepEnabled.load())
    {
        pingLiveForeignHosts();
        if (tick % SWEEP_SCAN_EVERY_TICKS == 0)
            sendSweepChunk();
        tick++;

        err = chiaki_bool_pred_cond_timedwait(&sweepStopCond, SWEEP_TICK_MS);
    }

    chiaki_bool_pred_cond_unlock(&sweepStopCond);
    brls::Logger::info("Discovery sweep loop exiting");
}

void DiscoveryManager::pingHostAddrNet(uint32_t addrNet)
{
    struct in_addr ina = {};
    ina.s_addr = addrNet;
    char buf[INET_ADDRSTRLEN] = {};
    if (inet_ntop(AF_INET, &ina, buf, sizeof(buf)))
    {
        std::lock_guard<std::mutex> lk(sweepMutex);
        sweepCurrentTarget = buf;
    }

    struct sockaddr_in dst = {};
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = addrNet;

    ChiakiDiscoveryPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.cmd = CHIAKI_DISCOVERY_CMD_SRCH;

    packet.protocol_version = const_cast<char*>(CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS4);
    dst.sin_port = htons(CHIAKI_DISCOVERY_PORT_PS4);
    chiaki_discovery_send(&service.discovery, &packet, reinterpret_cast<struct sockaddr*>(&dst), sizeof(dst));

    packet.protocol_version = const_cast<char*>(CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS5);
    dst.sin_port = htons(CHIAKI_DISCOVERY_PORT_PS5);
    chiaki_discovery_send(&service.discovery, &packet, reinterpret_cast<struct sockaddr*>(&dst), sizeof(dst));
}

void DiscoveryManager::pingLiveForeignHosts()
{
    std::vector<uint32_t> hosts;
    {
        std::lock_guard<std::mutex> lk(sweepMutex);
        hosts = liveForeignHosts;
    }
    for (uint32_t addr : hosts)
    {
        if (!sweepEnabled.load())
            return;
        pingHostAddrNet(addr);
    }
}

void DiscoveryManager::noteForeignResponder(uint32_t addrNet)
{
    std::lock_guard<std::mutex> lk(sweepMutex);
    for (uint32_t existing : liveForeignHosts)
    {
        if (existing == addrNet)
            return;
    }
    if (liveForeignHosts.size() >= static_cast<size_t>(SWEEP_MAX_TARGETS))
        return;
    liveForeignHosts.push_back(addrNet);
}

void DiscoveryManager::sendSweepChunk()
{
    std::vector<uint32_t> targets;
    size_t chunk = 0;
    {
        std::lock_guard<std::mutex> lk(sweepMutex);
        if (sweepTargets.empty())
            return;
        int numChunks = akira::discovery::sweepChunkCount(static_cast<int>(sweepTargets.size()), SWEEP_CHUNK);
        chunk = sweepChunkIndex;
        sweepChunkIndex = (sweepChunkIndex + 1) % (numChunks > 0 ? static_cast<size_t>(numChunks) : 1);
        targets = sweepTargets;
    }

    size_t startIdx = chunk * SWEEP_CHUNK;
    size_t endIdx = startIdx + SWEEP_CHUNK;
    if (endIdx > targets.size())
        endIdx = targets.size();

    for (size_t i = startIdx; i < endIdx && sweepEnabled.load(); i++)
        pingHostAddrNet(targets[i]);
}

DiscoveryManager::SweepStatus DiscoveryManager::getSweepStatus()
{
    SweepStatus s;
    s.serviceRunning = serviceEnabled;
    s.sweepActive = sweepEnabled.load();
    std::lock_guard<std::mutex> lk(sweepMutex);
    s.subnets = sweepSubnetLabels;
    s.currentTarget = sweepCurrentTarget;
    return s;
}

void DiscoveryManager::reconcileDiscoveredHosts(const std::vector<std::string>& liveIds)
{
    auto ids = std::make_shared<std::vector<std::string>>(liveIds);
    brls::sync([this, ids]() {
        auto* hostsMap = settings->getHostsMap();
        if (!hostsMap)
            return;

        bool changed = false;
        for (auto& entry : *hostsMap)
        {
            Host* host = entry.second.get();
            if (!host || host->hostId.empty())
                continue;

            bool present = false;
            for (const auto& id : *ids)
            {
                if (!id.empty() && id == host->hostId)
                {
                    present = true;
                    break;
                }
            }

            if (!present && host->state != CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN)
            {
                host->state = CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN;
                changed = true;
            }

            if (host->hasRpKey() || host->isManual() || host->isRemote())
                continue;
            if (!host->isDiscovered())
                continue;

            if (!present)
            {
                host->discovered = false;
                changed = true;
            }
        }

        if (changed && onHostsChanged)
            onHostsChanged();
    });
}
