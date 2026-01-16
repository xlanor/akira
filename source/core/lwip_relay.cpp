#include "core/lwip_relay.hpp"
#include "core/wg_netif.h"
#include <borealis.hpp>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>

extern "C" {
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/ip.h"
}

#define POLL_TIMEOUT_MS 10

LwipRelay::LwipRelay(WgTunnel* tunnel)
    : tunnel_(tunnel)
    , running_(false)
    , initialized_(false) {
    memset(&netif_, 0, sizeof(netif_));
    memset(&tunnelAddr_, 0, sizeof(tunnelAddr_));
    memset(&targetAddr_, 0, sizeof(targetAddr_));
}

LwipRelay::~LwipRelay() {
    stop();
}

bool LwipRelay::start(const std::string& tunnelIp, const std::string& targetIp) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_) {
        return false;
    }

    if (!initialized_) {
        lwip_init();
        initialized_ = true;
    }

    if (inet_pton(AF_INET, tunnelIp.c_str(), &tunnelAddr_) != 1) {
        brls::Logger::error("LwipRelay: invalid tunnel IP: {}", tunnelIp);
        return false;
    }

    if (inet_pton(AF_INET, targetIp.c_str(), &targetAddr_) != 1) {
        brls::Logger::error("LwipRelay: invalid target IP: {}", targetIp);
        return false;
    }

    ip4_addr_t netmask, gw;
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 0, 0, 0, 0);

    netif_add(&netif_, &tunnelAddr_, &netmask, &gw, tunnel_, wg_netif_init, ip_input);
    netif_set_default(&netif_);
    netif_set_up(&netif_);

    brls::Logger::info("LwipRelay: netif created with IP {}", tunnelIp);

    running_ = true;
    loopThread_ = std::thread(&LwipRelay::runLoop, this);

    return true;
}

void LwipRelay::stop() {
    running_ = false;

    if (loopThread_.joinable()) {
        loopThread_.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& pair : tcpListeners_) {
        close(pair.second);
    }
    tcpListeners_.clear();

    for (auto& pair : tcpConnections_) {
        if (pair.second->localSock >= 0) {
            close(pair.second->localSock);
        }
        if (pair.second->pcb) {
            tcp_abort(pair.second->pcb);
        }
    }
    tcpConnections_.clear();

    for (auto& pair : udpBindings_) {
        if (pair.second->localSock >= 0) {
            close(pair.second->localSock);
        }
        if (pair.second->pcb) {
            udp_remove(pair.second->pcb);
        }
    }
    udpBindings_.clear();

    if (netif_.flags & NETIF_FLAG_UP) {
        netif_set_down(&netif_);
        netif_remove(&netif_);
    }

    brls::Logger::info("LwipRelay: stopped");
}

uint16_t LwipRelay::startTcpRelay(uint16_t targetPort, uint16_t localPort) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (tcpListeners_.find(localPort) != tcpListeners_.end()) {
        return localPort;
    }

    int sock = createTcpListener(localPort);
    if (sock < 0) {
        return 0;
    }

    tcpListeners_[localPort] = sock;
    brls::Logger::info("LwipRelay: TCP listener on port {} -> {}", localPort, targetPort);
    return localPort;
}

uint16_t LwipRelay::startUdpRelay(uint16_t targetPort, uint16_t localPort) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (udpBindings_.find(localPort) != udpBindings_.end()) {
        return localPort;
    }

    int sock = createUdpSocket(localPort);
    if (sock < 0) {
        return 0;
    }

    struct udp_pcb* pcb = udp_new();
    if (!pcb) {
        close(sock);
        return 0;
    }

    ip_addr_t localAddr;
    ip_addr_copy_from_ip4(localAddr, tunnelAddr_);
    err_t err = udp_bind(pcb, &localAddr, localPort);
    if (err != ERR_OK) {
        brls::Logger::error("LwipRelay: udp_bind failed: {}", (int)err);
        udp_remove(pcb);
        close(sock);
        return 0;
    }

    auto binding = std::make_shared<UdpBinding>();
    binding->localSock = sock;
    binding->pcb = pcb;
    binding->localPort = localPort;
    binding->targetPort = targetPort;
    binding->hasClient = false;

    udp_recv(pcb, onUdpRecv, binding.get());
    udpBindings_[localPort] = binding;

    brls::Logger::info("LwipRelay: UDP socket on port {} -> {}", localPort, targetPort);
    return localPort;
}

