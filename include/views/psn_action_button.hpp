#ifndef AKIRA_PSN_ACTION_BUTTON_HPP
#define AKIRA_PSN_ACTION_BUTTON_HPP

#include <borealis.hpp>

#include <functional>
#include <string>

#include "core/discovery_manager.hpp"

class PsnActionButton {
public:
    using StatusProvider = std::function<PsnActionStatus()>;

    void attach(brls::Button* button, NVGcolor readyColor, std::string readyLabel,
                std::string busyLabel, std::string waitLabelKey, StatusProvider provider);

    bool isReady() const;

    void start();
    void stop();
    void apply();

private:
    brls::Button* button = nullptr;
    NVGcolor readyColor{};
    std::string readyLabel;
    std::string busyLabel;
    std::string waitLabelKey;
    StatusProvider provider;

    brls::RepeatingTimer timer;
    PsnActionState appliedState = PsnActionState::Ready;
    int appliedSeconds = -1;
    bool applied = false;
};

#endif // AKIRA_PSN_ACTION_BUTTON_HPP
