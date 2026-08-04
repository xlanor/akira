#include "psn/auth.hpp"

#include "core/settings_manager.hpp"
#include "util/http_pool.hpp"

#include <borealis.hpp>
#include <ctime>
#include <format>

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
    return !storedAccessToken().empty() || !storedRefreshToken().empty();
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
        return false;

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

void Auth::clearTokens(const std::string& reason)
{
    brls::Logger::error("PSN {}: refresh token rejected ({}), clearing stored token data", label(), reason);
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

    if (storedRefreshToken().empty())
        return SessionState::NotLinked;

    return SessionState::Expired;
}

Error Auth::ensureSession(HttpSession& session, bool forceRefresh)
{
    if (!forceRefresh && tokenValid())
        return {};

    if (storedRefreshToken().empty())
    {
        if (storedAccessToken().empty())
            return {Status::NotLinked, "PSN account not linked"};

        return {Status::SessionExpired, "No PSN refresh token stored"};
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

    AuthResult result = performRefresh(session);

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

    struct json_object* parsed_json = json_tokener_parse(response.body.c_str());

    std::string errorCode;
    std::string errorMessage;
    struct json_object* error_obj;

    if (parsed_json && json_object_object_get_ex(parsed_json, "error", &error_obj))
    {
        errorCode = json_object_get_string(error_obj);
        errorMessage = errorCode;

        struct json_object* error_desc_obj;
        if (json_object_object_get_ex(parsed_json, "error_description", &error_desc_obj))
        {
            errorMessage += ": " + std::string(json_object_get_string(error_desc_obj));
        }
    }

    if (response.status != 200)
    {
        if (parsed_json)
            json_object_put(parsed_json);

        brls::Logger::error("PSN token refresh failed with HTTP {}: {}", response.status, response.body);

        bool tokenRejected = (response.status == 400 || response.status == 401) && errorCode == "invalid_grant";
        if (errorMessage.empty())
            errorMessage = std::format("HTTP error: {}", response.status);

        return {false, tokenRejected ? AuthError::Invalid : AuthError::Transient, errorMessage};
    }

    if (!parsed_json)
    {
        return {false, AuthError::Transient, "Failed to parse JSON response"};
    }

    if (!errorCode.empty())
    {
        json_object_put(parsed_json);
        return {false, AuthError::Transient, errorMessage};
    }

    std::string newAccessToken;
    std::string newRefreshToken;
    int expiresIn = 0;

    struct json_object* access_token_obj;
    struct json_object* refresh_token_obj;
    struct json_object* expires_in_obj;

    if (json_object_object_get_ex(parsed_json, "access_token", &access_token_obj))
    {
        newAccessToken = json_object_get_string(access_token_obj);
    }
    if (json_object_object_get_ex(parsed_json, "refresh_token", &refresh_token_obj))
    {
        newRefreshToken = json_object_get_string(refresh_token_obj);
    }
    if (json_object_object_get_ex(parsed_json, "expires_in", &expires_in_obj))
    {
        expiresIn = json_object_get_int(expires_in_obj);
    }

    json_object_put(parsed_json);

    if (newAccessToken.empty() || newRefreshToken.empty())
    {
        return {false, AuthError::Transient, "Missing tokens in response"};
    }

    storeTokens(newAccessToken, newRefreshToken, expiresIn);

    brls::Logger::info("PSN {}: token refreshed successfully", label());
    return {true, AuthError::Transient, ""};
}

} // namespace psn
