#include "core/host.hpp"
#include "core/io.hpp"
#include "core/exception.hpp"

#include <borealis.hpp>
#include <cstring>

#include <chiaki/base64.h>

static void InitAudioCallback(unsigned int channels, unsigned int rate, void* user)
{
    IO* io = static_cast<IO*>(user);
    io->InitAudioCB(channels, rate);
}

static bool VideoCallback(uint8_t* buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void* user)
{
    static uint32_t cb_count = 0;
    if (cb_count++ % 100 == 0)
    {
        brls::Logger::info("VideoCallback (static): frame#{}, buf_size={}", cb_count, buf_size);
    }

    IO* io = static_cast<IO*>(user);
    return io->VideoCB(buf, buf_size, frames_lost, frame_recovered, user);
}

static void AudioCallback(int16_t* buf, size_t samples_count, void* user)
{
    IO* io = static_cast<IO*>(user);
    io->AudioCB(buf, samples_count);
}

static void HapticsFrameCallback(uint8_t* buf, size_t buf_size, void* user)
{
    static uint32_t haptic_frame_count = 0;
    if (haptic_frame_count++ % 100 == 0) 
    {
        brls::Logger::info("HapticsFrameCallback: buf_size={}, frame#{}", buf_size, haptic_frame_count);
    }
    IO* io = static_cast<IO*>(user);
    io->HapticCB(buf, buf_size);
}

static void EventCallback(ChiakiEvent* event, void* user)
{
    Host* host = static_cast<Host*>(user);
    host->connectionEventCallback(event);
}

static void RegistEventCallback(ChiakiRegistEvent* event, void* user)
{
    Host* host = static_cast<Host*>(user);
    host->registCallback(event);
}

Host::Host(const std::string& name)
    : hostName(name)
{
    settings = SettingsManager::getInstance();
    log = settings->getLogger();
    memset(&session, 0, sizeof(session));
    memset(&opusDecoder, 0, sizeof(opusDecoder));
    memset(&regist, 0, sizeof(regist));
    memset(&registInfo, 0, sizeof(registInfo));
    memset(&controllerState, 0, sizeof(controllerState));
    memset(&videoProfile, 0, sizeof(videoProfile));
}

Host::~Host()
{
    if (sessionInit)
    {
        finiSession();
    }
}

bool Host::isPS5() const
{
    return target >= CHIAKI_TARGET_PS5_UNKNOWN;
}

void Host::setLoginPIN(const std::string& pin)
{
    brls::Logger::info("Host::setLoginPIN called with pin length={}", pin.length());

    if (!sessionInit)
    {
        brls::Logger::error("setLoginPIN: session not initialized!");
        return;
    }

    brls::Logger::info("setLoginPIN: calling chiaki_session_set_login_pin...");

    chiaki_session_set_login_pin(&session,
        reinterpret_cast<const uint8_t*>(pin.c_str()),
        pin.length());

    brls::Logger::info("setLoginPIN: chiaki_session_set_login_pin completed");
}

std::string Host::getStateString() const
{
    switch (state)
    {
        case CHIAKI_DISCOVERY_HOST_STATE_READY:
            return "Ready";
        case CHIAKI_DISCOVERY_HOST_STATE_STANDBY:
            return "Standby";
        default:
            return "Unknown";
    }
}

std::string Host::getTargetString() const
{
    if (target >= CHIAKI_TARGET_PS5_UNKNOWN)
    {
        return "PS5";
    }
    else if (target >= CHIAKI_TARGET_PS4_UNKNOWN)
    {
        return "PS4";
    }
    return "Unknown";
}

bool Host::getVideoResolution(int* width, int* height) const
{
    switch (videoResolution)
    {
        case CHIAKI_VIDEO_RESOLUTION_PRESET_360p:
            *width = 640;
            *height = 360;
            break;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_540p:
            *width = 950;
            *height = 540;
            break;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_720p:
            *width = 1280;
            *height = 720;
            break;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p:
            *width = 1920;
            *height = 1080;
            break;
        default:
            return false;
    }
    return true;
}

