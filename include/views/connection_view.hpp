#ifndef AKIRA_CONNECTION_VIEW_HPP
#define AKIRA_CONNECTION_VIEW_HPP

#include <borealis.hpp>
#include <chiaki/thread.h>
#include <deque>
#include <mutex>
#include <atomic>

#include "core/host.hpp"
#include "core/io.hpp"

class ConnectionView : public brls::Box {
public:
    explicit ConnectionView(Host* host);
    ~ConnectionView() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    brls::View* getDefaultFocus() override { return this; }

    static brls::View* create();

private:
    Host* host = nullptr;
    IO* io = nullptr;

    brls::Box* logContainer = nullptr;
    brls::ScrollingFrame* scrollFrame = nullptr;
    std::deque<std::string> logLines;
    std::mutex logMutex;
    static constexpr size_t MAX_LOG_LINES = 100;

    brls::Event<brls::Logger::TimePoint, brls::LogLevel, std::string>::Subscription logSubscription;

    ChiakiThread connectionThread;
    std::atomic<bool> threadStarted{false};
    std::atomic<bool> connectionRunning{false};
    std::atomic<bool> connectionSuccess{false};
    std::atomic<bool> connectionFinished{false};
    std::string connectionError;

    bool logsNeedUpdate = false;
    bool transitionPending = false;
    bool needsScrollToBottom = false;

    void startConnection();
    void addLogLine(const std::string& line, brls::LogLevel level);
    void updateLogDisplay();
    void onConnectionComplete();

    static void* connectionThreadFunc(void* user);
};

#endif // AKIRA_CONNECTION_VIEW_HPP
