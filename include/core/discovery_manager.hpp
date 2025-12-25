#ifndef AKIRA_DISCOVERY_MANAGER_HPP
#define AKIRA_DISCOVERY_MANAGER_HPP

#include <functional>
#include <string>
#include <vector>

#include <chiaki/discoveryservice.h>
#include <chiaki/log.h>

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

    // Singleton
    static DiscoveryManager* instance;
    DiscoveryManager();

public:
    // Singleton access
    static DiscoveryManager* getInstance();

    ~DiscoveryManager();

    // Logger
    void setLogger(ChiakiLog* logger) { this->log = logger; }
    ChiakiLog* getLogger() const { return log; }

    // Service control
    void setServiceEnabled(bool enable);
    bool isServiceEnabled() const { return serviceEnabled; }

    // Discovery methods
    int sendDiscovery();
    int sendDiscovery(const char* ipAddress);
    int sendDiscovery(struct sockaddr* addr, size_t addrLen);

    // Callback from chiaki discovery
    void discoveryCallback(ChiakiDiscoveryHost* discoveredHost);

    // Set callback for when a host is discovered
    void setOnHostDiscovered(HostDiscoveredCallback callback) {
        onHostDiscovered = std::move(callback);
    }

    // PSN Account ID lookup
    void lookupPsnAccountId(
        const std::string& username,
        std::function<void(const std::string&)> onSuccess,
        std::function<void(const std::string&)> onError
    );
};

#endif // AKIRA_DISCOVERY_MANAGER_HPP
