#include "views/stream_view.hpp"
#include "views/stream_nav.hpp"
#include "views/controller_picker_view.hpp"
#include "views/players_panel_view.hpp"
#include "input/extended_input_manager.hpp"
#include "input/pad_path.hpp"
#include "views/stream_menu.hpp"
#include "views/connecting_view.hpp"
#include "ui/theme.hpp"
#include "views/connection_stage.hpp"
#include "stream/video_renderer.hpp"
#include "views/enter_pin_view.hpp"
#include "views/controller_remap_view.hpp"
#include <format>
#include "core/exception.hpp"
#include "core/wireguard_manager.hpp"
#include "stream/input_manager.hpp"
#include "core/discovery_manager.hpp"
#include "psn/auth.hpp"
#include "util/shared_view_holder.hpp"
#include <switch.h>
#include <thread>
#include <chrono>
#include <borealis/core/i18n.hpp>
using namespace brls::literals;



StreamView::StreamView(Host* host, std::shared_ptr<Host> hostOwner)
    : host(host)
    , hostOwner(std::move(hostOwner))
{
    this->session = Session::GetInstance();
    this->settings = SettingsManager::getInstance();

    setFocusable(true);
}

void StreamView::setupCallbacks()
{
    auto weak = weak_from_this();

    host->setOnConnected([weak]() {
        if (auto self = weak.lock()) {
            self->onConnected();
        }
    });

    host->setOnQuit([weak](ChiakiQuitEvent* event) {
        if (auto self = weak.lock()) {
            self->onQuit(event);
        }
    });

    host->setOnRumble([weak](uint8_t player_index, uint8_t left, uint8_t right) {
        if (auto self = weak.lock()) {
            self->onRumble(player_index, left, right);
        }
    });

    host->setOnLoginPinRequest([weak](bool pinIncorrect) {
        if (auto self = weak.lock()) {
            self->onLoginPinRequest(pinIncorrect);
        }
    });

    host->setOnPadPasscodeRequest([weak](uint8_t slot, bool retry) {
        if (auto self = weak.lock()) {
            self->onPadPasscodeRequest(slot, retry);
        }
    });

    host->setOnPadJoinFailed([weak](uint8_t slot, uint8_t status) {
        if (auto self = weak.lock()) {
            self->onPadJoinFailed(slot, status);
        }
    });

    host->setOnReadController([weak](ChiakiControllerState* state, std::map<uint32_t, int8_t>* fingerIdTouchId) {
        if (auto self = weak.lock()) {
            self->session->UpdateControllerState(state, fingerIdTouchId);
        }
    });

    host->setOnReadCouchSecondary([weak](ChiakiControllerState* states, uint8_t* count) {
        if (auto self = weak.lock()) {
            self->session->UpdateSecondaryPads(states, count);
        }
    });

    session->getInputManager()->setTargetPS5(host->isPS5());

    session->getInputManager()->setOnPadArrived([weak](HidNpadIdType npad) {
        if (auto self = weak.lock())
            self->onPadArrived(npad);
    });

    session->getInputManager()->setOnCouchDisconnected([weak](HidNpadIdType, uint8_t slot) {
        if (auto self = weak.lock()) {
            if (self->host)
                self->host->couchRemovePlayer(slot);
        }
    });

    host->setOnTriggerEffects([weak](const ChiakiTriggerEffectsEvent* effects) {
        auto self = weak.lock();
        if (!self)
            return;

        if (effects->player_index != 0) {
            if (!self->host || !self->host->isCouchPadConfirmed(effects->player_index))
                return;
            auto* input = self->session ? self->session->getInputManager() : nullptr;
            if (!input)
                return;

            uint8_t addr[6];
            uint16_t vid = 0, pid = 0;
            if (!input->couchSlotOutputTarget(effects->player_index, addr, &vid, &pid))
                return;

            input->extendedInput().registerCouchOutput(addr, vid, pid);
            input->extendedInput().setCouchTriggerEffects(addr,
                effects->type_left, effects->left,
                effects->type_right, effects->right);
            return;
        }
        self->session->SetTriggerEffects(effects);
    });

    host->setOnPadConfirmed([weak](uint8_t index, uint8_t red, uint8_t green, uint8_t blue) {
        auto self = weak.lock();
        if (!self || index == 0)
            return;
        if (!self->host || !self->host->isCouchPadConfirmed(index))
            return;
        if ((red | green | blue) == 0)
            return;

        auto* input = self->session ? self->session->getInputManager() : nullptr;
        if (!input)
            return;

        uint8_t addr[6];
        uint16_t vid = 0, pid = 0;
        if (!input->couchSlotOutputTarget(index, addr, &vid, &pid))
            return;

        input->extendedInput().registerCouchOutput(addr, vid, pid);
        input->extendedInput().setCouchLightbar(addr, red, green, blue);
    });

    host->setOnEffectIntensity([weak](uint8_t haptics, uint8_t triggers) {
        if (auto self = weak.lock()) {
            self->session->SetEffectIntensity(haptics, triggers);
        }
    });

    host->setOnLedColor([weak](uint8_t red, uint8_t green, uint8_t blue) {
        if (auto self = weak.lock()) {
            self->session->SetLedColor(red, green, blue);
        }
    });

    host->setOnMotionReset([weak]() {
        if (auto self = weak.lock()) {
            if (self->session->getInputManager()) {
                self->session->getInputManager()->resetMotionControls();
            }
        }
    });

    exitSubscription = brls::Application::getExitEvent()->subscribe([weak]() {
        if (auto self = weak.lock()) {
            if (self->sessionStarted && self->settings->getSleepOnExit() && !self->host->isCloud()) {
                self->host->gotoBed();
            }
        }
    });

    focusSubscription = brls::Application::getWindowFocusChangedEvent()->subscribe([weak](bool focused) {
        if (auto self = weak.lock()) {
            self->onFocusChanged(focused);
        }
    });

    // Subscribe to logs for display while waiting for first frame
    logSubscription = brls::Logger::subscribeToLog(
        [weak](brls::Logger::TimePoint time, brls::LogLevel level, std::string msg) {
            if (auto self = weak.lock()) {
                std::lock_guard<std::mutex> lock(self->logMutex);
                self->logLines.push_back(msg);
                while (self->logLines.size() > MAX_LOG_LINES) {
                    self->logLines.pop_front();
                }
                self->currentStage = static_cast<int>(matchConnectionStage(
                    msg, static_cast<ConnectionStage>(self->currentStage.load())));
            }
        });
}

