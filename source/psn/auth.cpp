#include "psn/auth.hpp"

#include "psn/auth_bootstrap.hpp"
#include "core/settings_manager.hpp"
#include "util/http_pool.hpp"

#include <borealis.hpp>
#include <array>
#include <ctime>
#include <format>
#include <random>

#include <json-c/json.h>

namespace psn {

struct CredentialProfile {
    const char* label;
    const char* clientId;
    const char* clientSecret;
    const char* tokenUrl;
    const char* scopes;
    const char* extraFields;
};

static const CredentialProfile REMOTE_PLAY_PROFILE = {
    "remote play",
    "ba495a24-818c-472b-b12d-ff231c1b5745",
    "mvaiZkRsAsI1IBkY",
    "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/token",
    "psn:clientapp referenceDataService:countryConfig.read pushNotification:webSocket.desktop.connect sessionManager:remotePlaySession.system.update",
    "&redirect_uri=https://remoteplay.dl.playstation.net/remoteplay/redirect"
};

static const CredentialProfile MOBILE_SSO_PROFILE = {
    "mobile sso",
    "09515159-7237-4370-9b40-3806e67c0891",
    "ucPjka5tntB2KqsP",
    "https://ca.account.sony.com/api/authz/v3/oauth/token",
    "psn:mobile.v2.core psn:clientapp",
    "&token_format=jwt"
};

static const CredentialProfile& profileFor(Credential credential)
{
    return credential == Credential::MobileSso ? MOBILE_SSO_PROFILE : REMOTE_PLAY_PROFILE;
}

static bootstrap::Credential bootstrapCredentialFor(Credential credential)
{
    return credential == Credential::MobileSso
        ? bootstrap::Credential::MobileSso
        : bootstrap::Credential::RemotePlay;
}

static std::string buildCid()
{
    std::array<unsigned char, 16> bytes{};
    std::random_device rd;
    for (unsigned char& byte : bytes)
        byte = static_cast<unsigned char>(rd());

    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);

    return std::format(
        "{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5], bytes[6], bytes[7],
        bytes[8], bytes[9], bytes[10], bytes[11],
        bytes[12], bytes[13], bytes[14], bytes[15]);
}

struct AuthCodeResult {
    AuthResult result;
    std::string code;
};

static AuthResult parseTokenResponse(const HttpResponse& response, const char* action,
    bool invalidGrantIsInvalid, std::string& outAccessToken, std::string& outRefreshToken, int& outExpiresIn)
{
    json_object* parsed = json_tokener_parse(response.body.c_str());

    std::string errorCode;
    std::string errorMessage;
    json_object* errorObj = nullptr;
    if (parsed && json_object_object_get_ex(parsed, "error", &errorObj))
    {
        errorCode = json_object_get_string(errorObj);
        errorMessage = errorCode;

        json_object* errorDescObj = nullptr;
        if (json_object_object_get_ex(parsed, "error_description", &errorDescObj))
            errorMessage += ": " + std::string(json_object_get_string(errorDescObj));
    }

    if (response.status != 200)
    {
        if (parsed)
            json_object_put(parsed);

        brls::Logger::error("PSN {} failed with HTTP {}: {}", action, response.status, response.body);

        bool tokenRejected = invalidGrantIsInvalid &&
            (response.status == 400 || response.status == 401) &&
            errorCode == "invalid_grant";
        if (errorMessage.empty())
            errorMessage = std::format("HTTP error: {}", response.status);

        return {false, tokenRejected ? AuthError::Invalid : AuthError::Transient, errorMessage};
    }

    if (!parsed)
        return {false, AuthError::Transient, "Failed to parse JSON response"};

    if (!errorCode.empty())
    {
        bool tokenRejected = invalidGrantIsInvalid && errorCode == "invalid_grant";
        json_object_put(parsed);
        return {false, tokenRejected ? AuthError::Invalid : AuthError::Transient, errorMessage};
    }

    json_object* accessTokenObj = nullptr;
    json_object* refreshTokenObj = nullptr;
    json_object* expiresInObj = nullptr;

    if (json_object_object_get_ex(parsed, "access_token", &accessTokenObj))
        outAccessToken = json_object_get_string(accessTokenObj);
    if (json_object_object_get_ex(parsed, "refresh_token", &refreshTokenObj))
        outRefreshToken = json_object_get_string(refreshTokenObj);
    if (json_object_object_get_ex(parsed, "expires_in", &expiresInObj))
        outExpiresIn = json_object_get_int(expiresInObj);

    json_object_put(parsed);

    if (outAccessToken.empty() || outRefreshToken.empty())
        return {false, AuthError::Transient, "Missing tokens in response"};

    return {true, AuthError::Transient, {}};
}

