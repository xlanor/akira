#include "views/stream_view.hpp"
#include "views/stream_menu.hpp"
#include "views/enter_pin_view.hpp"
#include "core/exception.hpp"
#include "core/io/input_manager.hpp"
#include "core/discovery_manager.hpp"
#include <switch.h>

StreamView::StreamView(Host* host)
    : host(host)
{
    this->io = IO::GetInstance();
    this->settings = SettingsManager::getInstance();

    setFocusable(true);

    ASYNC_RETAIN  // Creates token/tokenCounter, increments counter to 1
    (*deletionTokenCounter)++;  // Increment for onQuit (counter = 2)
    (*deletionTokenCounter)++;  // Increment for onLoginPinRequest (counter = 3)

    host->setOnConnected([this, token, tokenCounter]() {
        // Check if View was destroyed before accessing any members
        if (*token) {
            (*tokenCounter)--;
            if (*tokenCounter == 0) { delete token; delete tokenCounter; }
            return;
        }
        onConnected();
    });

    host->setOnQuit([this, token, tokenCounter](ChiakiQuitEvent* event) {
        // Check if View was destroyed before accessing any members
        if (*token) {
            (*tokenCounter)--;
            if (*tokenCounter == 0) { delete token; delete tokenCounter; }
            return;
        }
        onQuit(event);
    });

    host->setOnRumble([this](uint8_t left, uint8_t right) {
        onRumble(left, right);
    });

    host->setOnLoginPinRequest([this, token, tokenCounter](bool pinIncorrect) {
        if (*token) {
            (*tokenCounter)--;
            if (*tokenCounter == 0) { delete token; delete tokenCounter; }
            return;
        }
        onLoginPinRequest(pinIncorrect);
    });

    host->setOnReadController([this](ChiakiControllerState* state, std::map<uint32_t, int8_t>* fingerIdTouchId) {
        io->UpdateControllerState(state, fingerIdTouchId);
    });

    host->setOnMotionReset([this]() {
        if (io->getInputManager()) {
            io->getInputManager()->resetMotionControls();
        }
    });

    exitSubscription = brls::Application::getExitEvent()->subscribe([this]() {
        if (this->sessionStarted) {
            brls::Logger::info("App exiting - putting PS5 to sleep");
            this->host->gotoBed();
        }
    });

    // Subscribe to logs for display while waiting for first frame
    logSubscription = brls::Logger::getLogEvent()->subscribe(
        [this](brls::Logger::TimePoint time, brls::LogLevel level, std::string msg) {
            std::lock_guard<std::mutex> lock(logMutex);
            logLines.push_back(msg);
            while (logLines.size() > MAX_LOG_LINES) {
                logLines.pop_front();
            }
        });
}

StreamView::~StreamView()
{
    brls::Application::getExitEvent()->unsubscribe(exitSubscription);
    brls::Logger::getLogEvent()->unsubscribe(logSubscription);

    stopStream();
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
            dialog->addButton("OK", [dialog]() {
                dialog->close();
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

    brls::sync([this, reasonStr, reason]() {
        if (!sessionStarted) {
            brls::Logger::info("onQuit: session already stopped via menu, skipping");
            return;
        }

        stopStream();

        if (reason == CHIAKI_QUIT_REASON_STOPPED) {
            brls::Application::notify(reasonStr);
            brls::Application::popActivity();
        } else {
            auto* dialog = new brls::Dialog("Session Ended\n\n" + reasonStr);
            dialog->addButton("OK", [dialog]() {
                dialog->close();
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

    menu->setOnStatsToggle([this](bool enabled) {
        brls::Logger::info("Stats overlay toggled: {}", enabled);
        io->setShowStatsOverlay(enabled);
        menuOpen = false;
        brls::Application::blockInputs(true);
    });

    menu->setOnGyroReset([this]() {
        brls::Logger::info("Gyro reset triggered from menu");
        if (io->getInputManager()) {
            io->getInputManager()->resetMotionControls();
        }
        menuOpen = false;
        brls::Application::blockInputs(true);
    });

    menu->setOnDisconnect([this](bool sleep) {
        brls::Logger::info("Disconnect requested, sleep={}", sleep);
        disconnectWithSleep(sleep);
    });

    menu->setOnDismiss([this]() {
        brls::Logger::info("Menu dismissed");
        menuOpen = false;
        brls::Application::blockInputs(true);
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