StreamView::~StreamView()
{
    brls::Application::getExitEvent()->unsubscribe(exitSubscription);
    brls::Application::getWindowFocusChangedEvent()->unsubscribe(focusSubscription);
    brls::Logger::unsubscribeFromLog(logSubscription);

    stopStream();

    // Release shared_ptr held by SharedViewHolder
    SharedViewHolder::release(this);
}

brls::View* StreamView::create()
{
    // This requires a host to be passed, so we return nullptr for XML creation
    return nullptr;
}

bool StreamView::beginPadChoice()
{
    if (padChoiceDone)
        return false;

    if (session == nullptr) {
        padChoiceDone = true;
        return false;
    }

    if (!controllerReady) {
        if (!session->InitController()) {
            padChoiceDone = true;
            return false;
        }
        controllerReady = true;
    }

    waitForPads(0);
    return true;
}

void StreamView::waitForPads(int attempt)
{
    constexpr int kMaxAttempts = 20;
    constexpr int kIntervalMs  = 100;

    auto* input = session ? session->getInputManager() : nullptr;
    if (input == nullptr) {
        finishPadChoice();
        return;
    }

    const auto availability = input->extendedInput().availability();
    const bool backendUsable =
        availability == ExtendedInputManager::Availability::Available;

    if (!backendUsable) {
        brls::Logger::info("PadChoice: no backend to wait for, deciding now");
        finishPadChoice();
        return;
    }

    for (const auto& pad : input->describePads()) {
        if (pad.kind == akira::input::PadPathKind::McPsNative ||
            pad.kind == akira::input::PadPathKind::McGeneric) {
            brls::Logger::info("PadChoice: backend named a pad after {}ms",
                               attempt * kIntervalMs);
            finishPadChoice();
            return;
        }
    }

    if (attempt >= kMaxAttempts) {
        brls::Logger::info("PadChoice: no backend pad after {}ms, deciding anyway",
                           attempt * kIntervalMs);
        finishPadChoice();
        return;
    }

    auto weak = weak_from_this();
    brls::delay(kIntervalMs, [weak, attempt]() {
        if (auto self = weak.lock())
            self->waitForPads(attempt + 1);
    });
}

void StreamView::finishPadChoice()
{
    padChoiceDone = true;

    auto* input = session ? session->getInputManager() : nullptr;
    if (input == nullptr) {
        startStream();
        return;
    }

    auto pads = input->describePads();

    {
        AkiraInputDeviceList devices{};
        if (input->extendedInput().listDevices(&devices)) {
            brls::Logger::info("PadChoice: backend lists {} device(s)", devices.count);
            for (uint8_t i = 0; i < devices.count && i < AKIRA_INPUT_MAX_LISTED_DEVICES; i++) {
                const AkiraInputDeviceInfo& d = devices.devices[i];
                brls::Logger::info(
                    "PadChoice:   {:04x}:{:04x} report=0x{:02x} flags=0x{:02x}{}{}{}",
                    d.vendor_id, d.product_id, d.report_id, d.flags,
                    (d.flags & AkiraInputDevice_Identified) ? " identified" : "",
                    (d.flags & AkiraInputDevice_Reporting)  ? " reporting"  : "",
                    (d.flags & AkiraInputDevice_Claimed)    ? " claimed"    : "");
            }
        } else {
            brls::Logger::warning("PadChoice: listDevices failed");
        }
    }

    for (const auto& pad : pads) {
        brls::Logger::info("PadChoice: npad {} {} [{}{}]",
                           (int)pad.npad, pad.label,
                           pad.caps.analog_triggers ? "analog " : "",
                           pad.caps.touchpad ? "touch " : "");
    }

    if (!ControllerPickerView::worthAsking(pads)) {
        brls::Logger::info("PadChoice: {} pad(s), nothing worth asking", pads.size());
        for (const auto& pad : pads) {
            if (pad.caps.analog_triggers || pad.caps.touchpad) {
                input->selectNpad(pad.npad);
                break;
            }
        }
        startStream();
        return;
    }

    brls::Logger::info("PadChoice: asking between {} pads", pads.size());

    auto weak = weak_from_this();
    auto describe = [weak]() -> std::vector<akira::input::PadDescription> {
        if (auto self = weak.lock()) {
            if (auto* mgr = self->session ? self->session->getInputManager() : nullptr)
                return mgr->describePads();
        }
        return {};
    };

    auto* picker = new ControllerPickerView(pads, [weak](HidNpadIdType npad) {
        auto self = weak.lock();

        if (npad == ControllerPickerView::kCancelled) {
            if (self)
                self->abandonBeforeStart();
            else
                brls::Application::popActivity();
            return;
        }

        brls::Application::popActivity();

        if (!self)
            return;

        if (npad != ControllerPickerView::kNoChoice) {
            if (auto* mgr = self->session ? self->session->getInputManager() : nullptr)
                mgr->selectNpad(npad);
        }
        self->startStream();
    }, describe);

    brls::Application::pushActivity(new brls::Activity(picker),
                                    brls::TransitionAnimation::NONE);
}

