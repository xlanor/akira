#include "core/stun_client.hpp"

#include <cstring>
#include <cstdlib>
#include <ctime>
#include <sstream>

#include <switch.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <curl/curl.h>

static const char* STUN_SERVER_1 = "stun.l.google.com";
static const char* STUN_SERVER_2 = "stun1.l.google.com";
static const uint16_t STUN_PORT = 19302;

static const uint16_t STUN_BINDING_REQUEST = 0x0001;
static const uint16_t STUN_BINDING_RESPONSE = 0x0101;
static const uint32_t STUN_MAGIC_COOKIE = 0x2112A442;
static const uint16_t STUN_ATTR_MAPPED_ADDRESS = 0x0001;
static const uint16_t STUN_ATTR_XOR_MAPPED_ADDRESS = 0x0020;
static const uint16_t STUN_ATTR_CHANGE_REQUEST = 0x0003;
static const uint16_t STUN_ATTR_OTHER_ADDRESS = 0x802C;

static const char* RFC5780_SERVER_LIST_URL = "https://raw.githubusercontent.com/pradt2/always-online-stun/master/valid_nat_testing_hosts.txt";

std::string StunClient::natTypeToString(NATType type) {
    switch (type) {
        case NATType::OpenInternet: return "Open Internet";
        case NATType::FullCone: return "Endpoint-Independent";
        case NATType::RestrictedCone: return "Endpoint-Independent";
        case NATType::PortRestrictedCone: return "Endpoint-Independent";
        case NATType::Symmetric: return "Address and Port-Dependent";
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

std::string StunClient::filteringTypeToString(FilteringType type) {
    switch (type) {
        case FilteringType::EndpointIndependent: return "Endpoint-Independent";
        case FilteringType::AddressDependent: return "Address-Dependent";
        case FilteringType::AddressPortDependent: return "Address and Port-Dependent";
        default: return "Unknown";
    }
}

static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

std::vector<std::string> StunClient::fetchRFC5780Servers() {
    std::vector<std::string> servers;

    CURL* curl = curl_easy_init();
    if (!curl) {
        return servers;
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, RFC5780_SERVER_LIST_URL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return servers;
    }

    std::istringstream stream(response);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line[0] != '#') {
            size_t start = line.find_first_not_of(" \t\r\n");
            size_t end = line.find_last_not_of(" \t\r\n");
            if (start != std::string::npos) {
                servers.push_back(line.substr(start, end - start + 1));
            }
        }
    }

    return servers;
}

FilteringType StunClient::detectFiltering() {
    std::vector<std::string> servers = fetchRFC5780Servers();

    for (const auto& server : servers) {
        FilteringType filtering = testFilteringWithServer(server);
        if (filtering != FilteringType::Unknown) {
            return filtering;
        }
    }

    return FilteringType::Unknown;
}

