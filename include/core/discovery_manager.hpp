#ifndef AKIRA_DISCOVERY_MANAGER_HPP
#define AKIRA_DISCOVERY_MANAGER_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <borealis.hpp>

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
    using HostsChangedCallback = std::function<void()>;

    struct SweepStatus {
        bool serviceRunning = false;
        bool sweepActive = false;
        std::vector<std::string> subnets;
        std::string currentTarget;
    };

private:
    SettingsManager* settings = nullptr;
    ChiakiLog* log = nullptr;
    ChiakiLog discoveryLog;
    ChiakiDiscoveryService service;
    ChiakiDiscovery discovery;

    struct sockaddr* hostAddr = nullptr;
    size_t hostAddrLen = 0;
    bool serviceEnabled = false;

    brls::Event<bool>::Subscription focusSubscription;
    bool focusSubscribed = false;

    void ensureFocusSubscription();

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

    ChiakiThread sweepThread;
    ChiakiBoolPredCond sweepStopCond;
    std::atomic<bool> sweepEnabled{false};
    std::mutex sweepMutex;
    std::vector<uint32_t> sweepTargets;
    std::vector<uint32_t> liveForeignHosts;
    std::vector<std::string> sweepSubnetLabels;
    std::string sweepCurrentTarget;
    size_t sweepChunkIndex = 0;
    static constexpr uint64_t SWEEP_TICK_MS = 1000;
    static constexpr int SWEEP_SCAN_EVERY_TICKS = 15;
    static constexpr int SWEEP_CHUNK = 64;
    static constexpr int SWEEP_MAX_TARGETS = 1024;

    static void* sweepThreadFunc(void* user);
    void runSweepLoop();
    void sendSweepChunk();
    void pingLiveForeignHosts();
    void pingHostAddrNet(uint32_t addrNet);
    void noteForeignResponder(uint32_t addrNet);

    HostsChangedCallback onHostsChanged;

    DiscoveryManager();

public:
    static DiscoveryManager* getInstance();

    static void appendDiscoveryLog(const std::string& line);
    static std::vector<std::string> getDiscoveryLogSnapshot();
    static uint64_t getDiscoveryLogVersion();
    static void clearDiscoveryLog();

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

    void setOnHostsChanged(HostsChangedCallback callback) {
        onHostsChanged = std::move(callback);
    }

    SweepStatus getSweepStatus();
    void reconcileDiscoveredHosts(const std::vector<std::string>& liveIds);

    void refreshRemoteDevices(RemoteRefreshCallback onComplete = nullptr, bool userInitiated = false);

    psn::ActionStatus getRemoteRefreshStatus() const;
};

#endif // AKIRA_DISCOVERY_MANAGER_HPP
