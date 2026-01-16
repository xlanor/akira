#include "core/wireguard_manager.hpp"
#include "core/lwip_relay.hpp"
#include <borealis.hpp>
#include <fstream>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

WireGuardManager& WireGuardManager::instance() {
    static WireGuardManager inst;
    return inst;
}

static void wg_lib_log_callback(const char* msg) {
    brls::Logger::info("WG-LIB: {}", msg);
}

static void wg_relay_recv_callback(void* user, const void* data, size_t len) {
    WireGuardManager* mgr = static_cast<WireGuardManager*>(user);
    mgr->routeIncomingPacket(data, len);
}

WireGuardManager::WireGuardManager() {
    memset(&config, 0, sizeof(config));
    wg_set_log_callback(wg_lib_log_callback);
}

WireGuardManager::~WireGuardManager() {
    disconnect();
}

bool WireGuardManager::configExists() const {
    std::ifstream file(CONFIG_PATH);
    return file.good();
}

std::string WireGuardManager::parseValue(const std::string& line) {
    size_t pos = line.find('=');
    if (pos == std::string::npos)
        return "";
    std::string value = line.substr(pos + 1);
    size_t start = value.find_first_not_of(" \t");
    size_t end = value.find_last_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    return value.substr(start, end - start + 1);
}

bool WireGuardManager::parseConfigFile(const std::string& path, WgConfig& cfg) {
    brls::Logger::info("WG: parseConfigFile({})", path);
    std::ifstream file(path);
    if (!file.is_open()) {
        lastError = "Cannot open config file";
        brls::Logger::error("WG: cannot open config file");
        return false;
    }
    brls::Logger::info("WG: config file opened");

    std::string line;
    std::string section;
    std::string privateKey, publicKey, endpoint, address;
    uint16_t port = 51820;
    uint16_t keepalive = 25;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#')
            continue;

        if (line[0] == '[') {
            size_t end = line.find(']');
            if (end != std::string::npos)
                section = line.substr(1, end - 1);
            continue;
        }

        if (line.find("PrivateKey") != std::string::npos) {
            privateKey = parseValue(line);
            brls::Logger::info("WG: found PrivateKey");
        } else if (line.find("PublicKey") != std::string::npos) {
            publicKey = parseValue(line);
            brls::Logger::info("WG: found PublicKey");
        } else if (line.find("Endpoint") != std::string::npos) {
            std::string ep = parseValue(line);
            brls::Logger::info("WG: found Endpoint: {}", ep);
            size_t colonPos = ep.rfind(':');
            if (colonPos != std::string::npos) {
                endpoint = ep.substr(0, colonPos);
                std::string portStr = ep.substr(colonPos + 1);
                brls::Logger::info("WG: parsing port: {}", portStr);
                port = static_cast<uint16_t>(std::stoi(portStr));
            } else {
                endpoint = ep;
            }
            brls::Logger::info("WG: endpoint={} port={}", endpoint, port);
        } else if (line.find("Address") != std::string::npos) {
            address = parseValue(line);
            brls::Logger::info("WG: found Address: {}", address);
            size_t slashPos = address.find('/');
            if (slashPos != std::string::npos)
                address = address.substr(0, slashPos);
            size_t commaPos = address.find(',');
            if (commaPos != std::string::npos)
                address = address.substr(0, commaPos);
            brls::Logger::info("WG: parsed address: {}", address);
        } else if (line.find("PersistentKeepalive") != std::string::npos) {
            std::string kaStr = parseValue(line);
            brls::Logger::info("WG: found PersistentKeepalive: {}", kaStr);
            keepalive = static_cast<uint16_t>(std::stoi(kaStr));
        }
    }

    brls::Logger::info("WG: validating parsed values");
    if (privateKey.empty()) {
        lastError = "Missing PrivateKey";
        brls::Logger::error("WG: {}", lastError);
        return false;
    }
    if (publicKey.empty()) {
        lastError = "Missing PublicKey";
        brls::Logger::error("WG: {}", lastError);
        return false;
    }
    if (endpoint.empty()) {
        lastError = "Missing Endpoint";
        brls::Logger::error("WG: {}", lastError);
        return false;
    }
    if (address.empty()) {
        lastError = "Missing Address";
        brls::Logger::error("WG: {}", lastError);
        return false;
    }

    brls::Logger::info("WG: decoding private key");
    if (wg_key_from_base64(cfg.private_key, privateKey.c_str()) != 0) {
        lastError = "Invalid PrivateKey format";
        brls::Logger::error("WG: {}", lastError);
        return false;
    }
    brls::Logger::info("WG: decoding public key");
    if (wg_key_from_base64(cfg.peer_public_key, publicKey.c_str()) != 0) {
        lastError = "Invalid PublicKey format";
        brls::Logger::error("WG: {}", lastError);
        return false;
    }

    brls::Logger::info("WG: parsing tunnel IP: {}", address);
    if (inet_pton(AF_INET, address.c_str(), &cfg.tunnel_ip) != 1) {
        lastError = "Invalid Address format";
        brls::Logger::error("WG: {}", lastError);
        return false;
    }

    brls::Logger::info("WG: setting endpoint host: {}", endpoint);
    strncpy(cfg.endpoint_host, endpoint.c_str(), sizeof(cfg.endpoint_host) - 1);
    cfg.endpoint_port = port;
    cfg.keepalive_interval = keepalive;
    cfg.has_preshared_key = 0;

    tunnelIP = address;
    brls::Logger::info("WG: parseConfigFile completed successfully");
    return true;
}

