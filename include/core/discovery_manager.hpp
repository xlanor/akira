#ifndef AKIRA_DISCOVERY_MANAGER_HPP
#define AKIRA_DISCOVERY_MANAGER_HPP

#include <functional>
#include <string>
#include <vector>
#include <atomic>

#include <chiaki/discoveryservice.h>
#include <chiaki/log.h>
#include <chiaki/thread.h>
#include <chiaki/remote/holepunch.h>

#include "host.hpp"

class SettingsManager;

struct NetworkAddresses {
    uint32_t local;
    uint32_t broadcast;
};

class DiscoveryManager {
public:
    using HostDiscoveredCallback = std::function<void(Host*)>;

private:
    SettingsManager* settings = nullptr;
    ChiakiLog* log = nullptr;
    ChiakiDiscoveryService service;
    ChiakiDiscovery discovery;

    struct sockaddr* hostAddr = nullptr;
    size_t hostAddrLen = 0;
    bool serviceEnabled = false;

    HostDiscoveredCallback onHostDiscovered;

    NetworkAddresses getIPv4BroadcastAddr();

    void fetchRemoteDevicesFromPsn();
    void processRemoteDevice(ChiakiHolepunchDeviceInfo* device, ChiakiHolepunchConsoleType consoleType);

    ChiakiThread remoteDiscoveryThread;
    ChiakiBoolPredCond remoteStopCond;
    std::atomic<bool> remoteDiscoveryEnabled{false};
    static constexpr uint64_t REMOTE_DISCOVERY_INTERVAL_MS = 45000;

    static void* remoteDiscoveryThreadFunc(void* user);
    void runRemoteDiscoveryLoop();

    static DiscoveryManager* instance;
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

    void lookupPsnAccountId(
        const std::string& username,
        std::function<void(const std::string&)> onSuccess,
        std::function<void(const std::string&)> onError
    );

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
        std::function<void(const std::string&)> onError
    );

    bool isPsnTokenValid() const;

    void refreshRemoteDevices();
};

#endif // AKIRA_DISCOVERY_MANAGER_HPP