static AuthCodeResult requestAuthCode(HttpSession& session, Credential credential,
    SettingsManager* settings, const std::string& npsso)
{
    std::string duid;
    if (credential == Credential::RemotePlay)
    {
        duid = settings->getGlobalDuid();
        if (duid.empty())
            return {{false, AuthError::Invalid, "No DUID stored for NPSSO bootstrap"}, {}};
    }

    HttpRequest request;
    request.url = bootstrap::buildAuthorizeUrl(bootstrapCredentialFor(credential), duid, buildCid());
    request.headers = {
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
        "Cookie: npsso=" + npsso
    };
    request.followLocation = false;
    request.timeoutSec = 30;

    HttpResponse response = session.perform(request);
    if (response.transportFailed())
    {
        brls::Logger::error("PSN {} NPSSO authorize transport failure: {}", profileFor(credential).label, response.error);
        return {{false, AuthError::Transient, response.error}, {}};
    }

    std::string code = bootstrap::extractAuthCode(response.header("Location"));
    if (!code.empty())
        return {{true, AuthError::Transient, {}}, std::move(code)};

    if (response.status >= 500)
        return {{false, AuthError::Transient, std::format("HTTP error: {}", response.status)}, {}};

    std::string message = std::format(
        "No authorization code returned (status {}); the stored NPSSO may be invalid or expired",
        response.status);
    brls::Logger::warning("PSN {} NPSSO authorize failed: {}", profileFor(credential).label, message);
    return {{false, AuthError::Invalid, std::move(message)}, {}};
}

ActionStatus actionStatus(bool busy, std::chrono::steady_clock::time_point readyAt)
{
    if (busy)
        return {ActionState::Busy, 0};

    auto now = std::chrono::steady_clock::now();
    if (readyAt <= now)
        return {ActionState::Ready, 0};

    auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(readyAt - now).count();
    return {ActionState::CoolingDown, static_cast<int>((remainingMs + 999) / 1000)};
}

Auth& Auth::instance()
{
    static Auth instance(Credential::RemotePlay);
    return instance;
}

Auth& Auth::mobile()
{
    static Auth instance(Credential::MobileSso);
    return instance;
}

Auth& Auth::forCredential(Credential credential)
{
    return credential == Credential::MobileSso ? mobile() : instance();
}

Auth::Auth(Credential credential)
    : credential(credential)
{
    settings = SettingsManager::getInstance();
}

const char* Auth::label() const
{
    return profileFor(credential).label;
}

std::string Auth::storedAccessToken() const
{
    return credential == Credential::MobileSso
        ? settings->getPsnMobileSsoAccessToken()
        : settings->getPsnAccessToken();
}

std::string Auth::storedRefreshToken() const
{
    return credential == Credential::MobileSso
        ? settings->getPsnMobileSsoRefreshToken()
        : settings->getPsnRefreshToken();
}

int64_t Auth::storedExpiresAt() const
{
    return credential == Credential::MobileSso
        ? settings->getPsnMobileSsoExpiresAt()
        : settings->getPsnTokenExpiresAt();
}

std::string Auth::storedNpsso() const
{
    return settings->getPsnNpsso();
}

int64_t Auth::storedNpssoLastCheckedAt() const
{
    return settings->getPsnNpssoLastCheckedAt();
}

bool Auth::storedNpssoValid() const
{
    return settings->getPsnNpssoValid();
}

bool Auth::hasUsableNpsso() const
{
    std::string npsso = storedNpsso();
    if (npsso.empty())
        return false;

    int64_t checkedAt = storedNpssoLastCheckedAt();
    if (checkedAt > 0 && !storedNpssoValid())
        return false;

    return true;
}

void Auth::storeNpssoValidation(bool valid, int64_t checkedAt)
{
    settings->setPsnNpssoValid(valid);
    settings->setPsnNpssoLastCheckedAt(checkedAt);
    settings->writeFile();
}

void Auth::storeTokens(const std::string& access, const std::string& refresh, int expiresIn)
{
    int64_t expiresAt = expiresIn > 0
        ? static_cast<int64_t>(std::time(nullptr)) + expiresIn
        : 0;

    if (credential == Credential::MobileSso)
    {
        settings->setPsnMobileSsoAccessToken(access);
        settings->setPsnMobileSsoRefreshToken(refresh);
        if (expiresAt > 0)
            settings->setPsnMobileSsoExpiresAt(expiresAt);
    }
    else
    {
        settings->setPsnAccessToken(access);
        settings->setPsnRefreshToken(refresh);
        if (expiresAt > 0)
            settings->setPsnTokenExpiresAt(expiresAt);
    }

    settings->writeFile();
}

void Auth::clearStoredTokens()
{
    if (credential == Credential::MobileSso)
        settings->clearPsnMobileSsoData();
    else
        settings->clearPsnTokenData();

    settings->writeFile();
}