int Host::wakeup()
{
    if (strlen(rpRegistKey) > 8)
    {
        brls::Logger::error("Given registkey is too long");
        return 1;
    }
    else if (strlen(rpRegistKey) <= 0)
    {
        brls::Logger::error("Given registkey is not defined");
        return 2;
    }

    uint64_t credential = strtoull(rpRegistKey, NULL, 16);
    ChiakiErrorCode ret = chiaki_discovery_wakeup(log, NULL, hostAddr.c_str(), credential, isPS5());

    return ret;
}

void Host::applyRegistrationData(ChiakiRegisteredHost* regHost)
{
    if (!regHost) return;

    apSsid = regHost->ap_ssid;
    apKey = regHost->ap_key;
    apName = regHost->ap_name;
    memcpy(serverMac, regHost->server_mac, sizeof(serverMac));
    serverNickname = regHost->server_nickname;
    memcpy(rpRegistKey, regHost->rp_regist_key, sizeof(rpRegistKey));
    rpKeyType = regHost->rp_key_type;
    memcpy(rpKey, regHost->rp_key, sizeof(rpKey));
    target = regHost->target;

    registered = true;
    rpKeyData = true;

    brls::Logger::info("Applied registration data for {}", hostName);
}

int Host::registerHost(int pin)
{
    std::string accountId = settings->getPsnAccountId(this);
    std::string onlineId = settings->getPsnOnlineId(this);
    size_t accountIdSize = sizeof(uint8_t[CHIAKI_PSN_ACCOUNT_ID_SIZE]);

    registInfo.target = target;
    registInfo.holepunch_info = NULL;
    registInfo.rudp = NULL;

    if (target >= CHIAKI_TARGET_PS4_9)
    {
        // Use AccountID for PS4 > 7.0
        if (accountId.length() > 0)
        {
            chiaki_base64_decode(accountId.c_str(), accountId.length(),
                registInfo.psn_account_id, &accountIdSize);
            registInfo.psn_online_id = nullptr;
        }
        else
        {
            brls::Logger::error("Undefined PSN Account ID (Please configure a valid psn_account_id)");
            return HOST_REGISTER_ERROR_SETTING_PSNACCOUNTID;
        }
    }
    else if (target > CHIAKI_TARGET_PS4_UNKNOWN)
    {
        if (onlineId.length() > 0)
        {
            registInfo.psn_online_id = psnOnlineId.c_str();
        }
        else
        {
            brls::Logger::error("Undefined PSN Online ID (Please configure a valid psn_online_id)");
            return HOST_REGISTER_ERROR_SETTING_PSNONLINEID;
        }
    }
    else
    {
        brls::Logger::error("Undefined PS4 system version (please run discover first)");
        return HOST_REGISTER_ERROR_UNDEFINED_TARGET;
    }

    registInfo.pin = pin;
    registInfo.host = hostAddr.c_str();
    registInfo.broadcast = false;

    if (target >= CHIAKI_TARGET_PS4_9)
    {
        brls::Logger::info("Registering to host `{}` `{}` with PSN AccountID `{}` pin `{}`",
            hostName, hostAddr, accountId, pin);
    }
    else
    {
        brls::Logger::info("Registering to host `{}` `{}` with PSN OnlineID `{}` pin `{}`",
            hostName, hostAddr, onlineId, pin);
    }

    chiaki_regist_start(&regist, log, &registInfo, RegistEventCallback, this);
    return HOST_REGISTER_OK;
}

int Host::initSession(IO* io)
{
    return initSessionWithHolepunch(io, nullptr);
}

