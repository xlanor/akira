#ifndef AKIRA_STREAM_VIEW_HPP
#define AKIRA_STREAM_VIEW_HPP

#include <borealis.hpp>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>

#include "core/host.hpp"
#include "core/io.hpp"
#include "core/settings_manager.hpp"

class StreamView : public brls::Box, public std::enable_shared_from_this<StreamView> {
public:
    explicit StreamView(Host* host);
    ~StreamView() override;

    // Must be called after make_shared to enable weak_from_this()
    void setupCallbacks();

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    brls::View* getDefaultFocus() override { return this; }

    void startStream();
    void stopStream();
    void setSessionAlreadyStarted(bool started) { sessionPreStarted = started; }

    static brls::View* create();

private:
    Host* host = nullptr;
    IO* io = nullptr;
    SettingsManager* settings = nullptr;
    bool streamActive = false;
    bool sessionStarted = false;
    bool sessionPreStarted = false;
    bool wakeAttempted = false;
    int wakeRetryCount = 0;
    static constexpr int MAX_WAKE_RETRIES = 4;

    bool menuOpen = false;
    std::chrono::steady_clock::time_point minusHoldStart;
    bool minusWasHeld = false;
    brls::Event<>::Subscription exitSubscription;

    std::deque<std::string> logLines;
    std::mutex logMutex;
    brls::Event<brls::Logger::TimePoint, brls::LogLevel, std::string>::Subscription logSubscription;
    static constexpr size_t MAX_LOG_LINES = 30;
    void renderLogs(NVGcontext* vg, float x, float y, float width, float height);

    void onConnected();
    void onQuit(ChiakiQuitEvent* event);
    void onRumble(uint8_t left, uint8_t right);
    void onLoginPinRequest(bool pinIncorrect);

    void checkMenuTrigger();
    void showDisconnectMenu();
    void disconnectWithSleep(bool sleep);
    void retryWithWake();
};

#endif // AKIRA_STREAM_VIEW_HPP