bool WireGuardManager::loadConfig() {
    std::lock_guard<std::mutex> lock(mutex);
    if (!parseConfigFile(CONFIG_PATH, config))
        return false;
    configLoaded = true;
    return true;
}

bool WireGuardManager::connect() {
    brls::Logger::info("WG: connect() entered, acquiring lock");
    std::lock_guard<std::mutex> lock(mutex);
    brls::Logger::info("WG: lock acquired");

    if (connected) {
        brls::Logger::info("WG: already connected");
        return true;
    }

    if (!configLoaded) {
        brls::Logger::info("WG: parsing config file");
        if (!parseConfigFile(CONFIG_PATH, config)) {
            brls::Logger::error("WG: parseConfigFile failed: {}", lastError);
            return false;
        }
        configLoaded = true;
        brls::Logger::info("WG: config loaded successfully");
    }

    brls::Logger::info("WG: calling wg_init");
    tunnel = wg_init(&config);
    if (!tunnel) {
        lastError = "Failed to initialize tunnel";
        brls::Logger::error("WG: wg_init failed");
        return false;
    }
    brls::Logger::info("WG: wg_init succeeded, calling wg_connect");

    int err = wg_connect(tunnel);
    if (err != WG_OK) {
        lastError = "Handshake failed: " + std::to_string(err);
        brls::Logger::error("WG: wg_connect failed: {}", err);
        wg_close(tunnel);
        tunnel = nullptr;
        return false;
    }

    brls::Logger::info("WG: wg_connect succeeded, calling wg_start");
    err = wg_start(tunnel);
    if (err != WG_OK) {
        lastError = "Failed to start tunnel: " + std::to_string(err);
        brls::Logger::error("WG: wg_start failed: {}", err);
        wg_close(tunnel);
        tunnel = nullptr;
        return false;
    }

    brls::Logger::info("WG: connected and started successfully");
    connected = true;
    lastError.clear();
    return true;
}

void WireGuardManager::disconnect() {
    std::lock_guard<std::mutex> lock(mutex);

    stopRelays();

    if (tunnel) {
        wg_stop(tunnel);
        wg_close(tunnel);
        tunnel = nullptr;
    }
    connected = false;
}

bool WireGuardManager::isConnected() const {
    return connected;
}

std::string WireGuardManager::getTunnelIP() const {
    std::lock_guard<std::mutex> lock(mutex);
    return tunnelIP;
}

std::string WireGuardManager::getLastError() const {
    std::lock_guard<std::mutex> lock(mutex);
    return lastError;
}

void WireGuardManager::routeIncomingPacket(const void* data, size_t len) {
    LwipRelay* relay = lwipRelay_.get();
    if (relay) {
        relay->handleIncomingPacket(data, len);
    }
}

