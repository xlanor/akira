#include "views/stream_view.hpp"
#include "views/stream_menu.hpp"
#include "views/enter_pin_view.hpp"
#include "core/exception.hpp"
#include "core/io/input_manager.hpp"
#include "core/discovery_manager.hpp"
#include "util/shared_view_holder.hpp"
#include <switch.h>

StreamView::StreamView(Host* host)
    : host(host)
{
    this->io = IO::GetInstance();
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
            self->io->UpdateControllerState(state, fingerIdTouchId);
        }
    });

    host->setOnMotionReset([weak]() {
        if (auto self = weak.lock()) {
            if (self->io->getInputManager()) {
                self->io->getInputManager()->resetMotionControls();
            }
        }
    });

    exitSubscription = brls::Application::getExitEvent()->subscribe([weak]() {
        if (auto self = weak.lock()) {
            if (self->sessionStarted) {
                brls::Logger::info("App exiting - putting PS5 to sleep");
                self->host->gotoBed();
            }
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
        if (!io->InitController())
        {
            brls::Logger::error("Failed to initialize controller");
            throw Exception("Failed to initialize controller");
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
                    throw Exception("PSN token expired. Please refresh in settings.");
                }

                ChiakiErrorCode err = host->connectHolepunch();
                if (err != CHIAKI_ERR_SUCCESS)
                {
                    brls::Logger::error("Holepunch connection failed: {}", chiaki_error_string(err));
                    throw Exception((std::string("Remote connection failed: ") + chiaki_error_string(err)).c_str());
                }

                brls::Logger::info("Holepunch successful!");
            }

            brls::Logger::info("Initializing session with holepunch...");
            host->initSessionWithHolepunch(io, host->getHolepunchSession());
        }
        else
        {
            host->initSession(io);
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

        io->FreeController();
        host->cleanupHolepunch();
        host->finiSession();

        std::string errorMsg = e.what();
        brls::sync([errorMsg]() {
            auto* dialog = new brls::Dialog("Connection Failed\n\n" + errorMsg);
            dialog->addButton("OK", []() {
                brls::Application::popActivity();
            });
            dialog->open();
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

    brls::Application::unblockInputs();

    streamActive = false;

    host->stopSession();
    host->finiSession();
    host->cleanupHolepunch();

    io->FreeController();
    io->FreeVideo();

    sessionStarted = false;
}

void StreamView::draw(NVGcontext* vg, float x, float y, float width, float height,
                      brls::Style style, brls::FrameContext* ctx)
{
    if (!streamActive || !sessionStarted)
    {
        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, height);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
        nvgFill(vg);

        nvgFontSize(vg, 24);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, x + width / 2, y + height / 2, "Connecting...", nullptr);

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
            io->getShowStatsOverlay());
        wasMenuOpen = false;

        // Skip one frame to let GPU state stabilize after menu closes
        // Draw black and return - next frame will render video
        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, height);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
        nvgFill(vg);
        brls::Logger::info("StreamView::draw: skipping first frame after menu");
        return;
    }

    // Show logs while waiting for first video frame
    // This doesnt seem to make much sense but its mostly for remote
    // so that people dont think that its a black screen doing nothing.
    if (!io->hasReceivedFirstFrame())
    {
        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, height);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
        nvgFill(vg);

        renderLogs(vg, x, y, width, height);

        // Still process frames so we detect when first one arrives
        io->MainLoop();
        return;
    }

    host->sendFeedbackState();

    if (!io->MainLoop())
    {
        brls::Logger::info("Quit requested via controller");
        stopStream();
        brls::Application::popActivity();
        return;
    }
}