void StreamView::startStream()
{
    if (sessionStarted)
    {
        return;
    }

    if (beginPadChoice())
    {
        return;
    }

    if (sessionPreStarted)
    {
        brls::Logger::info("Session already started while connecting, activating stream...");
        sessionStarted = true;
        streamActive = true;
        prepareVideoPipelineTick();
        brls::Application::blockInputs(true);
        brls::Logger::info("Stream activated successfully");
        return;
    }

    brls::Logger::info("Starting stream to {}", host->getHostName());

    try
    {
        auto profile = SettingsManager::StreamProfile::Local;
        if (host->isCloud()) {
            profile = SettingsManager::StreamProfile::Cloud;
            const auto* cloudConfig = host->getCloudSessionConfig();
            settings->setActiveCloudPscloud(
                cloudConfig && cloudConfig->serviceType == CHIAKI_SERVICE_TYPE_PSCLOUD);
        }
        else if (host->isRemote())
            profile = SettingsManager::StreamProfile::Remote;
        else if (WireGuardManager::instance().isConnected())
            profile = SettingsManager::StreamProfile::Vpn;
        settings->setActiveStreamProfile(profile);

        if (!controllerReady)
        {
            if (!session->InitController())
            {
                brls::Logger::error("Failed to initialize controller");
                throw Exception("akira/stream/failed_init_controller"_i18n);
            }
            controllerReady = true;
        }

        if (host->isRemote())
        {
            if (host->getHolepunchSession() != nullptr)
            {
                brls::Logger::info("Holepunch already completed, initializing session...");
            }
            else
            {
                brls::Logger::info("Remote host detected, initiating holepunch connection...");

                if (!psn::Auth::instance().tokenValid())
                {
                    brls::Logger::error("PSN token not valid for remote connection");
                    throw Exception("akira/stream/psn_token_expired"_i18n);
                }

                ChiakiErrorCode err = host->connectHolepunch();
                if (err != CHIAKI_ERR_SUCCESS)
                {
                    brls::Logger::error("Holepunch connection failed: {}", chiaki_error_string(err));
                    throw Exception(brls::getStr("akira/stream/holepunch_failed", chiaki_error_string(err)));
                }

                brls::Logger::info("Holepunch successful!");
            }

            brls::Logger::info("Initializing session with holepunch...");
            host->initSessionWithHolepunch(session, host->getHolepunchSession());
        }
        else
        {
            host->initSession(session);
        }

        host->startSession();

        sessionStarted = true;
        streamActive = true;
        prepareVideoPipelineTick();

        brls::Application::blockInputs(true);

        brls::Logger::info("Stream started successfully");
    }
    catch (const Exception& e)
    {
        brls::Logger::error("Failed to start stream: {}", e.what());

        session->FreeController();
        host->cleanupHolepunch();
        host->finiSession();

        std::string errorMsg = e.what();
        brls::sync([errorMsg]() {
            auto* dialog = new brls::Dialog(brls::getStr("akira/stream/connection_failed", errorMsg));
            dialog->addButton("OK", []() {
                akira::views::stream_nav::unwindToBase();
            });
            dialog->open();
            brls::Application::forceUnblockInputs();
        });
    }
}

void StreamView::stopStream()
{
    if (!sessionStarted)
    {
        return;
    }

    brls::Logger::info("Stopping stream");

    brls::Application::forceUnblockInputs();
    brls::Application::setRenderSuspended(false);
    if (session)
    brls::Application::setSuspendedRenderCallback(nullptr);
    brls::Application::setLimitedFPS(0);
    brls::Application::setSwapInterval(1);
    brls::Application::setExclusiveRender(false);
    videoPipelineActive = false;

    streamActive = false;

    host->stopSession();
    host->finiSession();
    host->cleanupHolepunch();

    session->FreeController();
    session->FreeVideo();

    controllerReady = false;

    sessionStarted = false;
}

void StreamView::capturePausedFrame()
{
    pausedFrameRGBA.clear();
    pausedFrameW = 0;
    pausedFrameH = 0;

    if (!session)
        return;

    IVideoRenderer* renderer = session->getVideoRenderer();
    if (!renderer)
        return;

    if (!renderer->captureLastFrame(pausedFrameRGBA, pausedFrameW, pausedFrameH))
    {
        pausedFrameRGBA.clear();
        pausedFrameW = 0;
        pausedFrameH = 0;
    }
}

void StreamView::releasePausedFrame(NVGcontext* vg)
{
    if (pausedFrameImage >= 0 && vg)
        nvgDeleteImage(vg, pausedFrameImage);
    pausedFrameImage = -1;
    pausedFrameRGBA.clear();
    pausedFrameRGBA.shrink_to_fit();
    pausedFrameW = 0;
    pausedFrameH = 0;
}