uint16_t WireGuardManager::startTcpRelay(const std::string& targetIp, uint16_t targetPort, uint16_t localPort) {
    std::lock_guard<std::mutex> lock(mutex);

    if (!connected || !tunnel)
        return 0;

    if (!lwipRelay_) {
        lwipRelay_ = std::make_unique<LwipRelay>(tunnel);
        if (!lwipRelay_->start(tunnelIP, targetIp)) {
            brls::Logger::error("WG: Failed to start lwIP relay");
            lwipRelay_.reset();
            return 0;
        }
        targetIp_ = targetIp;
        wg_set_recv_callback(tunnel, wg_relay_recv_callback, this);
        relayRunning = true;
    }

    return lwipRelay_->startTcpRelay(targetPort, localPort);
}

uint16_t WireGuardManager::startUdpRelay(const std::string& targetIp, uint16_t targetPort, uint16_t localPort) {
    std::lock_guard<std::mutex> lock(mutex);

    if (!connected || !tunnel)
        return 0;

    if (!lwipRelay_) {
        lwipRelay_ = std::make_unique<LwipRelay>(tunnel);
        if (!lwipRelay_->start(tunnelIP, targetIp)) {
            brls::Logger::error("WG: Failed to start lwIP relay");
            lwipRelay_.reset();
            return 0;
        }
        targetIp_ = targetIp;
        wg_set_recv_callback(tunnel, wg_relay_recv_callback, this);
        relayRunning = true;
    }

    return lwipRelay_->startUdpRelay(targetPort, localPort);
}

void WireGuardManager::stopRelays() {
    if (tunnel) {
        wg_set_recv_callback(tunnel, nullptr, nullptr);
    }
    if (lwipRelay_) {
        lwipRelay_->stop();
        lwipRelay_.reset();
    }
    relayRunning = false;
}

bool WireGuardManager::isRelayRunning() const {
    return relayRunning;
}

int WireGuardManager::sendUdpPacket(const std::string& targetIp, uint16_t targetPort, const void* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex);

    if (!connected || !tunnel)
        return -1;

    uint32_t dstIp;
    if (inet_pton(AF_INET, targetIp.c_str(), &dstIp) != 1)
        return -1;

    uint32_t srcIp = config.tunnel_ip.s_addr;
    uint16_t srcPort = 9999;

    size_t ipHdrLen = 20;
    size_t udpHdrLen = 8;
    size_t totalLen = ipHdrLen + udpHdrLen + len;

    uint8_t buf[2048];
    if (totalLen > sizeof(buf))
        return -1;

    uint8_t* ip = buf;
    ip[0] = 0x45;
    ip[1] = 0;
    ip[2] = (totalLen >> 8) & 0xFF;
    ip[3] = totalLen & 0xFF;
    ip[4] = ip[5] = 0;
    ip[6] = ip[7] = 0;
    ip[8] = 64;
    ip[9] = IPPROTO_UDP;
    ip[10] = ip[11] = 0;
    memcpy(ip + 12, &srcIp, 4);
    memcpy(ip + 16, &dstIp, 4);

    uint32_t sum = 0;
    for (int i = 0; i < 20; i += 2) {
        sum += (ip[i] << 8) | ip[i + 1];
    }
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    uint16_t ipCksum = ~sum;
    ip[10] = (ipCksum >> 8) & 0xFF;
    ip[11] = ipCksum & 0xFF;

    uint8_t* udp = buf + ipHdrLen;
    udp[0] = (srcPort >> 8) & 0xFF;
    udp[1] = srcPort & 0xFF;
    udp[2] = (targetPort >> 8) & 0xFF;
    udp[3] = targetPort & 0xFF;
    uint16_t udpLen = udpHdrLen + len;
    udp[4] = (udpLen >> 8) & 0xFF;
    udp[5] = udpLen & 0xFF;
    udp[6] = udp[7] = 0;

    memcpy(buf + ipHdrLen + udpHdrLen, data, len);

    brls::Logger::info("WG: sendUdpPacket to {}:{}, len={}", targetIp, targetPort, len);
    return wg_send(tunnel, buf, totalLen);
}