void StreamView::onConnected()
{
    std::string hostName = host->getHostName();
    brls::Logger::info("Connected to {}", hostName);
    brls::sync([hostName]() {
        brls::Application::notify("Connected to " + hostName);
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
            reasonStr = "No error";
            break;
        case CHIAKI_QUIT_REASON_STOPPED:
            reasonStr = "Session stopped";
            break;
        case CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN:
            reasonStr = "Unknown session request";
            break;
        case CHIAKI_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED:
            reasonStr = "Connection refused";
            break;
        case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE:
            reasonStr = "Remote Play in use";
            break;
        case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_CRASH:
            reasonStr = "Remote Play crashed";
            break;
        case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_VERSION_MISMATCH:
            reasonStr = "Version mismatch";
            break;
        case CHIAKI_QUIT_REASON_CTRL_UNKNOWN:
            reasonStr = "Control connection error";
            break;
        case CHIAKI_QUIT_REASON_CTRL_CONNECT_FAILED:
            reasonStr = "Control connection failed";
            break;
        case CHIAKI_QUIT_REASON_CTRL_CONNECTION_REFUSED:
            reasonStr = "Control connection refused";
            break;
        case CHIAKI_QUIT_REASON_STREAM_CONNECTION_UNKNOWN:
            reasonStr = "Stream connection error";
            break;
        case CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_DISCONNECTED:
            reasonStr = "Remote disconnected";
            break;
        case CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN:
            reasonStr = "Remote shutdown";
            break;
        case CHIAKI_QUIT_REASON_PSN_REGIST_FAILED:
            reasonStr = "PSN registration failed";
            break;
        default:
            reasonStr = "Unknown error (code: " + std::to_string(static_cast<int>(event->reason)) + ")";
            break;
    }

    ChiakiQuitReason reason = event->reason;

    auto weak = weak_from_this();
    brls::sync([weak, reasonStr, reason]() {
        auto self = weak.lock();
        if (!self) {
            brls::Logger::info("onQuit: StreamView already destroyed, skipping");
            return;
        }

        if (!self->sessionStarted) {
            brls::Logger::info("onQuit: session already stopped via menu, skipping");
            return;
        }

        if (reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED && !self->wakeAttempted) {
            brls::Logger::info("Connection refused - attempting wake and retry");
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
            auto* dialog = new brls::Dialog("Session Ended\n\n" + reasonStr);
            dialog->addButton("OK", []() {
                brls::Application::popActivity();
            });
            dialog->open();
        }
    });
}

void StreamView::onRumble(uint8_t left, uint8_t right)
{
    io->SetRumble(left, right);
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

    if (minusPressed) {
        if (!minusWasHeld) {
            minusHoldStart = std::chrono::steady_clock::now();
            minusWasHeld = true;
        } else {
            auto elapsed = std::chrono::steady_clock::now() - minusHoldStart;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= 3) {
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
    brls::Application::unblockInputs();

    auto* menu = new StreamMenu();

    menu->setStatsEnabled(io->getShowStatsOverlay());

    auto weak = weak_from_this();

    menu->setOnStatsToggle([weak](bool enabled) {
        if (auto self = weak.lock()) {
            brls::Logger::info("Stats overlay toggled: {}", enabled);
            self->io->setShowStatsOverlay(enabled);
            self->menuOpen = false;
            brls::Application::blockInputs(true);
        }
    });

    menu->setOnGyroReset([weak]() {
        if (auto self = weak.lock()) {
            brls::Logger::info("Gyro reset triggered from menu");
            if (self->io->getInputManager()) {
                self->io->getInputManager()->resetMotionControls();
            }
            self->menuOpen = false;
            brls::Application::blockInputs(true);
        }
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
            self->menuOpen = false;
            brls::Application::blockInputs(true);
        }
    });

    brls::Application::pushActivity(new brls::Activity(menu));
    brls::Logger::info("showDisconnectMenu: menu opened");
}

void StreamView::disconnectWithSleep(bool sleep)
{
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
    wakeAttempted = true;

    host->finiSession();
    io->FreeController();

    int wakeResult = host->wakeup();
    if (wakeResult != 0) {
        brls::Logger::error("Wake failed with code {}", wakeResult);
        stopStream();
        SharedViewHolder::release(this);
        auto* dialog = new brls::Dialog("Failed to wake console");
        dialog->addButton("OK", []() {
            brls::Application::popActivity();
        });
        dialog->open();
        return;
    }

    brls::Logger::info("Wake sent, retrying connection...");
    brls::Application::notify("Waking console...");

    sessionStarted = false;
    streamActive = false;

    try {
        if (!io->InitController()) {
            throw Exception("Failed to initialize controller");
        }
        host->initSession(io);
        host->startSession();
        sessionStarted = true;
        streamActive = true;
        brls::Application::blockInputs(true);
        brls::Logger::info("Retry connection started");
    } catch (const Exception& e) {
        brls::Logger::error("Retry failed: {}", e.what());
        io->FreeController();
        host->finiSession();
        SharedViewHolder::release(this);
        std::string errorMsg = e.what();
        auto* dialog = new brls::Dialog("Connection Failed\n\n" + errorMsg);
        dialog->addButton("OK", []() {
            brls::Application::popActivity();
        });
        dialog->open();
    }
}