bool Auth::linked() const
{
    return !storedAccessToken().empty() || !storedRefreshToken().empty() || hasUsableNpsso();
}

std::string Auth::accessToken() const
{
    return storedAccessToken();
}

bool Auth::tokenValid() const
{
    if (storedAccessToken().empty())
        return false;

    int64_t expiresAt = storedExpiresAt();
    if (expiresAt <= 0)
        return false;

    int64_t now = static_cast<int64_t>(std::time(nullptr));
    return (expiresAt - 60) > now;
}

bool Auth::needsProactiveRefresh(int64_t windowSeconds) const
{
    if (storedRefreshToken().empty())
        return hasUsableNpsso() && !tokenValid();

    int64_t expiresAt = storedExpiresAt();
    if (expiresAt <= 0)
        return true;

    int64_t now = static_cast<int64_t>(std::time(nullptr));
    return (expiresAt - windowSeconds) <= now;
}

int64_t Auth::secondsUntilExpiry() const
{
    int64_t expiresAt = storedExpiresAt();
    if (expiresAt <= 0)
        return -1;

    return expiresAt - static_cast<int64_t>(std::time(nullptr));
}

bool Auth::shouldValidateNpsso(int64_t intervalSeconds, bool force) const
{
    bootstrap::NpssoState state{
        .hasNpsso = !storedNpsso().empty(),
        .valid = storedNpssoValid(),
        .lastCheckedAt = storedNpssoLastCheckedAt(),
    };
    return bootstrap::shouldValidateNpsso(
        state, static_cast<int64_t>(std::time(nullptr)), intervalSeconds, force);
}

void Auth::clearTokens(const std::string& reason)
{
    brls::Logger::error("PSN {}: stored credentials are no longer usable ({}), clearing token data", label(), reason);
    clearStoredTokens();
    notifyStateChanged();
}

void Auth::setStateObserver(StateObserver observer)
{
    stateObserver = std::move(observer);
}

void Auth::notifyStateChanged()
{
    if (stateObserver)
        stateObserver();
}

SessionState Auth::state() const
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (inFlight || queued > 0)
            return SessionState::Refreshing;
    }

    if (tokenValid())
        return SessionState::Valid;

    if (storedRefreshToken().empty() && !hasUsableNpsso())
        return SessionState::NotLinked;

    return SessionState::Expired;
}

Error Auth::ensureSession(HttpSession& session, bool forceRefresh)
{
    if (!forceRefresh && tokenValid())
        return {};

    bool haveRefreshToken = !storedRefreshToken().empty();
    if (!haveRefreshToken && !hasUsableNpsso())
    {
        if (storedAccessToken().empty())
            return {Status::NotLinked, "PSN account not linked"};

        clearTokens("No refresh token or valid NPSSO recovery credential stored");
        return {Status::SessionExpired, "No PSN refresh token or valid NPSSO stored"};
    }

    brls::Logger::info("PSN {}: session needs a refresh ({})", label(), forceRefresh ? "forced" : "expired");

    AuthResult result = refreshBlocking(session);
    if (result.success)
    {
        notifyStateChanged();
        return {};
    }

    if (result.error == AuthError::Invalid)
    {
        clearTokens(result.message);
        return {Status::SessionExpired, result.message};
    }

    brls::Logger::warning("PSN: session refresh failed transiently ({}), keeping stored tokens", result.message);
    return {Status::Offline, result.message};
}

ActionStatus Auth::refreshStatus() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return actionStatus(inFlight || queued > 0, readyAt);
}

void Auth::refresh(std::function<void()> onSuccess, ErrorCallback onError)
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        queued++;
    }

    HttpPool::instance().submit([this, onSuccess = std::move(onSuccess), onError = std::move(onError)](HttpSession& session) {
        AuthResult result = refreshBlocking(session);

        {
            std::lock_guard<std::mutex> lock(mutex);
            queued--;
        }

        brls::sync([result, onSuccess, onError]() {
            if (result.success)
            {
                if (onSuccess)
                    onSuccess();
            }
            else if (onError)
            {
                onError(result.error, result.message);
            }
        });
    });
}

AuthResult Auth::refreshBlocking(HttpSession& session)
{
    std::unique_lock<std::mutex> lock(mutex);

    if (inFlight)
    {
        brls::Logger::info("PSN token refresh already in flight, awaiting its result");
        cond.wait(lock, [this]() { return !inFlight; });
        return lastResult;
    }

    inFlight = true;
    lock.unlock();

    AuthResult result = {false, AuthError::Invalid, "No refresh token stored"};
    if (!storedRefreshToken().empty())
    {
        result = performRefresh(session);
        if (!result.success && result.error == AuthError::Invalid && hasUsableNpsso())
        {
            brls::Logger::warning("PSN {}: refresh token rejected, trying stored NPSSO recovery", label());
            result = performNpssoBootstrap(session);
        }
    }
    else if (hasUsableNpsso())
    {
        brls::Logger::info("PSN {}: no refresh token, bootstrapping from stored NPSSO", label());
        result = performNpssoBootstrap(session);
    }

    lock.lock();
    lastResult = result;
    inFlight = false;
    readyAt = std::chrono::steady_clock::now() +
        std::chrono::seconds(result.success ? COOLDOWN_S : FAILED_COOLDOWN_S);
    cond.notify_all();

    return result;
}

