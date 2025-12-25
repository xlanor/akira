#ifndef AKIRA_STREAM_VIEW_HPP
#define AKIRA_STREAM_VIEW_HPP

#include <borealis.hpp>
#include <chrono>

#include "core/host.hpp"
#include "core/io.hpp"
#include "core/settings_manager.hpp"

class StreamView : public brls::Box {
public:
    explicit StreamView(Host* host);
    ~StreamView() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    brls::View* getDefaultFocus() override { return this; }

    void startStream();
    void stopStream();

    static brls::View* create();

private:
    Host* host = nullptr;
    IO* io = nullptr;
    SettingsManager* settings = nullptr;
    bool streamActive = false;
    bool sessionStarted = false;

    bool menuOpen = false;
    std::chrono::steady_clock::time_point minusHoldStart;
    bool minusWasHeld = false;
    brls::Event<>::Subscription exitSubscription;

    void onConnected();
    void onQuit(ChiakiQuitEvent* event);
    void onRumble(uint8_t left, uint8_t right);
    void onLoginPinRequest(bool pinIncorrect);

    void checkMenuTrigger();
    void showDisconnectMenu();
    void disconnectWithSleep(bool sleep);
};

#endif // AKIRA_STREAM_VIEW_HPP
