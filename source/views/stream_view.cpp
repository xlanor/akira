#include "views/stream_view.hpp"
#include "views/stream_menu.hpp"
#include "stream/video_renderer.hpp"
#include "views/enter_pin_view.hpp"
#include "views/controller_remap_view.hpp"
#include <format>
#include "core/exception.hpp"
#include "core/wireguard_manager.hpp"
#include "stream/input_manager.hpp"
#include "core/discovery_manager.hpp"
#include "util/shared_view_holder.hpp"
#include <switch.h>
#include <thread>
#include <chrono>
#include <borealis/core/i18n.hpp>
using namespace brls::literals;

StreamView::StreamView(Host* host)
    : host(host)
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

    host->setOnRumble([weak](uint8_t left, uint8_t right) {
        if (auto self = weak.lock()) {
            self->onRumble(left, right);
        }
    });

    host->setOnLoginPinRequest([weak](bool pinIncorrect) {
        if (auto self = weak.lock()) {
            self->onLoginPinRequest(pinIncorrect);
        }
    });

    host->setOnReadController([weak](ChiakiControllerState* state, std::map<uint32_t, int8_t>* fingerIdTouchId) {
        if (auto self = weak.lock()) {
            self->session->UpdateControllerState(state, fingerIdTouchId);
        }
    });

    session->getInputManager()->setTargetPS5(host->isPS5());

    host->setOnMotionReset([weak]() {
        if (auto self = weak.lock()) {
            if (self->session->getInputManager()) {
                self->session->getInputManager()->resetMotionControls();
            }
        }
    });

    exitSubscription = brls::Application::getExitEvent()->subscribe([weak]() {
        if (auto self = weak.lock()) {
            if (self->sessionStarted && self->settings->getSleepOnExit()) {
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
    logSubscription = brls::Logger::getLogEvent()->subscribe(
        [weak](brls::Logger::TimePoint time, brls::LogLevel level, std::string msg) {
            if (auto self = weak.lock()) {
                std::lock_guard<std::mutex> lock(self->logMutex);
                self->logLines.push_back(msg);
                while (self->logLines.size() > MAX_LOG_LINES) {
                    self->logLines.pop_front();
                }
            }
        });
}

StreamView::~StreamView()
{
    brls::Application::getExitEvent()->unsubscribe(exitSubscription);
    brls::Application::getWindowFocusChangedEvent()->unsubscribe(focusSubscription);
    brls::Logger::getLogEvent()->unsubscribe(logSubscription);

    stopStream();

    // Release shared_ptr held by SharedViewHolder
    SharedViewHolder::release(this);
}

brls::View* StreamView::create()
{
    // This requires a host to be passed, so we return nullptr for XML creation
    return nullptr;
}

void StreamView::startStream()
{
    if (sessionStarted)
    {
        return;
    }

    if (sessionPreStarted)
    {
        brls::Logger::info("Session already started by ConnectionView, activating stream...");
        sessionStarted = true;
        streamActive = true;
        brls::Application::blockInputs(true);
        brls::Logger::info("Stream activated successfully");
        return;
    }

    brls::Logger::info("Starting stream to {}", host->getHostName());

    try
    {
        auto profile = SettingsManager::StreamProfile::Local;
        if (host->isRemote())
            profile = SettingsManager::StreamProfile::Remote;
        else if (WireGuardManager::instance().isConnected())
            profile = SettingsManager::StreamProfile::Vpn;
        settings->setActiveStreamProfile(profile);

        if (!session->InitController())
        {
            brls::Logger::error("Failed to initialize controller");
            throw Exception("akira/stream/failed_init_controller"_i18n);
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

                auto* dm = DiscoveryManager::getInstance();
                if (!dm->isPsnTokenValid())
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
                brls::Application::popActivity();
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

    streamActive = false;

    host->stopSession();
    host->finiSession();
    host->cleanupHolepunch();

    session->FreeController();
    session->FreeVideo();

    sessionStarted = false;
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

        nvgFontSize(vg, 24);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        std::string connectingText = "akira/stream/connecting"_i18n;
        nvgText(vg, x + width / 2, y + height / 2, connectingText.c_str(), nullptr);

        return;
    }

    if (!menuOpen) {
        checkMenuTrigger();
    }

    static bool wasMenuOpen = false;
    if (menuOpen)
    {
        wasMenuOpen = true;
        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, height);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
        nvgFill(vg);
        return;
    }

    if (wasMenuOpen)
    {
        brls::Logger::info("StreamView::draw: resuming after menu close, stats_overlay={}",
            session->getShowStatsOverlay());
        wasMenuOpen = false;

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

        renderLogs(vg, x, y, width, height);

        session->MainLoop();
        return;
    }

    auto renderer = session->getVideoRenderer();
    if (renderer)
    {
        renderer->setTickCallback([this]() -> bool {
            checkMenuTrigger();
            if (menuOpen)
            {
                brls::Application::setExclusiveRender(false);
                session->getVideoRenderer()->setTickCallback(nullptr);
                return false;
            }

            host->sendFeedbackState();

            if (!session->MainLoop())
            {
                brls::Application::setExclusiveRender(false);
                session->getVideoRenderer()->setTickCallback(nullptr);
                intentionalDisconnect = true;
                brls::sync([this]() {
                    stopStream();
                    brls::Application::popActivity();
                });
                return false;
            }
            return true;
        });
        brls::Application::setExclusiveRender(true);
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

        if ((reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED ||
             reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN) &&
            self->wakeRetryCount < MAX_WAKE_RETRIES) {
            brls::Logger::info("Connection failed (reason={}) - attempting wake and retry ({}/{})",
                              static_cast<int>(reason), self->wakeRetryCount + 1, MAX_WAKE_RETRIES);
            self->retryWithWake();
            return;
        }

        self->stopStream();

        // Release early to invalidate weak_ptrs before popActivity
        SharedViewHolder::release(self.get());

        if (reason == CHIAKI_QUIT_REASON_STOPPED) {
            brls::Application::notify(reasonStr);
            brls::Application::popActivity();
        } else {
            auto* dialog = new brls::Dialog(brls::getStr("akira/stream/session_ended", reasonStr));
            dialog->setCloseCallback([]() {
                brls::Application::popActivity();
            });
            dialog->addButton("OK", []() {
                brls::Application::popActivity();
            });
            brls::Application::forceUnblockInputs();
            dialog->open();
        }
    });
}

void StreamView::onRumble(uint8_t left, uint8_t right)
{
    session->SetRumble(left, right);
}

void StreamView::onLoginPinRequest(bool pinIncorrect)
{
    brls::Logger::info("Login PIN request (incorrect: {})", pinIncorrect);

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

    brls::sync([hostPtr, settingsPtr, pinIncorrect]() {
        auto* pinView = new EnterPinView(hostPtr, PinViewType::Login, pinIncorrect);

        pinView->setOnPinEntered([hostPtr, settingsPtr](const std::string& pin) {
            brls::Logger::info("Login PIN entered, sending to session");
            hostPtr->setLoginPIN(pin);

            settingsPtr->setConsolePIN(hostPtr, pin);
            settingsPtr->writeFile();
        });

        pinView->setOnCancel([hostPtr]() {
            brls::Logger::info("Login PIN cancelled, stopping session");
            hostPtr->stopSession();
            brls::Application::popActivity();
        });

        brls::Application::pushActivity(new brls::Activity(pinView));
    });
}

void StreamView::checkMenuTrigger()
{
    PadState pad;
    padInitializeDefault(&pad);
    padUpdate(&pad);
    u64 buttons = padGetButtons(&pad);

    bool minusPressed = (buttons & HidNpadButton_Minus) != 0;

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
    menuOpen = true;
    session->setVideoPaused(true);
    session->CleanUpHaptic();
    brls::Application::forceUnblockInputs();

    auto* menu = new StreamMenu();

    menu->setStatsEnabled(session->getShowStatsOverlay());

    auto weak = weak_from_this();

    menu->setOnStatsToggle([weak](bool enabled) {
        if (auto self = weak.lock()) {
            brls::Logger::info("Stats overlay toggled: {}", enabled);
            self->session->setShowStatsOverlay(enabled);
            self->session->setVideoPaused(false);
            self->menuOpen = false;
            brls::Application::blockInputs(true);
        }
    });

    menu->setOnGyroReset([weak]() {
        if (auto self = weak.lock()) {
            brls::Logger::info("Gyro reset triggered from menu");
            if (self->session->getInputManager()) {
                self->session->getInputManager()->resetMotionControls();
            }
            self->session->setVideoPaused(false);
            self->menuOpen = false;
            brls::Application::blockInputs(true);
        }
    });

    menu->setOnButtonMapping([]() {
        auto* remapView = new ControllerRemapView();
        remapView->setTranslucent(true);
        remapView->setStreamMode(true);
        brls::Application::pushActivity(new brls::Activity(remapView), brls::TransitionAnimation::NONE);
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

void StreamView::disconnectWithSleep(bool sleep)
{
    intentionalDisconnect = true;
    menuOpen = false;
    brls::Application::blockInputs(true);

    if (sleep) {
        host->gotoBed();
    }

    stopStream();

    // Release early to invalidate weak_ptrs BEFORE activities are popped
    // This ensures any pending brls::sync tasks from chiaki callbacks
    // will fail weak.lock() and not access this object
    SharedViewHolder::release(this);
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
                brls::Application::popActivity();
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
                brls::Application::popActivity();
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

    if (intentionalDisconnect || !settings->getAutoReconnect() || !sessionStarted)
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
            brls::Application::popActivity();
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
