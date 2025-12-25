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

    void setStatsEnabled(bool enabled);

    bool isTranslucent() override { return true; }

private:
    bool statsEnabled = false;

    std::function<void(bool)> onStatsToggle;
    std::function<void(bool)> onDisconnect;
    std::function<void()> onDismiss;

    BRLS_BIND(brls::Button, statsButton, "stream_menu/stats");
    BRLS_BIND(brls::Button, disconnectButton, "stream_menu/disconnect");
    BRLS_BIND(brls::Button, sleepButton, "stream_menu/sleep");
};

#endif // AKIRA_STREAM_MENU_HPP
