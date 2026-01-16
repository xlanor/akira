#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <map>
#include <vector>
#include <queue>

extern "C" {
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/netif.h"
#include "wireguard.h"
}

class LwipRelay {
public:
    LwipRelay(WgTunnel* tunnel);
    ~LwipRelay();

    bool start(const std::string& tunnelIp, const std::string& targetIp);
    void stop();

    uint16_t startTcpRelay(uint16_t targetPort, uint16_t localPort);
    uint16_t startUdpRelay(uint16_t targetPort, uint16_t localPort);

    void handleIncomingPacket(const void* data, size_t len);
    void tick();

    bool isRunning() const { return running_; }

private:
    struct TcpConnection {
        int localSock;
        struct tcp_pcb* pcb;
        uint16_t localPort;
        uint16_t targetPort;
        std::vector<uint8_t> pendingData;
        bool connected;
    };

    struct UdpBinding {
        int localSock;
        struct udp_pcb* pcb;
        uint16_t localPort;
        uint16_t targetPort;
        struct sockaddr_in clientAddr;
        bool hasClient;
    };

    void runLoop();
    void pollTcpListeners();
    void pollUdpSockets();
    void pollTcpConnections();

    int createTcpListener(uint16_t port);
    int createUdpSocket(uint16_t port);

    static err_t onTcpConnected(void* arg, struct tcp_pcb* pcb, err_t err);
    static err_t onTcpRecv(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err);
    static err_t onTcpSent(void* arg, struct tcp_pcb* pcb, u16_t len);
    static void onTcpError(void* arg, err_t err);

    static void onUdpRecv(void* arg, struct udp_pcb* pcb, struct pbuf* p,
                          const ip_addr_t* addr, u16_t port);

    WgTunnel* tunnel_;
    struct netif netif_;
    ip4_addr_t tunnelAddr_;
    ip4_addr_t targetAddr_;

    std::atomic<bool> running_;
    std::thread loopThread_;
    std::mutex mutex_;

    std::map<uint16_t, int> tcpListeners_;
    std::map<int, std::shared_ptr<TcpConnection>> tcpConnections_;
    std::map<uint16_t, std::shared_ptr<UdpBinding>> udpBindings_;

    std::queue<std::vector<uint8_t>> incomingQueue_;
    std::mutex queueMutex_;

    void processIncomingQueue();

    bool initialized_;
};
