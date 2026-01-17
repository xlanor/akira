#ifndef AKIRA_SETTINGS_TAB_HPP
#define AKIRA_SETTINGS_TAB_HPP

#include <borealis.hpp>
#include <chrono>

#include "core/settings_manager.hpp"

class SettingsTab : public brls::Box {
public:
    SettingsTab();

    static brls::View* create();

private:
    BRLS_BIND(brls::SelectorCell, localResolutionSelector, "settings/localResolution");
    BRLS_BIND(brls::SelectorCell, remoteResolutionSelector, "settings/remoteResolution");
    BRLS_BIND(brls::SelectorCell, localFpsSelector, "settings/localFps");
    BRLS_BIND(brls::SelectorCell, remoteFpsSelector, "settings/remoteFps");
    BRLS_BIND(brls::SliderCell, localBitrateSlider, "settings/localBitrate");
    BRLS_BIND(brls::SliderCell, remoteBitrateSlider, "settings/remoteBitrate");
    BRLS_BIND(brls::SelectorCell, vpnResolutionSelector, "settings/vpnResolution");
    BRLS_BIND(brls::SelectorCell, vpnFpsSelector, "settings/vpnFps");
    BRLS_BIND(brls::SliderCell, vpnBitrateSlider, "settings/vpnBitrate");
    BRLS_BIND(brls::SelectorCell, hapticSelector, "settings/haptic");
    BRLS_BIND(brls::BooleanCell, invertABToggle, "settings/invertAB");
    BRLS_BIND(brls::SelectorCell, gyroSourceSelector, "settings/gyroSource");
    BRLS_BIND(brls::BooleanCell, sleepOnExitToggle, "settings/sleepOnExit");
    BRLS_BIND(brls::BooleanCell, holepunchRetryToggle, "settings/holepunchRetry");
    BRLS_BIND(brls::BooleanCell, requestIdrOnFecFailureToggle, "settings/requestIdrOnFecFailure");
    BRLS_BIND(brls::SliderCell, packetLossMaxSlider, "settings/packetLossMax");
    BRLS_BIND(brls::BooleanCell, enableFileLoggingToggle, "settings/enableFileLogging");
    BRLS_BIND(brls::BooleanCell, debugLwipLogToggle, "settings/debugLwipLog");
    BRLS_BIND(brls::BooleanCell, debugWireguardLogToggle, "settings/debugWireguardLog");
    BRLS_BIND(brls::BooleanCell, debugRenderLogToggle, "settings/debugRenderLog");
    BRLS_BIND(brls::BooleanCell, debugChiakiLogToggle, "settings/debugChiakiLog");
    BRLS_BIND(brls::InputCell, psnOnlineIdInput, "settings/psnOnlineId");
    BRLS_BIND(brls::Button, lookupBtn, "settings/lookupBtn");
    BRLS_BIND(brls::InputCell, psnAccountIdInput, "settings/psnAccountId");
    BRLS_BIND(brls::InputCell, companionHostInput, "settings/companionHost");
    BRLS_BIND(brls::InputCell, companionPortInput, "settings/companionPort");
    BRLS_BIND(brls::Button, fetchPsnBtn, "settings/fetchPsnBtn");
    BRLS_BIND(brls::Button, refreshTokenBtn, "settings/refreshTokenBtn");
    BRLS_BIND(brls::Button, clearPsnBtn, "settings/clearPsnBtn");

    BRLS_BIND(brls::DetailCell, credOnlineIdCell, "settings/credOnlineId");
    BRLS_BIND(brls::DetailCell, credAccountIdCell, "settings/credAccountId");
    BRLS_BIND(brls::DetailCell, credAccessTokenCell, "settings/credAccessToken");
    BRLS_BIND(brls::DetailCell, credRefreshTokenCell, "settings/credRefreshToken");
    BRLS_BIND(brls::DetailCell, credTokenExpiryCell, "settings/credTokenExpiry");
    BRLS_BIND(brls::DetailCell, credDuidCell, "settings/credDuid");
    BRLS_BIND(brls::Button, revealCredentialsBtn, "settings/revealCredentials");

    BRLS_BIND(brls::Label, versionLabel, "settings/version");
    BRLS_BIND(brls::Box, powerUserSection, "settings/powerUserSection");
    BRLS_BIND(brls::BooleanCell, unlockBitrateMaxToggle, "settings/unlockBitrateMax");
    BRLS_BIND(brls::BooleanCell, experimentalCryptoToggle, "settings/experimentalCrypto");
    BRLS_BIND(brls::Button, runBenchmarkBtn, "settings/runBenchmark");

    SettingsManager* settings = nullptr;
    int powerUserClickCount = 0;
    std::chrono::steady_clock::time_point lastPowerUserClick;
    bool credentialsRevealed = false;

    void updateCredentialsDisplay();
    std::string censorString(const std::string& str);

    void initLocalResolutionSelector();
    void initRemoteResolutionSelector();
    void initLocalFpsSelector();
    void initRemoteFpsSelector();
    void initLocalBitrateSlider();
    void updateLocalBitrateSlider();
    void initRemoteBitrateSlider();
    void updateRemoteBitrateSlider();
    void initVpnResolutionSelector();
    void initVpnFpsSelector();
    void initVpnBitrateSlider();
    void updateVpnBitrateSlider();
    void initHapticSelector();
    void initInvertABToggle();
    void initGyroSourceSelector();
    void initSleepOnExitToggle();
    void initHolepunchRetryToggle();
    void initRequestIdrOnFecFailureToggle();
    void initPacketLossMaxSlider();
    void initEnableFileLoggingToggle();
    void initDebugLwipLogToggle();
    void initDebugWireguardLogToggle();
    void initDebugRenderLogToggle();
    void initDebugChiakiLogToggle();
    void initPsnAccountSection();
    void initCompanionSection();
    void initPowerUserSection();
    void updatePowerUserVisibility();
    void initExperimentalCryptoToggle();
    void runGhashBenchmark();
};

#endif // AKIRA_SETTINGS_TAB_HPP