int Host::initSessionWithHolepunch(IO* io, ChiakiHolepunchSession holepunch)
{
    chiaki_connect_video_profile_preset(&videoProfile, videoResolution, videoFps);

    // Override bitrates for Switch
    // I can guarantee you you will not have a good time trying to do
    // 10000 for 720p on switch.
    switch (videoResolution)
    {
        case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p:
            videoProfile.bitrate = 7000; 
            break;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_720p:
            videoProfile.bitrate = 5000; 
            break;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_540p:
            videoProfile.bitrate = 3000; 
            break;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_360p:
            videoProfile.bitrate = 1500; 
            break;
    }

    chiaki_opus_decoder_init(&opusDecoder, log);

    ChiakiAudioSink audioSink = {};
    ChiakiAudioSink hapticsSink = {};
    hapticsSink.user = io;
    hapticsSink.frame_cb = HapticsFrameCallback;

    ChiakiConnectInfo connectInfo = {};
    connectInfo.host = hostAddr.c_str();
    connectInfo.video_profile = videoProfile;
    connectInfo.video_profile_auto_downgrade = true;
    connectInfo.holepunch_session = holepunch; 

    if (isPS5())
    {
        connectInfo.video_profile.codec = CHIAKI_CODEC_H265;
    }

    io->setRequestedProfile(
        connectInfo.video_profile.width,
        connectInfo.video_profile.height,
        connectInfo.video_profile.max_fps,
        connectInfo.video_profile.bitrate,
        connectInfo.video_profile.codec == CHIAKI_CODEC_H265
    );

    HapticPreset hapticSetting = settings->getHaptic(this);
    int effectiveHaptic = static_cast<int>(hapticSetting);
    brls::Logger::info("Host::initSession: haptic={} (per-host={})", effectiveHaptic, haptic);
    if (effectiveHaptic > 0)
    {
        connectInfo.enable_dualsense = true;
        brls::Logger::info("Host::initSession: enable_dualsense=true");
        if (effectiveHaptic == 1)
        {
            io->setHapticBase(128);
            io->setRumbleStrength(0.5f);
        }
        else
        {
            io->setHapticBase(50);
            io->setRumbleStrength(1.0f);
        }
    }
    else
    {
        brls::Logger::info("Host::initSession: enable_dualsense=false (haptic disabled)");
        io->setRumbleStrength(0.0f);
    }

    connectInfo.ps5 = isPS5();

    if (!io->InitAVCodec(isPS5(), connectInfo.video_profile.width, connectInfo.video_profile.height))
    {
        throw Exception("Failed to initiate libav codec");
    }

    if (!io->InitVideo(connectInfo.video_profile.width, connectInfo.video_profile.height, 1280, 720))
    {
        throw Exception("Failed to initiate video");
    }

    memcpy(connectInfo.regist_key, rpRegistKey, sizeof(connectInfo.regist_key));
    memcpy(connectInfo.morning, rpKey, sizeof(connectInfo.morning));

    ChiakiErrorCode err = chiaki_session_init(&session, &connectInfo, log);
    if (err != CHIAKI_ERR_SUCCESS)
    {
        throw Exception(chiaki_error_string(err));
    }

    sessionInit = true;

    brls::Logger::info("Host::initSession: Setting up callbacks...");
    chiaki_opus_decoder_set_cb(&opusDecoder, InitAudioCallback, AudioCallback, io);
    chiaki_opus_decoder_get_sink(&opusDecoder, &audioSink);
    chiaki_session_set_audio_sink(&session, &audioSink);
    chiaki_session_set_haptics_sink(&session, &hapticsSink);
    brls::Logger::info("Host::initSession: Setting video callback, io={}", (void*)io);
    chiaki_session_set_video_sample_cb(&session, VideoCallback, io);
    chiaki_session_set_event_cb(&session, EventCallback, this);
    brls::Logger::info("Host::initSession: All callbacks set");

    chiaki_controller_state_set_idle(&controllerState);

    return 0;
}