FilteringType StunClient::testFilteringWithServer(const std::string& server) {
    size_t colonPos = server.find(':');
    if (colonPos == std::string::npos) {
        return FilteringType::Unknown;
    }

    std::string host = server.substr(0, colonPos);
    uint16_t port = static_cast<uint16_t>(std::stoi(server.substr(colonPos + 1)));

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return FilteringType::Unknown;
    }

    struct sockaddr_in localAddr;
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = INADDR_ANY;
    localAddr.sin_port = 0;

    if (bind(sock, (struct sockaddr*)&localAddr, sizeof(localAddr)) < 0) {
        close(sock);
        return FilteringType::Unknown;
    }

    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct addrinfo hints, *serverInfo;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    char portStr[6];
    snprintf(portStr, sizeof(portStr), "%d", port);

    if (getaddrinfo(host.c_str(), portStr, &hints, &serverInfo) != 0) {
        close(sock);
        return FilteringType::Unknown;
    }

    uint8_t request[20];
    memset(request, 0, sizeof(request));
    request[0] = (STUN_BINDING_REQUEST >> 8) & 0xFF;
    request[1] = STUN_BINDING_REQUEST & 0xFF;
    request[4] = (STUN_MAGIC_COOKIE >> 24) & 0xFF;
    request[5] = (STUN_MAGIC_COOKIE >> 16) & 0xFF;
    request[6] = (STUN_MAGIC_COOKIE >> 8) & 0xFF;
    request[7] = STUN_MAGIC_COOKIE & 0xFF;

    srand(time(nullptr));
    for (int i = 8; i < 20; i++) {
        request[i] = rand() & 0xFF;
    }

    ssize_t sent = sendto(sock, request, sizeof(request), 0, serverInfo->ai_addr, serverInfo->ai_addrlen);
    if (sent != sizeof(request)) {
        freeaddrinfo(serverInfo);
        close(sock);
        return FilteringType::Unknown;
    }

    uint8_t response[512];
    ssize_t received = recv(sock, response, sizeof(response), 0);

    if (received < 20) {
        freeaddrinfo(serverInfo);
        close(sock);
        return FilteringType::Unknown;
    }

    MappedAddress mapped;
    OtherAddress other;
    other.valid = false;

    if (!parseBindingResponse(response, received, mapped, &other)) {
        freeaddrinfo(serverInfo);
        close(sock);
        return FilteringType::Unknown;
    }

    if (!other.valid) {
        freeaddrinfo(serverInfo);
        close(sock);
        return FilteringType::Unknown;
    }

    uint8_t changeRequest[28];
    memset(changeRequest, 0, sizeof(changeRequest));
    changeRequest[0] = (STUN_BINDING_REQUEST >> 8) & 0xFF;
    changeRequest[1] = STUN_BINDING_REQUEST & 0xFF;
    changeRequest[2] = 0;
    changeRequest[3] = 8;
    changeRequest[4] = (STUN_MAGIC_COOKIE >> 24) & 0xFF;
    changeRequest[5] = (STUN_MAGIC_COOKIE >> 16) & 0xFF;
    changeRequest[6] = (STUN_MAGIC_COOKIE >> 8) & 0xFF;
    changeRequest[7] = STUN_MAGIC_COOKIE & 0xFF;

    for (int i = 8; i < 20; i++) {
        changeRequest[i] = rand() & 0xFF;
    }

    changeRequest[20] = (STUN_ATTR_CHANGE_REQUEST >> 8) & 0xFF;
    changeRequest[21] = STUN_ATTR_CHANGE_REQUEST & 0xFF;
    changeRequest[22] = 0;
    changeRequest[23] = 4;
    changeRequest[24] = 0;
    changeRequest[25] = 0;
    changeRequest[26] = 0;
    changeRequest[27] = 0x04;

    sent = sendto(sock, changeRequest, sizeof(changeRequest), 0, serverInfo->ai_addr, serverInfo->ai_addrlen);
    freeaddrinfo(serverInfo);

    if (sent != sizeof(changeRequest)) {
        close(sock);
        return FilteringType::Unknown;
    }

    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in fromAddr;
    socklen_t fromLen = sizeof(fromAddr);
    received = recvfrom(sock, response, sizeof(response), 0, (struct sockaddr*)&fromAddr, &fromLen);
    close(sock);

    if (received < 20) {
        return FilteringType::AddressDependent;
    }

    char fromIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &fromAddr.sin_addr, fromIP, sizeof(fromIP));

    if (std::string(fromIP) != host) {
        return FilteringType::EndpointIndependent;
    }

    return FilteringType::AddressDependent;
}

StunResult StunClient::detectNATType() {
    StunResult result;
    result.type = NATType::Unknown;
    result.filtering = FilteringType::Unknown;
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
    } else if (addr1.port == addr2.port) {
        result.type = NATType::FullCone;
    } else {
        result.type = NATType::Symmetric;
    }

    result.filtering = detectFiltering();

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

bool StunClient::parseBindingResponse(const uint8_t* buffer, size_t length, MappedAddress& result, OtherAddress* otherAddr) {
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
        } else if (attrType == STUN_ATTR_OTHER_ADDRESS && attrLength >= 8 && otherAddr) {
            uint8_t family = buffer[offset + 5];
            if (family == 0x01) {
                otherAddr->port = (buffer[offset + 6] << 8) | buffer[offset + 7];

                uint32_t addr = (buffer[offset + 8] << 24) | (buffer[offset + 9] << 16) |
                                (buffer[offset + 10] << 8) | buffer[offset + 11];

                char ipStr[INET_ADDRSTRLEN];
                struct in_addr inAddr;
                inAddr.s_addr = htonl(addr);
                inet_ntop(AF_INET, &inAddr, ipStr, sizeof(ipStr));
                otherAddr->ip = ipStr;
                otherAddr->valid = true;
            }
        }

        offset += 4 + attrLength;
        if (attrLength % 4 != 0) {
            offset += 4 - (attrLength % 4);
        }
    }

    return result.valid;
}