bool StreamView::drawPausedFrame(NVGcontext* vg, float x, float y, float width, float height)
{
    if (pausedFrameImage < 0 && !pausedFrameRGBA.empty() && pausedFrameW > 0 && pausedFrameH > 0)
    {
        pausedFrameImage = nvgCreateImageRGBA(vg, pausedFrameW, pausedFrameH, 0, pausedFrameRGBA.data());
        if (pausedFrameImage < 0)
            brls::Logger::warning("StreamView: failed to create paused frame image");
        pausedFrameRGBA.clear();
        pausedFrameRGBA.shrink_to_fit();
    }

    if (pausedFrameImage < 0)
        return false;

    NVGpaint paint = nvgImagePattern(vg, x, y, width, height, 0.0f, pausedFrameImage, 1.0f);
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillPaint(vg, paint);
    nvgFill(vg);
    return true;
}

void StreamView::draw(NVGcontext* vg, float x, float y, float width, float height,
                      brls::Style style, brls::FrameContext* ctx)
{
    static int drawCount = 0;
    ++drawCount;
    if (settings->getDebugRenderLog() && drawCount % 60 == 0) {
        brls::Logger::info("StreamView::draw #{}: streamActive={}, sessionStarted={}, menuOpen={}",
            drawCount, streamActive, sessionStarted, menuOpen);
    }

    if (!streamActive || !sessionStarted)
    {
        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, height);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
        nvgFill(vg);

        if (settings->getConnectionShowStages()) {
            ConnectionStage stage = static_cast<ConnectionStage>(currentStage.load());
            int raw = static_cast<int>(stage);
            int idx = raw < 1 ? 1 : (raw > 6 ? 6 : raw);
            drawConnectionRing(vg, x + width / 2, y + height / 2,
                brls::getStr(connectionStageLabelKey(stage)), idx, 6);
        } else {
            nvgFontSize(vg, 24);
            nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
            std::string connectingText = "akira/stream/connecting"_i18n;
            nvgText(vg, x + width / 2, y + 40, connectingText.c_str(), nullptr);
            renderLogs(vg, x, y + 90, width, height - 90);
        }

        return;
    }

    if (!menuOpen) {
        checkMenuTrigger();
    }

    static bool wasMenuOpen = false;
    if (menuOpen)
    {
        wasMenuOpen = true;
        if (!drawPausedFrame(vg, x, y, width, height))
        {
            nvgBeginPath(vg);
            nvgRect(vg, x, y, width, height);
            nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
            nvgFill(vg);
        }
        return;
    }

    if (wasMenuOpen)
    {
        brls::Logger::info("StreamView::draw: resuming after menu close, stats_overlay={}",
            (int)session->getStatsOverlayMode());
        wasMenuOpen = false;
        releasePausedFrame(vg);

        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, height);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
        nvgFill(vg);
        brls::Logger::info("StreamView::draw: skipping first frame after menu");
        return;
    }

    if (!session->hasReceivedFirstFrame())
    {
        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, height);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
        nvgFill(vg);

        if (settings->getConnectionShowStages()) {
            ConnectionStage stage = static_cast<ConnectionStage>(currentStage.load());
            int raw = static_cast<int>(stage);
            int idx = raw < 1 ? 1 : (raw > 6 ? 6 : raw);
            drawConnectionRing(vg, x + width / 2, y + height / 2,
                brls::getStr(connectionStageLabelKey(stage)), idx, 6);
        } else {
            renderLogs(vg, x, y, width, height);
        }

        session->MainLoop();
        return;
    }

    activateVideoPipeline();
}

void StreamView::prepareVideoPipelineTick()
{
    auto weak = weak_from_this();
    brls::Application::setSuspendedRenderCallback([weak]() {
        if (auto self = weak.lock())
            self->streamingTick();
    });
    brls::Application::setLimitedFPS(60);
    brls::Application::setSwapInterval(0);
    brls::Application::setExclusiveRender(true);
}

void StreamView::activateVideoPipeline()
{
    if (videoPipelineActive)
        return;

    prepareVideoPipelineTick();
    brls::Application::setRenderSuspended(true);
    videoPipelineActive = true;
}

void StreamView::streamingTick()
{
    checkMenuTrigger();
    if (menuOpen)
    {
        brls::Application::setSwapInterval(1);
        brls::Application::setExclusiveRender(false);
        brls::Application::setRenderSuspended(false);
        videoPipelineActive = false;
        return;
    }

    host->sendFeedbackState();

    if (auto* input = session->getInputManager())
    {
        if (input->extendedInput().consumeDegradedNotice())
        {
            brls::sync([]() {
                brls::Application::notify("akira/settings/analog_triggers_degraded"_i18n);
            });
        }
    }

    if (!session->MainLoop())
    {
        brls::Application::setSwapInterval(1);
        brls::Application::setExclusiveRender(false);
        brls::Application::setRenderSuspended(false);
        brls::Application::setSuspendedRenderCallback(nullptr);
        brls::Application::setLimitedFPS(0);
        videoPipelineActive = false;
        intentionalDisconnect = true;
        brls::sync([this]() {
            stopStream();
            akira::views::stream_nav::unwindToBase();
        });
    }
}

void StreamView::onConnected()
{
    std::string hostName = host->getHostName();
    brls::Logger::info("Connected to {}", hostName);
    brls::sync([hostName]() {
        brls::Application::notify(brls::getStr("akira/stream/connected_to", hostName));
    });
}

