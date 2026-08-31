#ifndef AKIRA_HOST_HPP
#define AKIRA_HOST_HPP

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <chiaki/common.h>
#include <chiaki/discovery.h>
#include <chiaki/session.h>
#include <chiaki/regist.h>
#include <chiaki/opusdecoder.h>
#include <chiaki/log.h>
#include <chiaki/remote/holepunch.h>
#include <chiaki/remote/rudp.h>

#include "settings_manager.hpp"
#include "registration.hpp"

// Forward declarations
class SettingsManager;
class Session;

// Registration error codes
enum HostRegisterError {
    HOST_REGISTER_OK = 0,
    HOST_REGISTER_ERROR_SETTING_PSNACCOUNTID = 1,
    HOST_REGISTER_ERROR_SETTING_PSNONLINEID = 2,
    HOST_REGISTER_ERROR_UNDEFINED_TARGET = 3,
    HOST_REGISTER_ERROR_HOLEPUNCH = 4
};

enum class HostType {
    Discovered = 0,
    Auto = 1,
    Manual = 2,
    Remote = 3
};

enum class HolepunchPhase {
    Negotiating,
    Punching
};

class Host {
    friend class SettingsManager;
    friend class DiscoveryManager;

private:
    ChiakiLog* log = nullptr;
    SettingsManager* settings = nullptr;

    // Video/audio config
    ChiakiVideoResolutionPreset videoResolution = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
    ChiakiVideoFPSPreset videoFps = CHIAKI_VIDEO_FPS_PRESET_60;
    ChiakiConnectVideoProfile videoProfile;
    int haptic = -1;  // -1=inherit from global, 0=disabled, 1=weak, 2=strong

    // User info
    std::string psnOnlineId;
    std::string consolePIN;  // 4-digit login PIN for auto-login (optional)

    // Host identification
    std::string hostName;
    std::string hostId;
    std::string hostAddr;
    std::string serverNickname;

    // Access point info (from registration)
    std::string apSsid;
    std::string apKey;
    std::string apName;

    // Remote connection info
    std::string remoteDuid;  // DUID for matching remote hosts

    // Target console type
    ChiakiTarget target = CHIAKI_TARGET_PS4_UNKNOWN;
    ChiakiDiscoveryHostState state = CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN;

    // Registration data
    int64_t consoleId = 0;
    std::vector<Registration> registrations;

    bool discovered = false;
    bool sessionInit = false;
    bool registInit = false;
    bool psnRemotePlayDisabled = false;
    bool needsLinking = false;
    bool inConfig = false;
    HostType hostType = HostType::Discovered;

    ChiakiHolepunchSession holepunchSession = nullptr;

    // Session components
    ChiakiSession session;
    ChiakiOpusDecoder opusDecoder;
    ChiakiRegist regist;
    ChiakiRegistInfo registInfo;

    ChiakiRudp autoRegistRudp = nullptr;
    ChiakiHolepunchRegistInfo autoRegistHinfo{};
    bool autoRegistActive = false;
    ChiakiControllerState controllerState;
    std::map<uint32_t, int8_t> fingerIdTouchId;

    // Callbacks
    std::function<void()> onConnected;
    std::function<void(bool)> onLoginPinRequest;
    std::function<void(ChiakiQuitEvent*)> onQuit;
    std::function<void(uint8_t, uint8_t)> onRumble;
    std::function<void(ChiakiControllerState*, std::map<uint32_t, int8_t>*)> onReadController;
    std::function<void()> onRegistCanceled;
    std::function<void()> onRegistFailed;
    std::function<void()> onRegistSuccess;
    std::function<void(int, int)> onRegistStage;
    std::function<void()> onMotionReset;
    std::function<void(HolepunchPhase)> onHolepunchPhase;

public:
    struct CloudSessionConfig {
        ChiakiServiceType serviceType = CHIAKI_SERVICE_TYPE_REMOTE_PLAY;
        std::string host;
        uint16_t port = 0;
        std::string launchSpec;
        std::string handshakeKey;
        std::string sessionId;
        uint8_t psnWrapperType = 0;
        uint32_t mtuIn = 0;
        uint32_t mtuOut = 0;
        uint64_t rttUs = 0;
        std::string entitlementId;
        std::string platform;
    };

private:
    std::optional<CloudSessionConfig> cloudSession;

public:
    explicit Host(const std::string& name);
    ~Host();

    // Logger
    void setLogger(ChiakiLog* logger) { this->log = logger; }
    ChiakiLog* getLogger() const { return log; }

    // Host info getters
    std::string getHostName() const { return hostName; }
    std::string getHostAddr() const { return hostAddr; }
    std::string getHostId() const { return hostId; }
    ChiakiTarget getChiakiTarget() const { return target; }
    ChiakiDiscoveryHostState getState() const { return state; }

    // Host info setters
    void setHostAddr(const std::string& addr) { hostAddr = addr; }
    void setChiakiTarget(ChiakiTarget t) { target = t; }
    void setState(ChiakiDiscoveryHostState s) { state = s; }

