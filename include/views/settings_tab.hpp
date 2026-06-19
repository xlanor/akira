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
    BRLS_BIND(brls::SelectorCell, languageSelector, "settings/language");
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
    BRLS_BIND(brls::SliderCell, rumbleFreqLowSlider, "settings/rumbleFreqLow");
    BRLS_BIND(brls::SliderCell, rumbleFreqHighSlider, "settings/rumbleFreqHigh");
    BRLS_BIND(brls::SliderCell, rumbleEnvelopeAttackSlider, "settings/rumbleEnvelopeAttack");
    BRLS_BIND(brls::SliderCell, rumbleEnvelopeDecaySlider, "settings/rumbleEnvelopeDecay");
    BRLS_BIND(brls::SelectorCell, gyroSourceSelector, "settings/gyroSource");
    BRLS_BIND(brls::BooleanCell, sleepOnExitToggle, "settings/sleepOnExit");
    BRLS_BIND(brls::DetailCell, buttonMappingCell, "settings/buttonMapping");
    BRLS_BIND(brls::BooleanCell, enableDitheringToggle, "settings/enableDithering");
    BRLS_BIND(brls::SliderCell, ditheringStrengthSlider, "settings/ditheringStrength");
    BRLS_BIND(brls::BooleanCell, rcasEnabledToggle, "settings/rcasEnabled");
    BRLS_BIND(brls::SliderCell, rcasSharpnessSlider, "settings/rcasSharpness");
    BRLS_BIND(brls::BooleanCell, enableThreadAffinityToggle, "settings/enableThreadAffinity");
    BRLS_BIND(brls::BooleanCell, holepunchRetryToggle, "settings/holepunchRetry");
    BRLS_BIND(brls::BooleanCell, requestIdrOnFecFailureToggle, "settings/requestIdrOnFecFailure");
    BRLS_BIND(brls::SliderCell, packetLossMaxSlider, "settings/packetLossMax");
    BRLS_BIND(brls::BooleanCell, enableFileLoggingToggle, "settings/enableFileLogging");
    BRLS_BIND(brls::BooleanCell, debugLwipLogToggle, "settings/debugLwipLog");
    BRLS_BIND(brls::BooleanCell, debugWireguardLogToggle, "settings/debugWireguardLog");
    BRLS_BIND(brls::BooleanCell, debugRenderLogToggle, "settings/debugRenderLog");
    BRLS_BIND(brls::BooleanCell, debugChiakiLogToggle, "settings/debugChiakiLog");
    BRLS_BIND(brls::BooleanCell, debugDiscoveryLogToggle, "settings/debugDiscoveryLog");
    BRLS_BIND(brls::BooleanCell, debugFfmpegLogToggle, "settings/debugFfmpegLog");
    BRLS_BIND(brls::Label, versionLabel, "settings/version");
    BRLS_BIND(brls::Box, powerUserSection, "settings/powerUserSection");
    BRLS_BIND(brls::BooleanCell, unlockBitrateMaxToggle, "settings/unlockBitrateMax");
    BRLS_BIND(brls::BooleanCell, ipcStatsToggle, "settings/ipcStats");
    BRLS_BIND(brls::BooleanCell, lowLatencyModeToggle, "settings/lowLatencyMode");
    BRLS_BIND(brls::BooleanCell, portGuessingToggle, "settings/portGuessing");
    BRLS_BIND(brls::SliderCell, portGuessingCountSlider, "settings/portGuessingCount");
    BRLS_BIND(brls::SliderCell, portGuessingSocksSlider, "settings/portGuessingSocks");
    BRLS_BIND(brls::BooleanCell, autoReconnectToggle, "settings/autoReconnect");
    BRLS_BIND(brls::Button, runBenchmarkBtn, "settings/runBenchmark");

    SettingsManager* settings = nullptr;
    int powerUserClickCount = 0;
    std::chrono::steady_clock::time_point lastPowerUserClick;
    void initLanguageSelector();
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
    void initRumbleFreqLowSlider();
    void initRumbleFreqHighSlider();
    void initRumbleEnvelopeAttackSlider();
    void initRumbleEnvelopeDecaySlider();
    void initGyroSourceSelector();
    void initSleepOnExitToggle();
    void initButtonMappingCell();
    void initEnableDitheringToggle();
    void initDitheringStrengthSlider();
    void initRcasEnabledToggle();
    void initRcasSharpnessSlider();
    void initEnableThreadAffinityToggle();
    void initLowLatencyModeToggle();
    void initHolepunchRetryToggle();
    void initRequestIdrOnFecFailureToggle();
    void initPacketLossMaxSlider();
    void initEnableFileLoggingToggle();
    void initDebugLwipLogToggle();
    void initDebugWireguardLogToggle();
    void initDebugRenderLogToggle();
    void initDebugChiakiLogToggle();
    void initDebugDiscoveryLogToggle();
    void initDebugFfmpegLogToggle();
    void initPortGuessingToggle();
    void initPortGuessingCountSlider();
    void initPortGuessingSocksSlider();
    void initPowerUserSection();
    void updatePowerUserVisibility();
    void runGhashBenchmark();
};

#endif // AKIRA_SETTINGS_TAB_HPP
