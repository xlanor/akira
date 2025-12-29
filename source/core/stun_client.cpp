#include "core/stun_client.hpp"

#include <cstring>
#include <cstdlib>
#include <ctime>

#include <switch.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

static const char* STUN_SERVER_1 = "stun.l.google.com";
static const char* STUN_SERVER_2 = "stun1.l.google.com";
static const uint16_t STUN_PORT = 19302;

static const uint16_t STUN_BINDING_REQUEST = 0x0001;
static const uint16_t STUN_BINDING_RESPONSE = 0x0101;
static const uint32_t STUN_MAGIC_COOKIE = 0x2112A442;
static const uint16_t STUN_ATTR_MAPPED_ADDRESS = 0x0001;
static const uint16_t STUN_ATTR_XOR_MAPPED_ADDRESS = 0x0020;

std::string StunClient::natTypeToString(NATType type) {
    switch (type) {
        case NATType::OpenInternet: return "Open Internet";
        case NATType::FullCone: return "Endpoint-Independent Mapping";
        case NATType::RestrictedCone: return "Endpoint-Independent Mapping";
        case NATType::PortRestrictedCone: return "Endpoint-Independent Mapping";
        case NATType::Symmetric: return "Address & Port-Dependent Mapping";
        case NATType::UDPBlocked: return "UDP Blocked";
        default: return "Unknown";
    }
}

std::string StunClient::natTypeDescription(NATType type) {
    switch (type) {
        case NATType::OpenInternet:
            return "Direct connection, no NAT traversal needed";
        case NATType::FullCone:
        case NATType::RestrictedCone:
        case NATType::PortRestrictedCone:
            return "Same external port for all destinations, holepunch works";
        case NATType::Symmetric:
            return "External port changes per destination, holepunch difficult";
        case NATType::UDPBlocked:
            return "UDP traffic is blocked, remote play may not work";
        default:
            return "Could not determine NAT type";
    }
}

StunResult StunClient::detectNATType() {
    StunResult result;
    result.type = NATType::Unknown;
    result.externalPort = 0;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        result.error = "Failed to create socket";
        return result;
    }

    struct sockaddr_in localAddr;
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = INADDR_ANY;
    localAddr.sin_port = 0;

    if (bind(sock, (struct sockaddr*)&localAddr, sizeof(localAddr)) < 0) {
        close(sock);
        result.error = "Failed to bind socket";
        return result;
    }

    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    MappedAddress addr1 = sendBindingRequest(STUN_SERVER_1, STUN_PORT, sock);
    if (!addr1.valid) {
        close(sock);
        result.type = NATType::UDPBlocked;
        result.error = "No response from STUN server";
        return result;
    }

    result.externalIP = addr1.ip;
    result.externalPort = addr1.port;

    MappedAddress addr2 = sendBindingRequest(STUN_SERVER_2, STUN_PORT, sock);
    close(sock);

    if (!addr2.valid) {
        result.type = NATType::FullCone;
        return result;
    }

    if (addr1.port == addr2.port) {
        result.type = NATType::FullCone;
    } else {
        result.type = NATType::Symmetric;
    }

    return result;
}

StunClient::MappedAddress StunClient::sendBindingRequest(const char* stunServer, uint16_t stunPort, int localSocket) {
    MappedAddress result;
    result.valid = false;
    result.port = 0;

    struct addrinfo hints, *serverInfo;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    char portStr[6];
    snprintf(portStr, sizeof(portStr), "%d", stunPort);

    if (getaddrinfo(stunServer, portStr, &hints, &serverInfo) != 0) {
        return result;
    }

    uint8_t request[20];
    memset(request, 0, sizeof(request));

    request[0] = (STUN_BINDING_REQUEST >> 8) & 0xFF;
    request[1] = STUN_BINDING_REQUEST & 0xFF;

    request[2] = 0;
    request[3] = 0;

    request[4] = (STUN_MAGIC_COOKIE >> 24) & 0xFF;
    request[5] = (STUN_MAGIC_COOKIE >> 16) & 0xFF;
    request[6] = (STUN_MAGIC_COOKIE >> 8) & 0xFF;
    request[7] = STUN_MAGIC_COOKIE & 0xFF;

    srand(time(nullptr));
    for (int i = 8; i < 20; i++) {
        request[i] = rand() & 0xFF;
    }

    ssize_t sent = sendto(localSocket, request, sizeof(request), 0,
                          serverInfo->ai_addr, serverInfo->ai_addrlen);
    freeaddrinfo(serverInfo);

    if (sent != sizeof(request)) {
        return result;
    }

    uint8_t response[512];
    ssize_t received = recv(localSocket, response, sizeof(response), 0);

    if (received < 20) {
        return result;
    }

    parseBindingResponse(response, received, result);
    return result;
}

bool StunClient::parseBindingResponse(const uint8_t* buffer, size_t length, MappedAddress& result) {
    if (length < 20) {
        return false;
    }

    uint16_t msgType = (buffer[0] << 8) | buffer[1];
    if (msgType != STUN_BINDING_RESPONSE) {
        return false;
    }

    uint16_t msgLength = (buffer[2] << 8) | buffer[3];
    if (length < 20 + msgLength) {
        return false;
    }

    size_t offset = 20;
    while (offset + 4 <= 20 + msgLength) {
        uint16_t attrType = (buffer[offset] << 8) | buffer[offset + 1];
        uint16_t attrLength = (buffer[offset + 2] << 8) | buffer[offset + 3];

        if (offset + 4 + attrLength > length) {
            break;
        }

        if (attrType == STUN_ATTR_XOR_MAPPED_ADDRESS && attrLength >= 8) {
            uint8_t family = buffer[offset + 5];
            if (family == 0x01) {
                uint16_t xorPort = (buffer[offset + 6] << 8) | buffer[offset + 7];
                result.port = xorPort ^ (STUN_MAGIC_COOKIE >> 16);

                uint32_t xorAddr = (buffer[offset + 8] << 24) | (buffer[offset + 9] << 16) |
                                   (buffer[offset + 10] << 8) | buffer[offset + 11];
                uint32_t addr = xorAddr ^ STUN_MAGIC_COOKIE;

                char ipStr[INET_ADDRSTRLEN];
                struct in_addr inAddr;
                inAddr.s_addr = htonl(addr);
                inet_ntop(AF_INET, &inAddr, ipStr, sizeof(ipStr));
                result.ip = ipStr;
                result.valid = true;
                return true;
            }
        } else if (attrType == STUN_ATTR_MAPPED_ADDRESS && attrLength >= 8 && !result.valid) {
            uint8_t family = buffer[offset + 5];
            if (family == 0x01) {
                result.port = (buffer[offset + 6] << 8) | buffer[offset + 7];

                uint32_t addr = (buffer[offset + 8] << 24) | (buffer[offset + 9] << 16) |
                                (buffer[offset + 10] << 8) | buffer[offset + 11];

                char ipStr[INET_ADDRSTRLEN];
                struct in_addr inAddr;
                inAddr.s_addr = htonl(addr);
                inet_ntop(AF_INET, &inAddr, ipStr, sizeof(ipStr));
                result.ip = ipStr;
                result.valid = true;
            }
        }

        offset += 4 + attrLength;
        if (attrLength % 4 != 0) {
            offset += 4 - (attrLength % 4);
        }
    }

    return result.valid;
}