void StreamView::onQuit(ChiakiQuitEvent* event)
{
    brls::Logger::info("Session quit: reason={}", static_cast<int>(event->reason));

    streamActive = false;


    std::string reasonStr;
    switch (event->reason)
    {
        case CHIAKI_QUIT_REASON_NONE:
            reasonStr = "akira/stream/quit_none"_i18n;
            break;
        case CHIAKI_QUIT_REASON_STOPPED:
            reasonStr = "akira/stream/quit_stopped"_i18n;
            break;
        case CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN:
            reasonStr = "akira/stream/quit_request_unknown"_i18n;
            break;
        case CHIAKI_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED:
            reasonStr = "akira/stream/quit_connection_refused"_i18n;
            break;
        case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE:
            reasonStr = "akira/stream/quit_rp_in_use"_i18n;
            break;
        case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_CRASH:
            reasonStr = "akira/stream/quit_rp_crash"_i18n;
            break;
        case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_VERSION_MISMATCH:
            reasonStr = "akira/stream/quit_version_mismatch"_i18n;
            break;
        case CHIAKI_QUIT_REASON_CTRL_UNKNOWN:
            reasonStr = "akira/stream/quit_ctrl_unknown"_i18n;
            break;
        case CHIAKI_QUIT_REASON_CTRL_CONNECT_FAILED:
            reasonStr = "akira/stream/quit_ctrl_connect_failed"_i18n;
            break;
        case CHIAKI_QUIT_REASON_CTRL_CONNECTION_REFUSED:
            reasonStr = "akira/stream/quit_ctrl_connection_refused"_i18n;
            break;
        case CHIAKI_QUIT_REASON_STREAM_CONNECTION_UNKNOWN:
            reasonStr = "akira/stream/quit_stream_unknown"_i18n;
            break;
        case CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_DISCONNECTED:
            reasonStr = "akira/stream/quit_remote_disconnected"_i18n;
            break;
        case CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN:
            reasonStr = "akira/stream/quit_remote_shutdown"_i18n;
            break;
        case CHIAKI_QUIT_REASON_PSN_REGIST_FAILED:
            reasonStr = "akira/stream/quit_psn_regist_failed"_i18n;
            break;
        default:
            reasonStr = brls::getStr("akira/stream/quit_unknown", static_cast<int>(event->reason));
            break;
    }

    ChiakiQuitReason reason = event->reason;

    uint32_t gen = sessionGeneration;
    auto weak = weak_from_this();
    brls::sync([weak, reasonStr, reason, gen]() {
        auto self = weak.lock();
        if (!self) {
            brls::Logger::info("onQuit: StreamView already destroyed, skipping");
            return;
        }

        if (gen != self->sessionGeneration) {
            brls::Logger::info("onQuit: stale session (gen {} vs {}), skipping", gen, self->sessionGeneration);
            return;
        }

        if (!self->sessionStarted) {
            brls::Logger::info("onQuit: session already stopped via menu, skipping");
            return;
        }

        bool rejectedWhileAwake = self->host->isAwake() &&
            reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN;

        if (!self->host->isCloud() && !rejectedWhileAwake &&
            (reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED ||
             reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN) &&
            self->wakeRetryCount < MAX_WAKE_RETRIES) {
            brls::Logger::info("Connection failed (reason={}) - attempting wake and retry ({}/{})",
                              static_cast<int>(reason), self->wakeRetryCount + 1, MAX_WAKE_RETRIES);
            self->retryWithWake();
            return;
        }

        if (self->loginPinEnteredThisSession && reason != CHIAKI_QUIT_REASON_STOPPED) {
            brls::Logger::info("onQuit: first-time login PIN was entered, auto-reconnecting");
            self->loginPinEnteredThisSession = false;
            self->attemptReconnect();
            return;
        }

        bool duringConnect = !self->videoPipelineActive;

        self->stopStream();

        // Release early to invalidate weak_ptrs before popActivity
        SharedViewHolder::release(self.get());

        if (reason == CHIAKI_QUIT_REASON_STOPPED) {
            brls::Application::notify(reasonStr);
            akira::views::stream_nav::unwindToBase();
        } else {
            std::string body = duringConnect
                ? brls::getStr("akira/connection/connect_failed_title",
                    brls::getStr(rejectedWhileAwake
                        ? "akira/connection/fail_rp_disabled"
                        : connectionFailureKeyForReason(reason)))
                : brls::getStr("akira/stream/session_ended", reasonStr);
            auto* dialog = new brls::Dialog(body);
            dialog->setCloseCallback([]() {
                akira::views::stream_nav::unwindToBase();
            });
            dialog->addButton("OK", []() {
                akira::views::stream_nav::unwindToBase();
            });
            brls::Application::forceUnblockInputs();
            dialog->open();
        }
    });
}

void StreamView::onRumble(uint8_t player_index, uint8_t left, uint8_t right)
{
    if (player_index != 0)
    {
        if (!host || !host->isCouchPadConfirmed(player_index))
            return;
        auto* input = session ? session->getInputManager() : nullptr;
        if (!input)
            return;

        uint8_t addr[6];
        uint16_t vid = 0, pid = 0;
        if (!input->couchSlotOutputTarget(player_index, addr, &vid, &pid))
            return;

        input->extendedInput().registerCouchOutput(addr, vid, pid);
        input->extendedInput().setCouchRumble(addr, left, right);
        return;
    }
    session->SetRumble(left, right);
}

void StreamView::pauseStreamForUi()
{
    menuOpen = true;
    if (session)
        session->setVideoPaused(true);
    brls::Application::forceUnblockInputs();
}

void StreamView::resumeStreamAfterUi()
{
    if (session)
        session->setVideoPaused(false);
    menuOpen = false;
    brls::Application::blockInputs(true);
}

