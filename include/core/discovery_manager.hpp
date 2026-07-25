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
#include "util/http_pool.hpp"

class SettingsManager;

struct NetworkAddresses {
    uint32_t local;
    uint32_t broadcast;
};

enum class PsnAuthError {
    Transient,
    Invalid
};

struct PsnResult {
    bool success = false;
    PsnAuthError error = PsnAuthError::Transient;
    std::string message;
};

enum class PsnActionState {
    Ready,
    Busy,
    CoolingDown
};

struct PsnActionStatus {
    PsnActionState state = PsnActionState::Ready;
    int secondsRemaining = 0;
};

class DiscoveryManager {
public:
    using HostDiscoveredCallback = std::function<void(Host*)>;
    using PsnTokenErrorCallback = std::function<void(PsnAuthError, const std::string&)>;
    using RemoteRefreshCallback = std::function<void(const PsnResult&)>;

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

    static constexpr int PSN_TOKEN_COOLDOWN_S = 60;
    static constexpr int PSN_REMOTE_COOLDOWN_S = 15;
    static constexpr int PSN_FAILED_COOLDOWN_S = 5;

    mutable std::mutex psnRefreshMutex;
    std::condition_variable psnRefreshCond;
    bool psnRefreshInFlight = false;
    int psnRefreshQueued = 0;
    PsnResult psnLastRefreshResult;
    std::chrono::steady_clock::time_point psnRefreshReadyAt{};

    mutable std::mutex remoteRefreshMutex;
    bool remoteRefreshInFlight = false;
    bool remoteRefreshUserRequested = false;
    std::vector<RemoteRefreshCallback> remoteRefreshWaiters;
    std::chrono::steady_clock::time_point remoteRefreshReadyAt{};

    PsnResult performPsnTokenRefresh(HttpSession& session);
    void runRemoteDeviceRefresh(HttpSession& session);
    void finishRemoteDeviceRefresh(const PsnResult& result);

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

    void discoveryCallback(ChiakiDiscoveryHost* discoveredHost);

    void setOnHostDiscovered(HostDiscoveredCallback callback) {
        onHostDiscovered = std::move(callback);
    }

    void fetchCompanionCredentials(
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
        std::function<void(const std::string&)> onError
    );

    void refreshPsnToken(
        std::function<void()> onSuccess,
        PsnTokenErrorCallback onError
    );

    PsnResult refreshPsnTokenBlocking(HttpSession& session);

    bool isPsnTokenValid() const;

    void refreshRemoteDevices(RemoteRefreshCallback onComplete = nullptr, bool userInitiated = false);

    PsnActionStatus getTokenRefreshStatus() const;
    PsnActionStatus getRemoteRefreshStatus() const;
};

#endif // AKIRA_DISCOVERY_MANAGER_HPP
