#include "util/sony_dns_checker.hpp"

#include <borealis.hpp>
#include <switch.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>

namespace akira::net {
namespace {

std::string ipv4String(uint32_t value)
{
    in_addr address{};
    address.s_addr = value;
    char text[INET_ADDRSTRLEN]{};
    return inet_ntop(AF_INET, &address, text, sizeof(text)) ? text : "?";
}

bool encodeQuestion(std::array<uint8_t, 512>& query, size_t& length,
    uint16_t id, const char* domain)
{
    query.fill(0);
    query[0] = static_cast<uint8_t>(id >> 8);
    query[1] = static_cast<uint8_t>(id);
    query[2] = 0x01; // Recursion desired.
    query[5] = 0x01; // One question.
    length = 12;

    const char* label = domain;
    while (*label) {
        const char* end = std::strchr(label, '.');
        const size_t labelLength = end ? static_cast<size_t>(end - label) : std::strlen(label);
        if (labelLength == 0 || labelLength > 63 || length + labelLength + 6 > query.size())
            return false;
        query[length++] = static_cast<uint8_t>(labelLength);
        std::memcpy(query.data() + length, label, labelLength);
        length += labelLength;
        if (!end)
            break;
        label = end + 1;
    }

    query[length++] = 0;
    query[length++] = 0;
    query[length++] = 1; // A record.
    query[length++] = 0;
    query[length++] = 1; // IN class.
    return true;
}

DnsVerdict queryServer(uint32_t serverIp, const char* domain)
{
    const auto fail = [serverIp, domain](const char* step, int osErrno = 0) {
        brls::Logger::error(
            "[NET] dns_probe_fail domain={} server={} step={} os_errno={} os_error=\"{}\"",
            domain, ipv4String(serverIp), step, osErrno,
            osErrno ? std::strerror(osErrno) : "-");
        return DnsVerdict::Inconclusive;
    };

    static std::atomic<uint16_t> nextId{0x5a00};
    const uint16_t id = nextId.fetch_add(1, std::memory_order_relaxed);
    std::array<uint8_t, 512> query{};
    size_t queryLength = 0;
    if (!encodeQuestion(query, queryLength, id, domain))
        return fail("encode_question");

    const int socketFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketFd < 0)
        return fail("socket", errno);

    const timeval timeout{ .tv_sec = 0, .tv_usec = 500000 };
    if (setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
        setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
        const int socketErrno = errno;
        close(socketFd);
        return fail("socket_timeout", socketErrno);
    }

    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(53);
    destination.sin_addr.s_addr = serverIp;

    const ssize_t sent = sendto(socketFd, query.data(), queryLength, 0,
        reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
    if (sent != static_cast<ssize_t>(queryLength)) {
        const int sendErrno = sent < 0 ? errno : 0;
        close(socketFd);
        return fail("send_query", sendErrno);
    }

    std::array<uint8_t, 512> response{};
    sockaddr_in source{};
    socklen_t sourceLength = sizeof(source);
    const ssize_t received = recvfrom(socketFd, response.data(), response.size(), 0,
        reinterpret_cast<sockaddr*>(&source), &sourceLength);
    const int receiveErrno = received < 0 ? errno : 0;
    close(socketFd);
    if (received <= 0)
        return fail("receive_answer", receiveErrno);
    if (sourceLength < sizeof(sockaddr_in) ||
        source.sin_family != AF_INET || source.sin_addr.s_addr != serverIp ||
        source.sin_port != htons(53))
        return fail("unexpected_answer_source");
    const DnsVerdict verdict = classifyDnsAResponse(
        response.data(), static_cast<size_t>(received), id);
    if (verdict == DnsVerdict::Blocked)
        brls::Logger::error("[NET] dns_probe_fail domain={} server={} step=blocked_answer",
            domain, ipv4String(serverIp));
    else if (verdict == DnsVerdict::Inconclusive)
        return fail("invalid_or_empty_answer");
    return verdict;
}

const char* verdictName(DnsVerdict verdict)
{
    switch (verdict) {
        case DnsVerdict::Blocked: return "blocked";
        case DnsVerdict::Reachable: return "reachable";
        default: return "inconclusive";
    }
}

} // namespace

SonyDnsReport checkSonyDnsAtStartup()
{
    SonyDnsReport report;
    uint32_t ip = 0;
    uint32_t mask = 0;
    uint32_t gateway = 0;
    uint32_t dns1 = 0;
    uint32_t dns2 = 0;
    const Result configResult = nifmGetCurrentIpConfigInfo(&ip, &mask, &gateway, &dns1, &dns2);
    if (R_FAILED(configResult) || ip == 0) {
        brls::Logger::info("[NET] dns_probe skipped: no connected IP configuration (rc=0x{:08x})",
            static_cast<unsigned int>(configResult));
        return report;
    }

    const std::array<uint32_t, 2> dnsServers = {dns1, dns2};
    for (size_t serverIndex = 0; serverIndex < dnsServers.size(); ++serverIndex) {
        const uint32_t serverIp = dnsServers[serverIndex];
        if (serverIp == 0 || (serverIndex == 1 && dns1 == dns2))
            continue;

        DnsServerStatus server;
        server.address = ipv4String(serverIp);
        for (size_t i = 0; i < SonyDnsDomains.size(); ++i)
            server.domains[i] = queryServer(serverIp, SonyDnsDomains[i]);

        brls::Logger::info(
            "[NET] dns_probe server={} auth={} profile={} cloud={} failed_domains={}",
            server.address, verdictName(server.domains[0]), verdictName(server.domains[1]),
            verdictName(server.domains[2]), server.failedCount());
        report.servers.push_back(std::move(server));
    }
    return report;
}

} // namespace akira::net
