#ifndef AKIRA_PSN_AUTH_BOOTSTRAP_HPP
#define AKIRA_PSN_AUTH_BOOTSTRAP_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace psn::bootstrap {

enum class Credential {
    RemotePlay,
    MobileSso
};

struct NpssoState {
    bool hasNpsso = false;
    bool valid = false;
    int64_t lastCheckedAt = 0;
};

std::string urlEncode(std::string_view value);
std::string buildAuthorizeUrl(Credential credential, std::string_view duid, std::string_view cid);
std::string buildCodeExchangeBody(Credential credential, std::string_view code);
std::string extractAuthCode(std::string_view location);
bool shouldValidateNpsso(const NpssoState& state, int64_t now, int64_t intervalSeconds, bool force);

} // namespace psn::bootstrap

#endif // AKIRA_PSN_AUTH_BOOTSTRAP_HPP
