#include "psn/auth.hpp"

#include "core/settings_manager.hpp"
#include "util/http_pool.hpp"

#include <borealis.hpp>
#include <ctime>
#include <format>

#include <json-c/json.h>

namespace psn {

static const char* CLIENT_ID = "ba495a24-818c-472b-b12d-ff231c1b5745";
static const char* CLIENT_SECRET = "mvaiZkRsAsI1IBkY";
static const char* TOKEN_URL = "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/token";
static const char* SCOPES = "psn:clientapp referenceDataService:countryConfig.read pushNotification:webSocket.desktop.connect sessionManager:remotePlaySession.system.update";
static const char* REDIRECT_URI = "https://remoteplay.dl.playstation.net/remoteplay/redirect";

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
    static Auth instance;
    return instance;
}

Auth::Auth()
{
    settings = SettingsManager::getInstance();
}

bool Auth::linked() const
{
    return !settings->getPsnAccessToken().empty() || !settings->getPsnRefreshToken().empty();
}

std::string Auth::accessToken() const
{
    return settings->getPsnAccessToken();
}

bool Auth::tokenValid() const
{
    if (settings->getPsnAccessToken().empty())
        return false;

    int64_t expiresAt = settings->getPsnTokenExpiresAt();
    if (expiresAt <= 0)
        return false;

    int64_t now = static_cast<int64_t>(std::time(nullptr));
    return (expiresAt - 60) > now;
}

void Auth::clearTokens(const std::string& reason)
{
    brls::Logger::error("PSN: refresh token rejected ({}), clearing stored PSN token data", reason);
    settings->clearPsnTokenData();
    settings->writeFile();
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

    if (settings->getPsnRefreshToken().empty())
        return SessionState::NotLinked;

    return SessionState::Expired;
}

Error Auth::ensureSession(HttpSession& session, bool forceRefresh)
{
    if (!forceRefresh && tokenValid())
        return {};

    if (settings->getPsnRefreshToken().empty())
    {
        if (settings->getPsnAccessToken().empty())
            return {Status::NotLinked, "PSN account not linked"};

        return {Status::SessionExpired, "No PSN refresh token stored"};
    }

    brls::Logger::info("PSN: session needs a refresh ({})", forceRefresh ? "forced" : "expired");

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
    std::string refreshToken = settings->getPsnRefreshToken();
    if (refreshToken.empty())
    {
        return {false, AuthError::Invalid, "No refresh token stored"};
    }

    HttpRequest request;
    request.url = TOKEN_URL;
    request.basicUser = CLIENT_ID;
    request.basicPassword = CLIENT_SECRET;
    request.headers = {"Content-Type: application/x-www-form-urlencoded"};
    request.postFields = "grant_type=refresh_token"
        "&refresh_token=" + refreshToken +
        "&scope=" + std::string(SCOPES) +
        "&redirect_uri=" + std::string(REDIRECT_URI);
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

    settings->setPsnAccessToken(newAccessToken);
    settings->setPsnRefreshToken(newRefreshToken);

    if (expiresIn > 0)
    {
        int64_t expiresAt = static_cast<int64_t>(std::time(nullptr)) + expiresIn;
        settings->setPsnTokenExpiresAt(expiresAt);
    }

    settings->writeFile();

    brls::Logger::info("PSN token refreshed successfully");
    return {true, AuthError::Transient, ""};
}

} // namespace psn
