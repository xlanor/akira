#ifndef AKIRA_PSN_AUTH_HPP
#define AKIRA_PSN_AUTH_HPP

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>

#include "util/http.hpp"

class SettingsManager;

namespace psn {

enum class AuthError {
    Transient,
    Invalid
};

struct AuthResult {
    bool success = false;
    AuthError error = AuthError::Transient;
    std::string message;
};

enum class ActionState {
    Ready,
    Busy,
    CoolingDown
};

struct ActionStatus {
    ActionState state = ActionState::Ready;
    int secondsRemaining = 0;
};

ActionStatus actionStatus(bool busy, std::chrono::steady_clock::time_point readyAt);

class Auth {
public:
    using ErrorCallback = std::function<void(AuthError, const std::string&)>;

    static Auth& instance();

    bool linked() const;
    bool tokenValid() const;
    std::string accessToken() const;

    AuthResult refreshBlocking(HttpSession& session);
    void refresh(std::function<void()> onSuccess, ErrorCallback onError);

    void clearTokens(const std::string& reason);

    ActionStatus refreshStatus() const;

private:
    Auth();

    AuthResult performRefresh(HttpSession& session);

    static constexpr int COOLDOWN_S = 60;
    static constexpr int FAILED_COOLDOWN_S = 5;

    SettingsManager* settings = nullptr;

    mutable std::mutex mutex;
    std::condition_variable cond;
    bool inFlight = false;
    int queued = 0;
    AuthResult lastResult;
    std::chrono::steady_clock::time_point readyAt{};
};

} // namespace psn

#endif // AKIRA_PSN_AUTH_HPP
