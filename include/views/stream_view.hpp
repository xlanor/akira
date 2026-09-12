#ifndef AKIRA_STREAM_VIEW_HPP
#define AKIRA_STREAM_VIEW_HPP

#include <vector>
#include <cstdint>
#include <borealis.hpp>
#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>

#include "core/host.hpp"
#include "stream/session.hpp"
#include "core/settings_manager.hpp"

class StreamView : public brls::Box, public std::enable_shared_from_this<StreamView> {
public:
    explicit StreamView(Host* host, std::shared_ptr<Host> hostOwner = {});
    ~StreamView() override;

    // Must be called after make_shared to enable weak_from_this()
    void setupCallbacks();

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    brls::View* getDefaultFocus() override { return this; }

    bool beginPadChoice();
    void startStream();
    void stopStream();
    void setSessionAlreadyStarted(bool started) { sessionPreStarted = started; }

    static brls::View* create();

private:
    void waitForPads(int attempt);
    void abandonBeforeStart();
    void finishPadChoice();

    Host* host = nullptr;
    std::shared_ptr<Host> hostOwner;
    Session* session = nullptr;
    SettingsManager* settings = nullptr;
    bool streamActive = false;
    bool sessionStarted = false;
    bool sessionPreStarted = false;
    bool padChoiceDone = false;
    bool controllerReady = false;
    bool wakeAttempted = false;
    int wakeRetryCount = 0;
    static constexpr int MAX_WAKE_RETRIES = 4;

    bool menuOpen = false;

    std::vector<uint8_t> pausedFrameRGBA;
    int pausedFrameW = 0;
    int pausedFrameH = 0;
    int pausedFrameImage = -1;

    void capturePausedFrame();
    void releasePausedFrame(NVGcontext* vg);
    bool drawPausedFrame(NVGcontext* vg, float x, float y, float width, float height);
    bool videoPipelineActive = false;
    bool intentionalDisconnect = false;
    bool reconnecting = false;
    bool loginPinEnteredThisSession = false;
    bool couchPasscodeDialogOpen = false;
    bool couchArrivalPromptOpen = false;
    uint32_t sessionGeneration = 0;
    std::chrono::steady_clock::time_point minusHoldStart;
    bool minusWasHeld = false;
    brls::Event<>::Subscription exitSubscription;
    brls::Event<bool>::Subscription focusSubscription;

    std::deque<std::string> logLines;
    std::atomic<int> currentStage{0};
    std::mutex logMutex;
    brls::Event<brls::Logger::TimePoint, brls::LogLevel, std::string>::Subscription logSubscription;
    static constexpr size_t MAX_LOG_LINES = 30;
    void renderLogs(NVGcontext* vg, float x, float y, float width, float height);

    void onConnected();
    void onQuit(ChiakiQuitEvent* event);
    void onRumble(uint8_t player_index, uint8_t left, uint8_t right);
    void onLoginPinRequest(bool pinIncorrect);
    void pauseStreamForUi();
    void resumeStreamAfterUi();
    void onPadArrived(HidNpadIdType npad);
    void openCouchClaim();
    void openPrimaryControllerPicker();
    void onPadPasscodeRequest(uint8_t slot, bool retry);
    void onPadJoinFailed(uint8_t slot, uint8_t status);

    void checkMenuTrigger();
    void prepareVideoPipelineTick();
    void activateVideoPipeline();
    void streamingTick();
    void showDisconnectMenu();
    void disconnectWithSleep(bool sleep);
    void retryWithWake();

    void onFocusChanged(bool focused);
    void attemptReconnect();
};

#endif // AKIRA_STREAM_VIEW_HPP