    // Status checks
    bool isDiscovered() const { return discovered; }
    bool isRegistered() const { return activeRegistration() != nullptr; }
    bool hasRpKey() const { return activeRegistration() != nullptr; }
    bool isReady() const { return state == CHIAKI_DISCOVERY_HOST_STATE_READY; }
    bool isPS5() const;
    bool isPsnRemotePlayDisabled() const { return psnRemotePlayDisabled; }
    void setPsnRemotePlayDisabled(bool v) { psnRemotePlayDisabled = v; }
    bool isCloud() const { return cloudSession.has_value(); }
    bool isRemote() const { return hostType == HostType::Remote; }
    bool isManual() const { return hostType == HostType::Manual; }
    bool isAuto() const { return hostType == HostType::Auto; }
    HostType getHostType() const { return hostType; }
    void setHostType(HostType type) { hostType = type; }
    bool needsLink() const { return needsLinking; }
    void setNeedsLink(bool value) { needsLinking = value; }
    bool isInConfig() const { return inConfig; }
    void setInConfig(bool value) { inConfig = value; }

    // Console state helpers
    bool isStandby() const { return state == CHIAKI_DISCOVERY_HOST_STATE_STANDBY; }
    bool isAwake() const { return state == CHIAKI_DISCOVERY_HOST_STATE_READY; }

    // Console PIN for auto-login
    std::string getConsolePIN() const { return consolePIN; }
    void setConsolePIN(const std::string& pin) { consolePIN = pin; }

    // Remote DUID for matching remote hosts
    std::string getRemoteDuid() const { return remoteDuid; }
    void setRemoteDuid(const std::string& duid) { remoteDuid = duid; }

    // Per-host haptic setting (-1=inherit, 0=disabled, 1=weak, 2=strong)
    int getHapticRaw() const { return haptic; }
    void setHapticRaw(int value) { haptic = value; }
    void setCloudSessionConfig(const CloudSessionConfig& config) { cloudSession = config; }
    void clearCloudSessionConfig() { cloudSession.reset(); }
    const CloudSessionConfig* getCloudSessionConfig() const { return cloudSession ? &*cloudSession : nullptr; }

    // Send login PIN to active session
    void setLoginPIN(const std::string& pin);

    // Get display-friendly state string
    std::string getStateString() const;
    std::string getTargetString() const;

    // Video resolution helpers
    bool getVideoResolution(int* width, int* height) const;

    // Session management
    int initSession(Session* streamSession);
    int initSessionWithHolepunch(Session* streamSession, ChiakiHolepunchSession holepunch);
    void startSession();
    void stopSession();
    bool isSessionSocketHealthy() const;
    void gotoBed();
    int finiSession();
    int finiRegist();
    void sendFeedbackState();

    // Holepunch connection for remote play
    ChiakiErrorCode initHolepunchSession();
    ChiakiErrorCode connectHolepunch();
    void cancelHolepunch();
    void cleanupHolepunch();
    ChiakiHolepunchSession getHolepunchSession() const { return holepunchSession; }

    // Wakeup and registration
    int wakeup();
    int registerHost(int pin);
    int registerHostAuto();
    bool canAutoRegister() const;
    bool resolveRemoteDuid();
    void applyRegistrationData(ChiakiRegisteredHost* regHost);
    void copyRegistrationFrom(const Host* other);
    const Registration* activeRegistration() const;
    void upsertRegistration(const Registration& reg);
    int64_t getConsoleId() const { return consoleId; }

    // Event callbacks from chiaki
    void connectionEventCallback(ChiakiEvent* event);
    void registCallback(ChiakiRegistEvent* event);

    // Callback setters
    void setOnConnected(std::function<void()> callback) { onConnected = std::move(callback); }
    void setOnLoginPinRequest(std::function<void(bool)> callback) { onLoginPinRequest = std::move(callback); }
    void setOnQuit(std::function<void(ChiakiQuitEvent*)> callback) { onQuit = std::move(callback); }
    void setOnRumble(std::function<void(uint8_t, uint8_t)> callback) { onRumble = std::move(callback); }
    void setOnReadController(std::function<void(ChiakiControllerState*, std::map<uint32_t, int8_t>*)> callback) {
        onReadController = std::move(callback);
    }
    void setOnRegistCanceled(std::function<void()> callback) { onRegistCanceled = std::move(callback); }
    void setOnRegistFailed(std::function<void()> callback) { onRegistFailed = std::move(callback); }
    void setOnRegistSuccess(std::function<void()> callback) { onRegistSuccess = std::move(callback); }
    void setOnRegistStage(std::function<void(int, int)> callback) { onRegistStage = std::move(callback); }
    void setOnHolepunchPhase(std::function<void(HolepunchPhase)> callback) { onHolepunchPhase = std::move(callback); }
    void setOnMotionReset(std::function<void()> callback) { onMotionReset = std::move(callback); }
};

#endif // AKIRA_HOST_HPP
