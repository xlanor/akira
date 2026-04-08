#ifndef AKIRA_STREAM_MENU_HPP
#define AKIRA_STREAM_MENU_HPP

#include <borealis.hpp>
#include <functional>

class StreamMenu : public brls::Box
{
public:
    StreamMenu();

    void setOnStatsToggle(std::function<void(bool)> callback);
    void setOnDisconnect(std::function<void(bool sleep)> callback);
    void setOnDismiss(std::function<void()> callback);
    void setOnGyroReset(std::function<void()> callback);
    void setOnButtonMapping(std::function<void()> callback);
    void setOnTouchDebugToggle(std::function<void(bool)> callback);

    void setStatsEnabled(bool enabled);
    void setTouchDebugEnabled(bool enabled);

    bool isTranslucent() override { return true; }

private:
    bool statsEnabled = false;
    bool touchDebugEnabled = false;

    std::function<void(bool)> onStatsToggle;
    std::function<void(bool)> onDisconnect;
    std::function<void()> onDismiss;
    std::function<void()> onGyroReset;
    std::function<void()> onButtonMapping;
    std::function<void(bool)> onTouchDebugToggle;

    BRLS_BIND(brls::Button, statsButton, "stream_menu/stats");
    BRLS_BIND(brls::Button, buttonMappingButton, "stream_menu/button_mapping");
    BRLS_BIND(brls::Button, touchDebugButton, "stream_menu/touch_debug");
    BRLS_BIND(brls::Button, resetGyroButton, "stream_menu/reset_gyro");
    BRLS_BIND(brls::Button, disconnectButton, "stream_menu/disconnect");
    BRLS_BIND(brls::Button, sleepButton, "stream_menu/sleep");
};

#endif // AKIRA_STREAM_MENU_HPP
