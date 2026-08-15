#include "psn/auth_bootstrap.hpp"

#include <array>
#include <sstream>

namespace psn::bootstrap {

namespace {

constexpr const char* REMOTE_CLIENT_ID = "ba495a24-818c-472b-b12d-ff231c1b5745";
constexpr const char* REMOTE_CLIENT_SECRET = "mvaiZkRsAsI1IBkY";
constexpr const char* REMOTE_REDIRECT_URI = "https://remoteplay.dl.playstation.net/remoteplay/redirect";
constexpr const char* REMOTE_SCOPES =
    "psn:clientapp referenceDataService:countryConfig.read "
    "pushNotification:webSocket.desktop.connect "
    "sessionManager:remotePlaySession.system.update";

constexpr const char* MOBILE_CLIENT_ID = "09515159-7237-4370-9b40-3806e67c0891";
constexpr const char* MOBILE_REDIRECT_URI = "com.scee.psxandroid.scecompcall://redirect";
constexpr const char* MOBILE_SCOPES = "psn:mobile.v2.core psn:clientapp";

constexpr const char* AUTHORIZE_URL = "https://ca.account.sony.com/api/authz/v3/oauth/authorize";

bool isUnreserved(unsigned char ch)
{
    return (ch >= 'A' && ch <= 'Z') ||
        (ch >= 'a' && ch <= 'z') ||
        (ch >= '0' && ch <= '9') ||
        ch == '-' || ch == '_' || ch == '.' || ch == '~';
}

void appendParam(std::ostringstream& out, bool& first, std::string_view key, std::string_view value)
{
    out << (first ? '?' : '&') << key << '=' << urlEncode(value);
    first = false;
}

} // namespace

std::string urlEncode(std::string_view value)
{
    static constexpr std::array<char, 16> HEX = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
    };

    std::string encoded;
    encoded.reserve(value.size() * 3);

    for (unsigned char ch : value)
    {
        if (isUnreserved(ch))
        {
            encoded.push_back(static_cast<char>(ch));
            continue;
        }

        encoded.push_back('%');
        encoded.push_back(HEX[(ch >> 4) & 0x0F]);
        encoded.push_back(HEX[ch & 0x0F]);
    }

    return encoded;
}

std::string buildAuthorizeUrl(Credential credential, std::string_view duid, std::string_view cid)
{
    std::ostringstream out;
    out << AUTHORIZE_URL;

    bool first = true;
    if (credential == Credential::MobileSso)
    {
        appendParam(out, first, "access_type", "offline");
        appendParam(out, first, "client_id", MOBILE_CLIENT_ID);
        appendParam(out, first, "redirect_uri", MOBILE_REDIRECT_URI);
        appendParam(out, first, "response_type", "code");
        appendParam(out, first, "scope", MOBILE_SCOPES);
        return out.str();
    }

    appendParam(out, first, "client_id", REMOTE_CLIENT_ID);
    appendParam(out, first, "redirect_uri", REMOTE_REDIRECT_URI);
    appendParam(out, first, "scope", REMOTE_SCOPES);
    appendParam(out, first, "response_type", "code");
    appendParam(out, first, "service_entity", "urn:service-entity:psn");
    appendParam(out, first, "access_type", "offline");
    appendParam(out, first, "duid", duid);
    appendParam(out, first, "smcid", "remoteplay");
    appendParam(out, first, "layout_type", "popup");
    appendParam(out, first, "PlatformPrivacyWs1", "minimal");
    appendParam(out, first, "no_captcha", "true");
    appendParam(out, first, "cid", cid);
    return out.str();
}

std::string buildCodeExchangeBody(Credential credential, std::string_view code)
{
    std::ostringstream out;
    out << "grant_type=authorization_code"
        << "&code=" << urlEncode(code);

    if (credential == Credential::MobileSso)
    {
        out << "&redirect_uri=" << urlEncode(MOBILE_REDIRECT_URI)
            << "&token_format=jwt";
        return out.str();
    }

    out << "&client_id=" << urlEncode(REMOTE_CLIENT_ID)
        << "&client_secret=" << urlEncode(REMOTE_CLIENT_SECRET)
        << "&redirect_uri=" << urlEncode(REMOTE_REDIRECT_URI)
        << "&scope=" << urlEncode(REMOTE_SCOPES);
    return out.str();
}

std::string extractAuthCode(std::string_view location)
{
    if (location.empty())
        return {};

    size_t query = location.find('?');
    if (query == std::string_view::npos)
        return {};

    std::string_view rest = location.substr(query + 1);
    while (!rest.empty())
    {
        size_t amp = rest.find('&');
        std::string_view part = rest.substr(0, amp);
        size_t eq = part.find('=');
        std::string_view key = part.substr(0, eq);
        std::string_view value = eq == std::string_view::npos ? std::string_view{} : part.substr(eq + 1);

        if (key == "code")
            return std::string(value);

        if (amp == std::string_view::npos)
            break;
        rest.remove_prefix(amp + 1);
    }

    return {};
}

bool shouldValidateNpsso(const NpssoState& state, int64_t now, int64_t intervalSeconds, bool force)
{
    if (!state.hasNpsso)
        return false;

    if (force || state.lastCheckedAt <= 0)
        return true;

    return (state.lastCheckedAt + intervalSeconds) <= now;
}

} // namespace psn::bootstrap