void StreamView::onPadArrived(HidNpadIdType npad)
{
    if (!host || !host->isCouchPadSlotAvailable()) {
        brls::Logger::info("Couch: pad arrived on npad {} but no slot is free", (int)npad);
        return;
    }

    if (couchArrivalPromptOpen || menuOpen) {
        brls::Logger::info("Couch: pad arrived on npad {} while UI is busy, not prompting", (int)npad);
        return;
    }
    couchArrivalPromptOpen = true;

    auto weak = weak_from_this();
    brls::sync([weak]() {
        auto self = weak.lock();
        if (!self) {
            return;
        }

        self->pauseStreamForUi();

        auto* dialog = new brls::Dialog("akira/couch/controller_arrived"_i18n);

        dialog->addButton("akira/couch/add_player"_i18n, [weak]() {
            auto self = weak.lock();
            if (!self)
                return;
            self->couchArrivalPromptOpen = false;
            self->openCouchClaim();
        });

        dialog->addButton("akira/couch/not_now"_i18n, [weak]() {
            auto self = weak.lock();
            if (!self)
                return;
            self->couchArrivalPromptOpen = false;
            auto* input = self->session ? self->session->getInputManager() : nullptr;
            if (input)
                input->resolvePadArrival();
            if (input && input->couchRoster().empty()) {
                self->openPrimaryControllerPicker();
                return;
            }
            self->resumeStreamAfterUi();
        });

        dialog->setCloseCallback([weak]() {
            auto self = weak.lock();
            if (!self)
                return;
            self->couchArrivalPromptOpen = false;
            if (auto* input = self->session ? self->session->getInputManager() : nullptr)
                input->resolvePadArrival();
            self->resumeStreamAfterUi();
        });

        brls::Logger::info("Couch: showing controller arrival prompt");
        dialog->open();
    });
}

void StreamView::openCouchClaim()
{
    auto* input = session ? session->getInputManager() : nullptr;
    if (input == nullptr) {
        brls::Logger::warning("StreamView: no input manager for couch claim");
        resumeStreamAfterUi();
        return;
    }

    auto weak = weak_from_this();
    auto* panel = new PlayersPanelView(host, input, true);
    panel->setOnClosed([weak]() {
        auto self = weak.lock();
        if (!self)
            return;
        if (auto* mgr = self->session ? self->session->getInputManager() : nullptr)
            mgr->resolvePadArrival();
        self->resumeStreamAfterUi();
    });
    brls::Application::pushActivity(new brls::Activity(panel), brls::TransitionAnimation::NONE);
}

void StreamView::openPrimaryControllerPicker()
{
    auto* input = session ? session->getInputManager() : nullptr;
    if (!input) {
        resumeStreamAfterUi();
        return;
    }

    auto pads = input->describePads();
    if (!ControllerPickerView::worthAsking(pads)) {
        brls::Logger::info("Couch: no alternate primary controller worth asking about");
        resumeStreamAfterUi();
        return;
    }

    brls::Logger::info("Couch: opening primary controller picker after Not now");
    auto weak = weak_from_this();
    auto describe = [weak]() -> std::vector<akira::input::PadDescription> {
        if (auto self = weak.lock()) {
            if (auto* mgr = self->session ? self->session->getInputManager() : nullptr)
                return mgr->describePads();
        }
        return {};
    };

    auto* picker = new ControllerPickerView(pads, [weak](HidNpadIdType npad) {
        brls::Application::popActivity(brls::TransitionAnimation::NONE);

        auto self = weak.lock();
        if (!self)
            return;
        if (npad != ControllerPickerView::kCancelled &&
            npad != ControllerPickerView::kNoChoice) {
            if (auto* mgr = self->session ? self->session->getInputManager() : nullptr)
                mgr->selectNpad(npad);
        }
        self->resumeStreamAfterUi();
    }, describe);

    brls::Application::pushActivity(new brls::Activity(picker),
                                    brls::TransitionAnimation::NONE);
}

void StreamView::onPadPasscodeRequest(uint8_t slot, bool retry)
{
    brls::Logger::info("Couch passcode request for player {} (retry: {})", slot + 1, retry);

    if (couchPasscodeDialogOpen) {
        brls::Logger::info("Couch passcode dialog already open, ignoring repeat request");
        return;
    }
    couchPasscodeDialogOpen = true;

    Host* hostPtr = host;
    auto weak = weak_from_this();

    brls::sync([weak, hostPtr, slot, retry]() {
        auto* pinView = new EnterPinView(hostPtr, PinViewType::CouchPasscode, retry);

        pinView->setOnPinEntered([weak, hostPtr, slot](const std::string& pin) {
            brls::Logger::info("Couch passcode entered for player {}", slot + 1);
            if (auto self = weak.lock())
                self->couchPasscodeDialogOpen = false;
            hostPtr->couchSendPasscode(slot, pin);
        });

        pinView->setOnCancel([weak, hostPtr, slot]() {
            brls::Logger::info("Couch passcode cancelled for player {}, releasing the pad", slot + 1);
            if (auto self = weak.lock())
                self->couchPasscodeDialogOpen = false;
            hostPtr->couchRemovePlayer(slot);
        });

        brls::Application::pushActivity(new brls::Activity(pinView));
    });
}

void StreamView::onPadJoinFailed(uint8_t slot, uint8_t status)
{
    brls::Logger::error("Couch join failed for player {} with status {}", slot + 1, status);

    std::string message = status == 1
        ? brls::getStr("akira/couch/join_failed_not_signed_in", slot + 1)
        : brls::getStr("akira/couch/join_failed_status", slot + 1, status);

    brls::sync([message]() {
        brls::Application::notify(message);
    });
}