AuthResult Auth::performRefresh(HttpSession& session)
{
    std::string refreshToken = storedRefreshToken();
    if (refreshToken.empty())
    {
        return {false, AuthError::Invalid, "No refresh token stored"};
    }

    const CredentialProfile& profile = profileFor(credential);

    HttpRequest request;
    request.url = profile.tokenUrl;
    request.basicUser = profile.clientId;
    request.basicPassword = profile.clientSecret;
    request.headers = {"Content-Type: application/x-www-form-urlencoded"};
    request.postFields = "grant_type=refresh_token"
        "&refresh_token=" + refreshToken +
        "&scope=" + std::string(profile.scopes) +
        std::string(profile.extraFields);
    request.post = true;
    request.timeoutSec = 30;

    HttpResponse response = session.perform(request);

    if (response.transportFailed())
    {
        brls::Logger::error("PSN token refresh transport failure: {}", response.error);
        return {false, AuthError::Transient, response.error};
    }

    std::string newAccessToken;
    std::string newRefreshToken;
    int expiresIn = 0;
    AuthResult parsed = parseTokenResponse(
        response, std::format("{} token refresh", label()).c_str(), true,
        newAccessToken, newRefreshToken, expiresIn);
    if (!parsed.success)
        return parsed;

    storeTokens(newAccessToken, newRefreshToken, expiresIn);

    brls::Logger::info("PSN {}: token refreshed successfully", label());
    return {true, AuthError::Transient, ""};
}

AuthResult Auth::validateNpssoBlocking(HttpSession& session)
{
    return performNpssoValidation(session);
}

AuthResult Auth::performNpssoValidation(HttpSession& session)
{
    std::string npsso = storedNpsso();
    if (npsso.empty())
        return {false, AuthError::Invalid, "No NPSSO stored"};

    AuthCodeResult code = requestAuthCode(session, credential, settings, npsso);
    if (code.result.success)
    {
        storeNpssoValidation(true, static_cast<int64_t>(std::time(nullptr)));
        brls::Logger::info("PSN {}: stored NPSSO validated successfully", label());
        return code.result;
    }

    if (code.result.error == AuthError::Invalid)
        storeNpssoValidation(false, static_cast<int64_t>(std::time(nullptr)));

    return code.result;
}

AuthResult Auth::performNpssoBootstrap(HttpSession& session)
{
    std::string npsso = storedNpsso();
    if (npsso.empty())
        return {false, AuthError::Invalid, "No NPSSO stored"};

    AuthCodeResult code = requestAuthCode(session, credential, settings, npsso);
    if (!code.result.success)
    {
        if (code.result.error == AuthError::Invalid)
            storeNpssoValidation(false, static_cast<int64_t>(std::time(nullptr)));
        return code.result;
    }

    const CredentialProfile& profile = profileFor(credential);

    HttpRequest request;
    request.url = "https://ca.account.sony.com/api/authz/v3/oauth/token";
    if (credential == Credential::MobileSso)
    {
        request.basicUser = profile.clientId;
        request.basicPassword = profile.clientSecret;
    }
    request.headers = {"Content-Type: application/x-www-form-urlencoded"};
    request.postFields = bootstrap::buildCodeExchangeBody(
        bootstrapCredentialFor(credential), code.code);
    request.post = true;
    request.timeoutSec = 30;

    HttpResponse response = session.perform(request);
    if (response.transportFailed())
    {
        brls::Logger::error("PSN {} NPSSO bootstrap transport failure: {}", label(), response.error);
        return {false, AuthError::Transient, response.error};
    }

    std::string newAccessToken;
    std::string newRefreshToken;
    int expiresIn = 0;
    AuthResult parsed = parseTokenResponse(
        response, std::format("{} NPSSO bootstrap", label()).c_str(), true,
        newAccessToken, newRefreshToken, expiresIn);
    if (!parsed.success)
    {
        if (parsed.error == AuthError::Invalid)
            storeNpssoValidation(false, static_cast<int64_t>(std::time(nullptr)));
        return parsed;
    }

    storeTokens(newAccessToken, newRefreshToken, expiresIn);
    storeNpssoValidation(true, static_cast<int64_t>(std::time(nullptr)));
    brls::Logger::info("PSN {}: bootstrapped fresh tokens from stored NPSSO", label());
    return {true, AuthError::Transient, {}};
}

} // namespace psn
