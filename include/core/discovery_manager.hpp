#ifndef AKIRA_DISCOVERY_MANAGER_HPP
#define AKIRA_DISCOVERY_MANAGER_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <chiaki/discoveryservice.h>
#include <chiaki/log.h>
#include <chiaki/thread.h>
#include <chiaki/remote/holepunch.h>

#include "host.hpp"
#include "psn/auth.hpp"
#include "util/http_pool.hpp"

class SettingsManager;

struct NetworkAddresses {
    uint32_t local;
    uint32_t broadcast;
};

class DiscoveryManager {
public:
    using HostDiscoveredCallback = std::function<void(Host*)>;
    using RemoteRefreshCallback = std::function<void(const psn::AuthResult&)>;

private:
    SettingsManager* settings = nullptr;
    ChiakiLog* log = nullptr;
    ChiakiLog discoveryLog;
    ChiakiDiscoveryService service;
    ChiakiDiscovery discovery;

    struct sockaddr* hostAddr = nullptr;
    size_t hostAddrLen = 0;
    bool serviceEnabled = false;

    HostDiscoveredCallback onHostDiscovered;

    NetworkAddresses getIPv4BroadcastAddr();

    void fetchRemoteDevicesFromPsn();
    void processRemoteDevice(ChiakiHolepunchDeviceInfo* device, ChiakiHolepunchConsoleType consoleType);

    static constexpr int PSN_REMOTE_COOLDOWN_S = 15;
    static constexpr int PSN_FAILED_COOLDOWN_S = 5;

    mutable std::mutex remoteRefreshMutex;
    bool remoteRefreshInFlight = false;
    bool remoteRefreshUserRequested = false;
    std::vector<RemoteRefreshCallback> remoteRefreshWaiters;
    std::chrono::steady_clock::time_point remoteRefreshReadyAt{};

    void runRemoteDeviceRefresh(HttpSession& session);
    void finishRemoteDeviceRefresh(const psn::AuthResult& result);

    ChiakiThread remoteDiscoveryThread;
    ChiakiBoolPredCond remoteStopCond;
    std::atomic<bool> remoteDiscoveryEnabled{false};
    static constexpr uint64_t REMOTE_DISCOVERY_INTERVAL_MS = 45000;

    static void* remoteDiscoveryThreadFunc(void* user);
    void runRemoteDiscoveryLoop();

    DiscoveryManager();

public:
    static DiscoveryManager* getInstance();

    ~DiscoveryManager();

    void setLogger(ChiakiLog* logger) { this->log = logger; }
    ChiakiLog* getLogger() const { return log; }

    void setServiceEnabled(bool enable);
    bool isServiceEnabled() const { return serviceEnabled; }

    int sendDiscovery();
    int sendDiscovery(const char* ipAddress);
    int sendDiscovery(struct sockaddr* addr, size_t addrLen);

    std::string getLocalSubnetCidr();

    void discoveryCallback(ChiakiDiscoveryHost* discoveredHost);

    void setOnHostDiscovered(HostDiscoveredCallback callback) {
        onHostDiscovered = std::move(callback);
    }

    void refreshRemoteDevices(RemoteRefreshCallback onComplete = nullptr, bool userInitiated = false);

    psn::ActionStatus getRemoteRefreshStatus() const;
};

#endif // AKIRA_DISCOVERY_MANAGER_HPP