void StreamView::onLoginPinRequest(bool pinIncorrect)
{
    brls::Logger::info("Login PIN request (incorrect: {})", pinIncorrect);

    if (host->isCloud()) {
        brls::Logger::warning("Ignoring login PIN request for a cloud session");
        return;
    }

    if (!pinIncorrect) {
        std::string savedPin = settings->getConsolePIN(host);
        if (!savedPin.empty()) {
            Host* hostPtr = host;
            brls::sync([hostPtr, savedPin]() {
                brls::Logger::info("Using saved Console PIN for auto-login");
                hostPtr->setLoginPIN(savedPin);
            });
            return;
        }
    }

    Host* hostPtr = host;
    SettingsManager* settingsPtr = settings;
    auto weak = weak_from_this();

    brls::sync([weak, hostPtr, settingsPtr, pinIncorrect]() {
        auto* pinView = new EnterPinView(hostPtr, PinViewType::Login, pinIncorrect);

        pinView->setOnPinEntered([weak, hostPtr, settingsPtr](const std::string& pin) {
            brls::Logger::info("Login PIN entered, sending to session");
            hostPtr->setLoginPIN(pin);

            if (auto self = weak.lock())
                self->loginPinEnteredThisSession = true;

            settingsPtr->setConsolePIN(hostPtr, pin);
            settingsPtr->writeFile();
        });

        pinView->setOnCancel([hostPtr]() {
            brls::Logger::info("Login PIN cancelled, stopping session");
            hostPtr->stopSession();
        });

        brls::Application::pushActivity(new brls::Activity(pinView));
    });
}

void StreamView::checkMenuTrigger()
{
    bool minusPressed = false;

    auto* input = session ? session->getInputManager() : nullptr;
    if (input != nullptr && input->path() != nullptr) {
        minusPressed = input->path()->menuHeld();
    } else {
        PadState pad;
        padInitializeDefault(&pad);
        padUpdate(&pad);
        minusPressed = (padGetButtons(&pad) & HidNpadButton_Minus) != 0;
    }

    static int checkCount = 0;
    if (minusPressed && checkCount++ % 30 == 0) {
        auto elapsed = minusWasHeld ?
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - minusHoldStart).count() : 0;
        brls::Logger::info("checkMenuTrigger: minus pressed, wasHeld={}, elapsed={}s", minusWasHeld, elapsed);
    }

    if (minusPressed) {
        if (!minusWasHeld) {
            minusHoldStart = std::chrono::steady_clock::now();
            minusWasHeld = true;
        } else {
            auto elapsed = std::chrono::steady_clock::now() - minusHoldStart;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= 3) {
                brls::Logger::info("checkMenuTrigger: 3 seconds reached, showing menu");
                showDisconnectMenu();
                minusWasHeld = false;  // Reset
            }
        }
    } else {
        minusWasHeld = false;
    }
}

void StreamView::showDisconnectMenu()
{
    brls::Logger::info("showDisconnectMenu: entering");
    capturePausedFrame();

    if (session)
    {
        IVideoRenderer* renderer = session->getVideoRenderer();
        if (renderer && renderer->takeOverlayPositionDirty())
            SettingsManager::getInstance()->writeFile();
    }
    menuOpen = true;
    session->setVideoPaused(true);
    session->CleanUpHaptic();
    brls::Application::forceUnblockInputs();

    auto* menu = new StreamMenu();
    menu->setSleepAvailable(!host->isCloud());

    menu->setConsoleName(host->getHostName());
    menu->setConsoleIsPs5(host->isPS5());
    menu->setStatsMode(session->getStatsOverlayMode());

    auto weak = weak_from_this();

    menu->setOnStatsToggle([weak](StatsOverlayMode mode) {
        if (auto self = weak.lock()) {
            brls::Logger::info("Stats overlay mode: {}", (int)mode);
            self->session->setStatsOverlayMode(mode);
        }
    });

    menu->setOnGyroReset([weak]() {
        if (auto self = weak.lock()) {
            brls::Logger::info("Gyro reset triggered from menu");
            if (self->session->getInputManager()) {
                self->session->getInputManager()->resetMotionControls();
            }
        }
    });

    menu->setOnButtonMapping([]() {
        auto* remapView = new ControllerRemapView();
        remapView->setTranslucent(true);
        remapView->setStreamMode(true);
        brls::Application::pushActivity(new brls::Activity(remapView), brls::TransitionAnimation::NONE);
    });

    menu->setOnPlayers([weak]() {
        auto self = weak.lock();
        if (!self)
            return;
        auto* input = self->session ? self->session->getInputManager() : nullptr;
        if (input == nullptr) {
            brls::Logger::warning("StreamMenu: no input manager for players panel");
            return;
        }
        auto* panel = new PlayersPanelView(self->host, input);
        brls::Application::pushActivity(new brls::Activity(panel), brls::TransitionAnimation::NONE);
    });

    menu->setOnDisconnect([weak](bool sleep) {
        if (auto self = weak.lock()) {
            brls::Logger::info("Disconnect requested, sleep={}", sleep);
            self->disconnectWithSleep(sleep);
        }
    });

    menu->setOnDismiss([weak]() {
        if (auto self = weak.lock()) {
            brls::Logger::info("Menu dismissed");
            self->session->setVideoPaused(false);
            self->menuOpen = false;
            brls::Application::blockInputs(true);
        }
    });

    brls::Application::pushActivity(new brls::Activity(menu));
    brls::Logger::info("showDisconnectMenu: menu opened");
}