void LwipRelay::handleIncomingPacket(const void* data, size_t len) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    incomingQueue_.emplace(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + len);
}

void LwipRelay::processIncomingQueue() {
    std::vector<std::vector<uint8_t>> packets;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (!incomingQueue_.empty()) {
            packets.push_back(std::move(incomingQueue_.front()));
            incomingQueue_.pop();
        }
    }
    for (const auto& pkt : packets) {
        wg_netif_input(&netif_, pkt.data(), pkt.size());
    }
}

void LwipRelay::tick() {
    sys_check_timeouts();
}

void LwipRelay::runLoop() {
    brls::Logger::info("LwipRelay: loop started");

    while (running_) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            processIncomingQueue();
            pollTcpListeners();
            pollTcpConnections();
            pollUdpSockets();
            sys_check_timeouts();
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    brls::Logger::info("LwipRelay: loop ended");
}

void LwipRelay::pollTcpListeners() {
    for (auto& pair : tcpListeners_) {
        uint16_t localPort = pair.first;
        int listenSock = pair.second;

        pollfd pfd;
        pfd.fd = listenSock;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int ret = poll(&pfd, 1, 0);
        if (ret <= 0 || !(pfd.revents & POLLIN)) {
            continue;
        }

        sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int clientSock = accept(listenSock, (sockaddr*)&clientAddr, &clientLen);
        if (clientSock < 0) {
            continue;
        }

        int flags = fcntl(clientSock, F_GETFL, 0);
        fcntl(clientSock, F_SETFL, flags | O_NONBLOCK);

        struct tcp_pcb* pcb = tcp_new();
        if (!pcb) {
            close(clientSock);
            continue;
        }

        auto conn = std::make_shared<TcpConnection>();
        conn->localSock = clientSock;
        conn->pcb = pcb;
        conn->localPort = localPort;
        conn->targetPort = localPort;
        conn->connected = false;

        tcp_arg(pcb, conn.get());
        tcp_err(pcb, onTcpError);

        tcpConnections_[clientSock] = conn;

        ip_addr_t destAddr;
        ip_addr_copy_from_ip4(destAddr, targetAddr_);

        brls::Logger::info("LwipRelay: new TCP connection, connecting to PS5 port {}", localPort);

        err_t err = tcp_connect(pcb, &destAddr, localPort, onTcpConnected);
        if (err != ERR_OK) {
            brls::Logger::error("LwipRelay: tcp_connect failed: {}", (int)err);
            tcp_abort(pcb);
            close(clientSock);
            tcpConnections_.erase(clientSock);
        }
    }
}

void LwipRelay::pollTcpConnections() {
    std::vector<int> toRemove;

    for (auto& pair : tcpConnections_) {
        int sock = pair.first;
        auto& conn = pair.second;

        if (!conn->connected) {
            continue;
        }

        pollfd pfd;
        pfd.fd = sock;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int ret = poll(&pfd, 1, 0);
        if (ret < 0) {
            toRemove.push_back(sock);
            continue;
        }

        if (ret > 0 && (pfd.revents & POLLIN)) {
            uint8_t buf[2048];
            ssize_t received = recv(sock, buf, sizeof(buf), 0);

            if (received <= 0) {
                if (received == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                    brls::Logger::info("LwipRelay: local socket closed");
                    if (conn->pcb) {
                        tcp_close(conn->pcb);
                        conn->pcb = nullptr;
                    }
                    toRemove.push_back(sock);
                }
                continue;
            }

            if (conn->pcb) {
                err_t err = tcp_write(conn->pcb, buf, (u16_t)received, TCP_WRITE_FLAG_COPY);
                if (err == ERR_OK) {
                    tcp_output(conn->pcb);
                    brls::Logger::info("LwipRelay: sent {} bytes to PS5", received);
                } else {
                    brls::Logger::error("LwipRelay: tcp_write failed: {}", (int)err);
                }
            }
        }

        if (pfd.revents & (POLLERR | POLLHUP)) {
            toRemove.push_back(sock);
        }
    }

    for (int sock : toRemove) {
        auto it = tcpConnections_.find(sock);
        if (it != tcpConnections_.end()) {
            if (it->second->localSock >= 0) {
                close(it->second->localSock);
            }
            if (it->second->pcb) {
                tcp_abort(it->second->pcb);
            }
            tcpConnections_.erase(it);
        }
    }
}

