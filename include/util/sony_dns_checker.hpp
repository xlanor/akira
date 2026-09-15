#ifndef AKIRA_SONY_DNS_CHECKER_HPP
#define AKIRA_SONY_DNS_CHECKER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace akira::net {

inline constexpr std::array<const char*, 3> SonyDnsDomains = {
    "auth.api.sonyentertainmentnetwork.com",
    "m.np.playstation.com",
    "psnow.playstation.com",
};

enum class DnsVerdict { Inconclusive, Reachable, Blocked };

struct DnsServerStatus {
    std::string address;
    std::array<DnsVerdict, 3> domains{};

    int failedCount() const {
        int count = 0;
        for (DnsVerdict verdict : domains)
            count += verdict != DnsVerdict::Reachable;
        return count;
    }

    bool hasSonyFailure() const { return failedCount() > 0; }
};

struct SonyDnsReport {
    std::vector<DnsServerStatus> servers;

    bool failed() const {
        for (const DnsServerStatus& server : servers)
            if (server.hasSonyFailure())
                return true;
        return false;
    }
};

inline bool isBlockedSonyIpv4(const uint8_t* address)
{
    return address && (address[0] == 127 ||
        (address[0] == 0 && address[1] == 0 && address[2] == 0 && address[3] == 0));
}

inline bool skipDnsName(const uint8_t* packet, size_t length, size_t& offset)
{
    // DNS compression pointers do not need to be followed to skip a name.
    for (size_t labels = 0; labels < 128; ++labels) {
        if (offset >= length)
            return false;
        const uint8_t label = packet[offset++];
        if ((label & 0xc0) == 0xc0) {
            if (offset >= length)
                return false;
            ++offset;
            return true;
        }
        if ((label & 0xc0) != 0)
            return false;
        if (label == 0)
            return true;
        if (offset + label > length)
            return false;
        offset += label;
    }
    return false;
}

inline DnsVerdict classifyDnsAResponse(const uint8_t* packet, size_t length, uint16_t expectedId)
{
    if (!packet || length < 12)
        return DnsVerdict::Inconclusive;
    const uint16_t id = static_cast<uint16_t>((packet[0] << 8) | packet[1]);
    const uint16_t flags = static_cast<uint16_t>((packet[2] << 8) | packet[3]);
    if (id != expectedId || (flags & 0x8000) == 0 || (flags & 0x0200) != 0 ||
        (flags & 0x000f) != 0)
        return DnsVerdict::Inconclusive;

    const uint16_t questions = static_cast<uint16_t>((packet[4] << 8) | packet[5]);
    const uint16_t answers = static_cast<uint16_t>((packet[6] << 8) | packet[7]);
    if (questions != 1 || answers > 32)
        return DnsVerdict::Inconclusive;

    size_t offset = 12;
    if (!skipDnsName(packet, length, offset) || offset + 4 > length)
        return DnsVerdict::Inconclusive;
    offset += 4; // QTYPE and QCLASS

    bool sawBlocked = false;
    bool sawReachable = false;
    for (uint16_t i = 0; i < answers; ++i) {
        if (!skipDnsName(packet, length, offset) || offset + 10 > length)
            return DnsVerdict::Inconclusive;
        const uint16_t type = static_cast<uint16_t>((packet[offset] << 8) | packet[offset + 1]);
        const uint16_t klass = static_cast<uint16_t>((packet[offset + 2] << 8) | packet[offset + 3]);
        const uint16_t dataLength = static_cast<uint16_t>((packet[offset + 8] << 8) | packet[offset + 9]);
        offset += 10;
        if (offset + dataLength > length)
            return DnsVerdict::Inconclusive;
        if (type == 1 && klass == 1 && dataLength == 4) {
            if (isBlockedSonyIpv4(packet + offset))
                sawBlocked = true;
            else
                sawReachable = true;
        }
        offset += dataLength;
    }

    if (sawReachable)
        return DnsVerdict::Reachable;
    if (sawBlocked)
        return DnsVerdict::Blocked;
    return DnsVerdict::Inconclusive;
}

inline int secondsUntilDnsWarningOk(int64_t elapsedMilliseconds)
{
    if (elapsedMilliseconds >= 5000)
        return 0;
    if (elapsedMilliseconds < 0)
        elapsedMilliseconds = 0;
    return static_cast<int>((5000 - elapsedMilliseconds + 999) / 1000);
}

// Runs on a worker thread; only numeric DNS servers configured on the Switch
// are queried. It makes no HTTP requests and sends no account data.
SonyDnsReport checkSonyDnsAtStartup();

} // namespace akira::net

#endif // AKIRA_SONY_DNS_CHECKER_HPP
