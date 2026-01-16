#ifndef AKIRA_WIREGUARD_MANAGER_HPP
#define AKIRA_WIREGUARD_MANAGER_HPP

#include <string>
#include <mutex>
#include <atomic>
#include <memory>
#include <map>

extern "C" {
#include <wireguard.h>
}

class LwipRelay;

class WireGuardManager {
public:
    static WireGuardManager& instance();

    bool configExists() const;
    bool loadConfig();
    bool connect();
    void disconnect();
    bool isConnected() const;
    std::string getTunnelIP() const;
    std::string getLastError() const;

    uint16_t startTcpRelay(const std::string& targetIp, uint16_t targetPort, uint16_t localPort = 0);
    uint16_t startUdpRelay(const std::string& targetIp, uint16_t targetPort, uint16_t localPort = 0);
    void stopRelays();
    bool isRelayRunning() const;
    void routeIncomingPacket(const void* data, size_t len);
    int sendUdpPacket(const std::string& targetIp, uint16_t targetPort, const void* data, size_t len);

private:
    WireGuardManager();
    ~WireGuardManager();
    WireGuardManager(const WireGuardManager&) = delete;
    WireGuardManager& operator=(const WireGuardManager&) = delete;

    static constexpr const char* CONFIG_PATH = "sdmc:/switch/akira/wg0.conf";

    bool parseConfigFile(const std::string& path, WgConfig& config);
    std::string parseValue(const std::string& line);

    WgTunnel* tunnel = nullptr;
    std::unique_ptr<LwipRelay> lwipRelay_;
    std::string targetIp_;
    WgConfig config = {};
    bool configLoaded = false;
    std::atomic<bool> connected{false};
    std::atomic<bool> relayRunning{false};
    std::string lastError;
    std::string tunnelIP;
    mutable std::mutex mutex;
};

#endif
