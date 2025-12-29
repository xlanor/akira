#ifndef AKIRA_STUN_CLIENT_HPP
#define AKIRA_STUN_CLIENT_HPP

#include <string>
#include <cstdint>

enum class NATType {
    Unknown,
    OpenInternet,
    FullCone,
    RestrictedCone,
    PortRestrictedCone,
    Symmetric,
    UDPBlocked
};

struct StunResult {
    NATType type;
    std::string externalIP;
    uint16_t externalPort;
    std::string error;
};

class StunClient {
public:
    static StunResult detectNATType();
    static std::string natTypeToString(NATType type);
    static std::string natTypeDescription(NATType type);

private:
    struct MappedAddress {
        std::string ip;
        uint16_t port;
        bool valid;
    };

    static MappedAddress sendBindingRequest(const char* stunServer, uint16_t stunPort, int localSocket);
    static bool parseBindingResponse(const uint8_t* buffer, size_t length, MappedAddress& result);
};

#endif