void StreamView::abandonBeforeStart()
{
    brls::Logger::info("Pad choice abandoned before the stream started");

    intentionalDisconnect = true;
    padChoiceDone         = false;
    controllerReady       = false;

    if (session)
        session->FreeController();

    brls::Application::forceUnblockInputs();

    SharedViewHolder::release(this);

    akira::views::stream_nav::unwindToBase();
}



void StreamView::disconnectWithSleep(bool sleep)
{
    intentionalDisconnect = true;
    menuOpen = false;
    brls::Application::blockInputs(true);

    if (sleep && !host->isCloud()) {
        host->gotoBed();
    }

    stopStream();

    // Release early to invalidate weak_ptrs BEFORE activities are popped
    // This ensures any pending brls::sync tasks from chiaki callbacks
    // will fail weak.lock() and not access this object
    SharedViewHolder::release(this);

    brls::sync([]() { akira::views::stream_nav::unwindToBase(); });
}

void StreamView::renderLogs(NVGcontext* vg, float x, float y, float width, float height)
{
    std::lock_guard<std::mutex> lock(logMutex);

    nvgFontSize(vg, 16);
    nvgFillColor(vg, nvgRGBA(200, 200, 200, 255));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

    float lineHeight = 20;
    float startY = y + 20;

    // Show logs from bottom up (most recent at bottom)
    size_t startIdx = 0;
    size_t maxVisibleLines = static_cast<size_t>((height - 40) / lineHeight);
    if (logLines.size() > maxVisibleLines) {
        startIdx = logLines.size() - maxVisibleLines;
    }

    float currentY = startY;
    for (size_t i = startIdx; i < logLines.size(); i++) {
        nvgText(vg, x + 20, currentY, logLines[i].c_str(), nullptr);
        currentY += lineHeight;
    }
}

void StreamView::retryWithWake()
{
    wakeRetryCount++;
    wakeAttempted = true;

    host->finiSession();
    session->FreeController();

    if (wakeRetryCount == 1) {
        int wakeResult = host->wakeup();
        if (wakeResult != 0) {
            brls::Logger::error("Wake failed with code {}", wakeResult);
            stopStream();
            SharedViewHolder::release(this);
            auto* dialog = new brls::Dialog("akira/stream/wake_failed"_i18n);
            dialog->addButton("OK", []() {
                akira::views::stream_nav::unwindToBase();
            });
            brls::Application::forceUnblockInputs();
            dialog->open();
            return;
        }
        brls::Logger::info("Wake sent, retrying connection...");
    }

    int delaySeconds = 5 + (wakeRetryCount - 1) * 3;
    brls::Logger::info("Wake retry attempt {}/{}, waiting {} seconds...",
                       wakeRetryCount, MAX_WAKE_RETRIES, delaySeconds);
    brls::Application::notify(brls::getStr("akira/stream/waking_attempt", wakeRetryCount, MAX_WAKE_RETRIES));

    std::this_thread::sleep_for(std::chrono::seconds(delaySeconds));

    sessionStarted = false;
    streamActive = false;

    try {
        if (!session->InitController()) {
            throw Exception("akira/stream/failed_init_controller"_i18n);
        }
        host->initSession(session);
        host->startSession();
        sessionStarted = true;
        streamActive = true;
        brls::Logger::info("Retry connection started");
    } catch (const Exception& e) {
        brls::Logger::error("Retry attempt {} failed: {}", wakeRetryCount, e.what());
        session->FreeController();
        host->finiSession();

        if (wakeRetryCount >= MAX_WAKE_RETRIES) {
            SharedViewHolder::release(this);
            std::string errorMsg = e.what();
            auto* dialog = new brls::Dialog(brls::getStr("akira/stream/connection_failed_attempts", MAX_WAKE_RETRIES, errorMsg));
            dialog->addButton("OK", []() {
                akira::views::stream_nav::unwindToBase();
            });
            brls::Application::forceUnblockInputs();
            dialog->open();
        }
    }
}

void StreamView::onFocusChanged(bool focused)
{
    if (!focused)
        return;

    if (intentionalDisconnect || !settings->getAutoReconnect() || !sessionStarted || host->isCloud())
        return;

    bool socketHealthy = host->isSessionSocketHealthy();
    brls::Logger::info("StreamView::onFocusChanged(InFocus): streamActive={}, socketHealthy={}",
                       streamActive, socketHealthy);

    if (!streamActive || !socketHealthy) {
        brls::Logger::info("Stream dead on focus regain, prompting user");
        stopStream();
        brls::Application::forceUnblockInputs();

        auto weak = weak_from_this();
        auto* dialog = new brls::Dialog("akira/stream/disconnected_reconnect"_i18n);
        dialog->addButton("akira/stream/no"_i18n, [weak]() {
            if (auto self = weak.lock()) {
                SharedViewHolder::release(self.get());
            }
            akira::views::stream_nav::unwindToBase();
        });
        dialog->addButton("akira/stream/yes"_i18n, [weak]() {
            if (auto self = weak.lock()) {
                self->attemptReconnect();
            }
        });
        dialog->open();
    }
}

void StreamView::attemptReconnect()
{
    if (reconnecting) {
        brls::Logger::info("Already reconnecting, skipping");
        return;
    }

    reconnecting = true;

    brls::Logger::info("Auto-reconnect: tearing down old session");
    stopStream();

    sessionGeneration++;
    wakeRetryCount = 0;
    wakeAttempted = false;

    brls::Application::notify("akira/stream/reconnecting"_i18n);
    startStream();

    reconnecting = false;
}