void LwipRelay::pollUdpSockets() {
    for (auto& pair : udpBindings_) {
        auto& binding = pair.second;

        pollfd pfd;
        pfd.fd = binding->localSock;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int ret = poll(&pfd, 1, 0);
        if (ret <= 0 || !(pfd.revents & POLLIN)) {
            continue;
        }

        uint8_t buf[2048];
        sockaddr_in fromAddr;
        socklen_t fromLen = sizeof(fromAddr);

        ssize_t received = recvfrom(binding->localSock, buf, sizeof(buf), 0,
                                    (sockaddr*)&fromAddr, &fromLen);
        if (received <= 0) {
            continue;
        }

        binding->clientAddr = fromAddr;
        binding->hasClient = true;

        struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)received, PBUF_RAM);
        if (!p) {
            continue;
        }

        memcpy(p->payload, buf, received);

        ip_addr_t destAddr;
        ip_addr_copy_from_ip4(destAddr, targetAddr_);

        udp_sendto(binding->pcb, p, &destAddr, binding->targetPort);
        pbuf_free(p);

        brls::Logger::info("LwipRelay: sent {} UDP bytes to PS5 port {}", received, binding->targetPort);
    }
}

int LwipRelay::createTcpListener(uint16_t port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    if (listen(sock, 5) < 0) {
        close(sock);
        return -1;
    }

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    return sock;
}

int LwipRelay::createUdpSocket(uint16_t port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return -1;
    }

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    int rcvbuf = 0x19000;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    return sock;
}

err_t LwipRelay::onTcpConnected(void* arg, struct tcp_pcb* pcb, err_t err) {
    TcpConnection* conn = (TcpConnection*)arg;
    if (!conn) {
        return ERR_ARG;
    }

    if (err != ERR_OK) {
        brls::Logger::error("LwipRelay: TCP connect failed: {}", (int)err);
        return err;
    }

    brls::Logger::info("LwipRelay: TCP connected to PS5 port {}", conn->targetPort);

    conn->connected = true;
    tcp_recv(pcb, onTcpRecv);
    tcp_sent(pcb, onTcpSent);

    return ERR_OK;
}

err_t LwipRelay::onTcpRecv(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err) {
    TcpConnection* conn = (TcpConnection*)arg;
    if (!conn) {
        if (p) pbuf_free(p);
        return ERR_ARG;
    }

    if (!p || err != ERR_OK) {
        brls::Logger::info("LwipRelay: PS5 closed TCP connection");
        if (conn->localSock >= 0) {
            close(conn->localSock);
            conn->localSock = -1;
        }
        return ERR_OK;
    }

    ssize_t sent = send(conn->localSock, p->payload, p->len, 0);
    (void)sent;

    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    return ERR_OK;
}

err_t LwipRelay::onTcpSent(void* arg, struct tcp_pcb* pcb, u16_t len) {
    (void)arg;
    (void)pcb;
    (void)len;
    return ERR_OK;
}

void LwipRelay::onTcpError(void* arg, err_t err) {
    TcpConnection* conn = (TcpConnection*)arg;
    if (conn) {
        brls::Logger::error("LwipRelay: TCP error: {}", (int)err);
        conn->pcb = nullptr;
    }
}

void LwipRelay::onUdpRecv(void* arg, struct udp_pcb* pcb, struct pbuf* p,
                          const ip_addr_t* addr, u16_t port) {
    (void)pcb;
    (void)addr;
    (void)port;

    UdpBinding* binding = (UdpBinding*)arg;
    if (!binding || !p) {
        if (p) pbuf_free(p);
        return;
    }

    if (!binding->hasClient) {
        pbuf_free(p);
        return;
    }

    ssize_t sent = sendto(binding->localSock, p->payload, p->len, 0,
                          (sockaddr*)&binding->clientAddr, sizeof(binding->clientAddr));
    (void)sent;

    pbuf_free(p);
}
