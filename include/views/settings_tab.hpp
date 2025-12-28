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
    BRLS_BIND(brls::SliderCell, bitrateSlider, "settings/bitrate");
    BRLS_BIND(brls::SelectorCell, hapticSelector, "settings/haptic");
    BRLS_BIND(brls::BooleanCell, invertABToggle, "settings/invertAB");
    BRLS_BIND(brls::InputCell, psnOnlineIdInput, "settings/psnOnlineId");
    BRLS_BIND(brls::Button, lookupBtn, "settings/lookupBtn");
    BRLS_BIND(brls::InputCell, psnAccountIdInput, "settings/psnAccountId");
    BRLS_BIND(brls::InputCell, companionHostInput, "settings/companionHost");
    BRLS_BIND(brls::InputCell, companionPortInput, "settings/companionPort");
    BRLS_BIND(brls::Button, fetchPsnBtn, "settings/fetchPsnBtn");
    BRLS_BIND(brls::Button, refreshTokenBtn, "settings/refreshTokenBtn");
    BRLS_BIND(brls::Button, clearPsnBtn, "settings/clearPsnBtn");

    // Credential display cells
    BRLS_BIND(brls::DetailCell, credOnlineIdCell, "settings/credOnlineId");
    BRLS_BIND(brls::DetailCell, credAccountIdCell, "settings/credAccountId");
    BRLS_BIND(brls::DetailCell, credAccessTokenCell, "settings/credAccessToken");
    BRLS_BIND(brls::DetailCell, credRefreshTokenCell, "settings/credRefreshToken");
    BRLS_BIND(brls::DetailCell, credTokenExpiryCell, "settings/credTokenExpiry");
    BRLS_BIND(brls::DetailCell, credDuidCell, "settings/credDuid");

    SettingsManager* settings = nullptr;

    void updateCredentialsDisplay();

    void initResolutionSelector();
    void initFpsSelector();
    void initBitrateSlider();
    void updateBitrateSlider();
    void initHapticSelector();
    void initInvertABToggle();
    void initPsnAccountSection();
    void initCompanionSection();
};

#endif // AKIRA_SETTINGS_TAB_HPP