int Host::finiSession()
{
    if (sessionInit)
    {
        sessionInit = false;
        chiaki_session_join(&session);
        chiaki_session_fini(&session);
        chiaki_opus_decoder_fini(&opusDecoder);
    }
    return 0;
}

void Host::stopSession()
{
    chiaki_session_stop(&session);
}

void Host::gotoBed()
{
    if (sessionInit) {
        brls::Logger::info("Sending go to bed command to console");
        chiaki_session_goto_bed(&session);
    }
}

void Host::startSession()
{
    brls::Logger::info("Starting chiaki session...");
    ChiakiErrorCode err = chiaki_session_start(&session);
    if (err != CHIAKI_ERR_SUCCESS)
    {
        brls::Logger::error("chiaki_session_start failed with error: {}", chiaki_error_string(err));
        chiaki_session_fini(&session);
        throw Exception("Chiaki Session Start failed");
    }
    brls::Logger::info("chiaki_session_start returned SUCCESS");
}

void Host::sendFeedbackState()
{
    if (onReadController)
    {
        onReadController(&controllerState, &fingerIdTouchId);
    }
    chiaki_session_set_controller_state(&session, &controllerState);
}

void Host::connectionEventCallback(ChiakiEvent* event)
{
    brls::Logger::info("connectionEventCallback: event type={}", static_cast<int>(event->type));

    switch (event->type)
    {
        case CHIAKI_EVENT_CONNECTED:
            brls::Logger::info("EventCB CHIAKI_EVENT_CONNECTED");
            if (onConnected)
            {
                onConnected();
            }
            break;

        case CHIAKI_EVENT_LOGIN_PIN_REQUEST:
            brls::Logger::info("EventCB CHIAKI_EVENT_LOGIN_PIN_REQUEST");
            if (onLoginPinRequest)
            {
                onLoginPinRequest(event->login_pin_request.pin_incorrect);
            }
            break;

        case CHIAKI_EVENT_RUMBLE:
            brls::Logger::info("RUMBLE EVENT: left={}, right={}", event->rumble.left, event->rumble.right);
            brls::Logger::debug("EventCB CHIAKI_EVENT_RUMBLE");
            if (onRumble)
            {
                onRumble(event->rumble.left, event->rumble.right);
            }
            break;

        case CHIAKI_EVENT_QUIT:
            brls::Logger::info("EventCB CHIAKI_EVENT_QUIT");
            if (onQuit)
            {
                onQuit(&event->quit);
            }
            break;
    }
}

void Host::registCallback(ChiakiRegistEvent* event)
{
    registered = false;

    switch (event->type)
    {
        case CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED:
            brls::Logger::info("Register event CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED");
            if (onRegistCanceled)
            {
                onRegistCanceled();
            }
            break;

        case CHIAKI_REGIST_EVENT_TYPE_FINISHED_FAILED:
            brls::Logger::info("Register event CHIAKI_REGIST_EVENT_TYPE_FINISHED_FAILED");
            if (onRegistFailed)
            {
                onRegistFailed();
            }
            break;

        case CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS:
        {
            ChiakiRegisteredHost* rHost = event->registered_host;
            brls::Logger::info("Register event CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS");

            apSsid = rHost->ap_ssid;
            apKey = rHost->ap_key;
            apName = rHost->ap_name;
            memcpy(serverMac, rHost->server_mac, sizeof(serverMac));
            serverNickname = rHost->server_nickname;
            memcpy(rpRegistKey, rHost->rp_regist_key, sizeof(rpRegistKey));
            rpKeyType = rHost->rp_key_type;
            memcpy(rpKey, rHost->rp_key, sizeof(rpKey));

            registered = true;
            rpKeyData = true;

            brls::Logger::info("Register Success {}", hostName);

            if (onRegistSuccess)
            {
                onRegistSuccess();
            }
            break;
        }
    }

    // Close registration socket
    chiaki_regist_stop(&regist);
    chiaki_regist_fini(&regist);
}
