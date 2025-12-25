#ifndef AKIRA_SETTINGS_TAB_HPP
#define AKIRA_SETTINGS_TAB_HPP

#include <borealis.hpp>

#include "core/settings_manager.hpp"

class SettingsTab : public brls::Box {
public:
    SettingsTab();

    static brls::View* create();

private:
    BRLS_BIND(brls::SelectorCell, resolutionSelector, "settings/resolution");
    BRLS_BIND(brls::SelectorCell, fpsSelector, "settings/fps");
    BRLS_BIND(brls::SelectorCell, hapticSelector, "settings/haptic");
    BRLS_BIND(brls::InputCell, psnOnlineIdInput, "settings/psnOnlineId");
    BRLS_BIND(brls::Button, lookupBtn, "settings/lookupBtn");
    BRLS_BIND(brls::InputCell, psnAccountIdInput, "settings/psnAccountId");

    SettingsManager* settings = nullptr;

    void initResolutionSelector();
    void initFpsSelector();
    void initHapticSelector();
    void initPsnAccountSection();
};

#endif // AKIRA_SETTINGS_TAB_HPP
