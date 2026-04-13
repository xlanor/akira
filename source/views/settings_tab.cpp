#include "views/settings_tab.hpp"
#include "views/benchmark_view.hpp"
#include "views/controller_remap_view.hpp"
#include "core/discovery_manager.hpp"
#include <borealis/core/i18n.hpp>
#include <format>

#include <ctime>
#include <iomanip>
#include <sstream>

using namespace brls::literals;

// Custom button styles with colored backgrounds, no border
static const brls::ButtonStyle BUTTONSTYLE_BLUE = {
    .shadowType              = brls::ShadowType::GENERIC,
    .hideHighlightBackground = true,
    .highlightPadding = "",
    .borderThickness  = "",
    .enabledBackgroundColor = "",
    .enabledLabelColor      = "brls/button/primary_enabled_text",
    .enabledBorderColor     = "",
    .disabledBackgroundColor = "",
    .disabledLabelColor      = "brls/button/primary_disabled_text",
    .disabledBorderColor     = "",
};

static const brls::ButtonStyle BUTTONSTYLE_GREEN = {
    .shadowType              = brls::ShadowType::GENERIC,
    .hideHighlightBackground = true,
    .highlightPadding = "",
    .borderThickness  = "",
    .enabledBackgroundColor = "",
    .enabledLabelColor      = "brls/button/primary_enabled_text",
    .enabledBorderColor     = "",
    .disabledBackgroundColor = "",
    .disabledLabelColor      = "brls/button/primary_disabled_text",
    .disabledBorderColor     = "",
};

SettingsTab::SettingsTab() {
    this->inflateFromXMLRes("xml/tabs/settings.xml");

    settings = SettingsManager::getInstance();

    initLanguageSelector();
    initLocalResolutionSelector();
    initRemoteResolutionSelector();
    initLocalFpsSelector();
    initRemoteFpsSelector();
    initLocalBitrateSlider();
    initRemoteBitrateSlider();
    initVpnResolutionSelector();
    initVpnFpsSelector();
    initVpnBitrateSlider();
    initHapticSelector();
    initRumbleFreqLowSlider();
    initRumbleFreqHighSlider();
    initRumbleEnvelopeAttackSlider();
    initRumbleEnvelopeDecaySlider();
    initGyroSourceSelector();
    initSleepOnExitToggle();
    initButtonMappingCell();
    initEnableDitheringToggle();
    initDitheringStrengthSlider();
    initRcasEnabledToggle();
    initRcasSharpnessSlider();
    initEnableThreadAffinityToggle();
    initLowLatencyModeToggle();
    initHolepunchRetryToggle();
    initPsnAccountSection();
    initCompanionSection();
    initPowerUserSection();
    initPortGuessingToggle();
    initPortGuessingCountSlider();
    initPortGuessingSocksSlider();
    initRequestIdrOnFecFailureToggle();
    initPacketLossMaxSlider();
    initEnableFileLoggingToggle();
    initDebugLwipLogToggle();
    initDebugWireguardLogToggle();
    initDebugRenderLogToggle();
    initDebugChiakiLogToggle();
    initDebugDiscoveryLogToggle();
    initDebugFfmpegLogToggle();

    runBenchmarkBtn->registerClickAction([this](brls::View*) {
        runGhashBenchmark();
        return true;
    });

    revealCredentialsBtn->registerClickAction([this](brls::View*) {
        credentialsRevealed = !credentialsRevealed;
        revealCredentialsBtn->setText(credentialsRevealed ? "akira/settings/hide_secrets"_i18n : "akira/settings/reveal_secrets"_i18n);
        updateCredentialsDisplay();
        return true;
    });

    updateCredentialsDisplay();
}

brls::View* SettingsTab::create() {
    return new SettingsTab();
}

void SettingsTab::initLanguageSelector() {
    static const std::vector<std::string> localeCodes = {"", "en-US", "zh-Hans"};

    std::vector<std::string> options = {
        "akira/settings/lang_system"_i18n,
        "akira/settings/lang_en"_i18n,
        "akira/settings/lang_zh_hans"_i18n,
    };

    std::string currentLocale = settings->getDebugLocale();
    int currentIndex = 0;
    for (size_t i = 1; i < localeCodes.size(); i++) {
        if (localeCodes[i] == currentLocale) {
            currentIndex = static_cast<int>(i);
            break;
        }
    }

    languageSelector->init(
        "akira/settings/language"_i18n,
        options,
        currentIndex,
        [](int selected) {},
        [this](int selected) {
            std::string locale = (selected > 0 && selected < (int)localeCodes.size()) ? localeCodes[selected] : "";
            settings->setDebugLocale(locale);
            settings->writeFile();
        }
    );
}

void SettingsTab::initLocalResolutionSelector() {
    std::vector<std::string> options = {
        "akira/settings/res_360p"_i18n,
        "akira/settings/res_540p"_i18n,
        "akira/settings/res_720p"_i18n,
        "akira/settings/res_1080p"_i18n,
        "720p (FSR)",
        "1080p (FSR)",
    };

    int currentIndex = 2;
    auto current = settings->getLocalVideoResolution();
    bool fsrOn = settings->getLocalFsrEnabled();
    if (fsrOn && current == CHIAKI_VIDEO_RESOLUTION_PRESET_540p) {
        currentIndex = 4;
    } else if (fsrOn && current == CHIAKI_VIDEO_RESOLUTION_PRESET_720p) {
        currentIndex = 5;
    } else {
        switch (current) {
            case CHIAKI_VIDEO_RESOLUTION_PRESET_360p: currentIndex = 0; break;
            case CHIAKI_VIDEO_RESOLUTION_PRESET_540p: currentIndex = 1; break;
            case CHIAKI_VIDEO_RESOLUTION_PRESET_720p: currentIndex = 2; break;
            case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p: currentIndex = 3; break;
        }
    }

    localResolutionSelector->init(
        "akira/settings/local_resolution"_i18n,
        options,
        currentIndex,
        [](int selected) {},
        [this](int selected) {
            ChiakiVideoResolutionPreset preset;
            bool fsr = false;
            switch (selected) {
                case 0: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_360p; break;
                case 1: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_540p; break;
                case 2: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_720p; break;
                case 3: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_1080p; break;
                case 4: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_540p; fsr = true; break;
                case 5: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_720p; fsr = true; break;
                default: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_720p; break;
            }
            settings->setLocalVideoResolution(preset);
            settings->setLocalFsrEnabled(fsr);
            updateLocalBitrateSlider();
            settings->writeFile();
            brls::Logger::info("Local resolution set to {}{}", SettingsManager::resolutionToString(preset), fsr ? " (FSR)" : "");
        }
    );
}

void SettingsTab::initRemoteResolutionSelector() {
    std::vector<std::string> options = {
        "akira/settings/res_360p"_i18n,
        "akira/settings/res_540p"_i18n,
        "akira/settings/res_720p"_i18n,
        "akira/settings/res_1080p"_i18n,
        "720p (FSR)",
        "1080p (FSR)",
    };

    int currentIndex = 2;
    auto current = settings->getRemoteVideoResolution();
    bool fsrOn = settings->getRemoteFsrEnabled();
    if (fsrOn && current == CHIAKI_VIDEO_RESOLUTION_PRESET_540p) {
        currentIndex = 4;
    } else if (fsrOn && current == CHIAKI_VIDEO_RESOLUTION_PRESET_720p) {
        currentIndex = 5;
    } else {
        switch (current) {
            case CHIAKI_VIDEO_RESOLUTION_PRESET_360p: currentIndex = 0; break;
            case CHIAKI_VIDEO_RESOLUTION_PRESET_540p: currentIndex = 1; break;
            case CHIAKI_VIDEO_RESOLUTION_PRESET_720p: currentIndex = 2; break;
            case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p: currentIndex = 3; break;
        }
    }

    remoteResolutionSelector->init(
        "akira/settings/remote_resolution"_i18n,
        options,
        currentIndex,
        [](int selected) {},
        [this](int selected) {
            ChiakiVideoResolutionPreset preset;
            bool fsr = false;
            switch (selected) {
                case 0: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_360p; break;
                case 1: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_540p; break;
                case 2: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_720p; break;
                case 3: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_1080p; break;
                case 4: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_540p; fsr = true; break;
                case 5: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_720p; fsr = true; break;
                default: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_720p; break;
            }
            settings->setRemoteVideoResolution(preset);
            settings->setRemoteFsrEnabled(fsr);
            updateRemoteBitrateSlider();
            settings->writeFile();
            brls::Logger::info("Remote resolution set to {}{}", SettingsManager::resolutionToString(preset), fsr ? " (FSR)" : "");
        }
    );
}

void SettingsTab::initLocalFpsSelector() {
    std::vector<std::string> options = {"akira/settings/fps_30"_i18n, "akira/settings/fps_60"_i18n};

    int currentIndex = 1;
    if (settings->getLocalVideoFPS() == CHIAKI_VIDEO_FPS_PRESET_30) {
        currentIndex = 0;
    }

    localFpsSelector->init(
        "akira/settings/local_frame_rate"_i18n,
        options,
        currentIndex,
        [](int selected) {},
        [this](int selected) {
            auto preset = selected == 0 ? CHIAKI_VIDEO_FPS_PRESET_30 : CHIAKI_VIDEO_FPS_PRESET_60;
            settings->setLocalVideoFPS(preset);
            settings->writeFile();
            brls::Logger::info("Local FPS set to {}", SettingsManager::fpsToString(preset));
        }
    );
}

void SettingsTab::initRemoteFpsSelector() {
    std::vector<std::string> options = {"akira/settings/fps_30"_i18n, "akira/settings/fps_60"_i18n};

    int currentIndex = 1;
    if (settings->getRemoteVideoFPS() == CHIAKI_VIDEO_FPS_PRESET_30) {
        currentIndex = 0;
    }

    remoteFpsSelector->init(
        "akira/settings/remote_frame_rate"_i18n,
        options,
        currentIndex,
        [](int selected) {},
        [this](int selected) {
            auto preset = selected == 0 ? CHIAKI_VIDEO_FPS_PRESET_30 : CHIAKI_VIDEO_FPS_PRESET_60;
            settings->setRemoteVideoFPS(preset);
            settings->writeFile();
            brls::Logger::info("Remote FPS set to {}", SettingsManager::fpsToString(preset));
        }
    );
}

void SettingsTab::initLocalBitrateSlider() {
    auto resolution = settings->getLocalVideoResolution();
    int maxBitrate = settings->getMaxBitrateForResolution(resolution);
    int minBitrate = settings->getMinBitrateForResolution(resolution);
    int currentBitrate = settings->getLocalVideoBitrate();

    bool needsSave = false;
    if (currentBitrate < minBitrate) { currentBitrate = minBitrate; needsSave = true; }
    if (currentBitrate > maxBitrate) { currentBitrate = maxBitrate; needsSave = true; }
    if (needsSave) {
        settings->setLocalVideoBitrate(currentBitrate);
        settings->writeFile();
    }

    float normalizedValue = static_cast<float>(currentBitrate - minBitrate) / (maxBitrate - minBitrate);
    normalizedValue = std::max(0.0f, std::min(1.0f, normalizedValue));

    localBitrateSlider->detail->setWidth(100);
    localBitrateSlider->detail->setShrink(0);
    localBitrateSlider->init(
        "akira/settings/local_bitrate"_i18n,
        normalizedValue,
        [this](float value) {
            auto resolution = settings->getLocalVideoResolution();
            int maxBitrate = settings->getMaxBitrateForResolution(resolution);
            int minBitrate = settings->getMinBitrateForResolution(resolution);
            int bitrate = minBitrate + static_cast<int>(value * (maxBitrate - minBitrate));
            settings->setLocalVideoBitrate(bitrate);
            localBitrateSlider->detail->setText(brls::getStr("akira/settings/kbps", bitrate));
            settings->writeFile();
            brls::Logger::info("Local bitrate set to {}", bitrate);
        }
    );

    localBitrateSlider->detail->setText(brls::getStr("akira/settings/kbps", currentBitrate));
}

void SettingsTab::updateLocalBitrateSlider() {
    auto resolution = settings->getLocalVideoResolution();
    int defaultBitrate = SettingsManager::getDefaultBitrateForResolution(resolution);
    int maxBitrate = settings->getMaxBitrateForResolution(resolution);
    int minBitrate = settings->getMinBitrateForResolution(resolution);

    settings->setLocalVideoBitrate(defaultBitrate);

    float normalizedValue = static_cast<float>(defaultBitrate - minBitrate) / (maxBitrate - minBitrate);
    localBitrateSlider->slider->setProgress(normalizedValue);
    localBitrateSlider->detail->setText(brls::getStr("akira/settings/kbps", defaultBitrate));
}

void SettingsTab::initRemoteBitrateSlider() {
    auto resolution = settings->getRemoteVideoResolution();
    int maxBitrate = settings->getMaxBitrateForResolution(resolution);
    int minBitrate = settings->getMinBitrateForResolution(resolution);
    int currentBitrate = settings->getRemoteVideoBitrate();

    bool needsSave = false;
    if (currentBitrate < minBitrate) { currentBitrate = minBitrate; needsSave = true; }
    if (currentBitrate > maxBitrate) { currentBitrate = maxBitrate; needsSave = true; }
    if (needsSave) {
        settings->setRemoteVideoBitrate(currentBitrate);
        settings->writeFile();
    }

    float normalizedValue = static_cast<float>(currentBitrate - minBitrate) / (maxBitrate - minBitrate);
    normalizedValue = std::max(0.0f, std::min(1.0f, normalizedValue));

    remoteBitrateSlider->detail->setWidth(100);
    remoteBitrateSlider->detail->setShrink(0);
    remoteBitrateSlider->init(
        "akira/settings/remote_bitrate"_i18n,
        normalizedValue,
        [this](float value) {
            auto resolution = settings->getRemoteVideoResolution();
            int maxBitrate = settings->getMaxBitrateForResolution(resolution);
            int minBitrate = settings->getMinBitrateForResolution(resolution);
            int bitrate = minBitrate + static_cast<int>(value * (maxBitrate - minBitrate));
            settings->setRemoteVideoBitrate(bitrate);
            remoteBitrateSlider->detail->setText(brls::getStr("akira/settings/kbps", bitrate));
            settings->writeFile();
            brls::Logger::info("Remote bitrate set to {}", bitrate);
        }
    );

    remoteBitrateSlider->detail->setText(brls::getStr("akira/settings/kbps", currentBitrate));
}

void SettingsTab::updateRemoteBitrateSlider() {
    auto resolution = settings->getRemoteVideoResolution();
    int defaultBitrate = SettingsManager::getDefaultBitrateForResolution(resolution);
    int maxBitrate = settings->getMaxBitrateForResolution(resolution);
    int minBitrate = settings->getMinBitrateForResolution(resolution);

    settings->setRemoteVideoBitrate(defaultBitrate);

    float normalizedValue = static_cast<float>(defaultBitrate - minBitrate) / (maxBitrate - minBitrate);
    remoteBitrateSlider->slider->setProgress(normalizedValue);
    remoteBitrateSlider->detail->setText(brls::getStr("akira/settings/kbps", defaultBitrate));
}

void SettingsTab::initVpnResolutionSelector() {
    std::vector<std::string> options = {
        "akira/settings/res_360p"_i18n,
        "akira/settings/res_540p"_i18n,
        "akira/settings/res_720p"_i18n,
        "720p (FSR)",
        "1080p (FSR)",
    };

    int currentIndex = 2;
    auto current = settings->getVpnVideoResolution();
    bool fsrOn = settings->getVpnFsrEnabled();
    if (fsrOn && current == CHIAKI_VIDEO_RESOLUTION_PRESET_540p) {
        currentIndex = 3;
    } else if (fsrOn && current == CHIAKI_VIDEO_RESOLUTION_PRESET_720p) {
        currentIndex = 4;
    } else {
        switch (current) {
            case CHIAKI_VIDEO_RESOLUTION_PRESET_360p: currentIndex = 0; break;
            case CHIAKI_VIDEO_RESOLUTION_PRESET_540p: currentIndex = 1; break;
            case CHIAKI_VIDEO_RESOLUTION_PRESET_720p: currentIndex = 2; break;
            default: currentIndex = 2; break;
        }
    }

    vpnResolutionSelector->init(
        "akira/settings/vpn_resolution"_i18n,
        options,
        currentIndex,
        [](int selected) {},
        [this](int selected) {
            ChiakiVideoResolutionPreset preset;
            bool fsr = false;
            switch (selected) {
                case 0: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_360p; break;
                case 1: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_540p; break;
                case 2: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_720p; break;
                case 3: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_540p; fsr = true; break;
                case 4: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_720p; fsr = true; break;
                default: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_720p; break;
            }
            settings->setVpnVideoResolution(preset);
            settings->setVpnFsrEnabled(fsr);
            settings->writeFile();
            brls::Logger::info("VPN resolution set to {}{}", SettingsManager::resolutionToString(preset), fsr ? " (FSR)" : "");
        }
    );
}

void SettingsTab::initVpnFpsSelector() {
    std::vector<std::string> options = {"akira/settings/fps_30"_i18n, "akira/settings/fps_60"_i18n};

    int currentIndex = 0;
    if (settings->getVpnVideoFPS() == CHIAKI_VIDEO_FPS_PRESET_60) {
        currentIndex = 1;
    }

    vpnFpsSelector->init(
        "akira/settings/vpn_frame_rate"_i18n,
        options,
        currentIndex,
        [](int selected) {},
        [this](int selected) {
            auto preset = selected == 0 ? CHIAKI_VIDEO_FPS_PRESET_30 : CHIAKI_VIDEO_FPS_PRESET_60;
            settings->setVpnVideoFPS(preset);
            settings->writeFile();
            brls::Logger::info("VPN FPS set to {}", SettingsManager::fpsToString(preset));
        }
    );
}

void SettingsTab::initVpnBitrateSlider() {
    const int VPN_MIN_BITRATE = 2000;
    const int VPN_MAX_BITRATE = 15000;

    int currentBitrate = settings->getVpnVideoBitrate();

    if (currentBitrate < VPN_MIN_BITRATE) currentBitrate = VPN_MIN_BITRATE;
    if (currentBitrate > VPN_MAX_BITRATE) currentBitrate = VPN_MAX_BITRATE;
    settings->setVpnVideoBitrate(currentBitrate);

    float normalizedValue = static_cast<float>(currentBitrate - VPN_MIN_BITRATE) / (VPN_MAX_BITRATE - VPN_MIN_BITRATE);
    normalizedValue = std::max(0.0f, std::min(1.0f, normalizedValue));

    vpnBitrateSlider->detail->setWidth(100);
    vpnBitrateSlider->detail->setShrink(0);
    vpnBitrateSlider->init(
        "akira/settings/vpn_bitrate"_i18n,
        normalizedValue,
        [this](float value) {
            const int VPN_MIN_BITRATE = 2000;
            const int VPN_MAX_BITRATE = 15000;
            int bitrate = VPN_MIN_BITRATE + static_cast<int>(value * (VPN_MAX_BITRATE - VPN_MIN_BITRATE));
            settings->setVpnVideoBitrate(bitrate);
            vpnBitrateSlider->detail->setText(brls::getStr("akira/settings/kbps", bitrate));
            settings->writeFile();
            brls::Logger::info("VPN bitrate set to {}", bitrate);
        }
    );

    vpnBitrateSlider->detail->setText(brls::getStr("akira/settings/kbps", currentBitrate));
}

void SettingsTab::updateVpnBitrateSlider() {
    const int VPN_MIN_BITRATE = 2000;
    const int VPN_MAX_BITRATE = 15000;
    const int VPN_DEFAULT_BITRATE = 5000;

    settings->setVpnVideoBitrate(VPN_DEFAULT_BITRATE);

    float normalizedValue = static_cast<float>(VPN_DEFAULT_BITRATE - VPN_MIN_BITRATE) / (VPN_MAX_BITRATE - VPN_MIN_BITRATE);
    vpnBitrateSlider->slider->setProgress(normalizedValue);
    vpnBitrateSlider->detail->setText(brls::getStr("akira/settings/kbps", VPN_DEFAULT_BITRATE));
}

void SettingsTab::initHapticSelector() {
    std::vector<std::string> options = {"akira/settings/haptic_disabled"_i18n, "akira/settings/haptic_weak"_i18n, "akira/settings/haptic_strong"_i18n};

    int currentIndex = static_cast<int>(settings->getHaptic(nullptr));

    hapticSelector->init(
        "akira/settings/haptic_feedback"_i18n,
        options,
        currentIndex,
        [this](int selected) {
            float freqLow = settings->getRumbleFreqLow();
            float freqHigh = settings->getRumbleFreqHigh();
            float strength = 0.0f;
            if (selected == 1) strength = 0.5f;
            else if (selected == 2) strength = 1.0f;

            auto* inputMgr = brls::Application::getPlatform()->getInputManager();
            if (strength > 0.0f)
            {
                inputMgr->sendRumbleRaw(0, freqLow * strength, freqHigh * strength, strength, strength);
                brls::delay(300, [inputMgr]() {
                    inputMgr->sendRumbleRaw(0, 0.0f, 0.0f, 0.0f, 0.0f);
                });
            }
            else
            {
                inputMgr->sendRumbleRaw(0, 0.0f, 0.0f, 0.0f, 0.0f);
            }
        },
        [this](int selected) {
            settings->setHaptic(nullptr, static_cast<HapticPreset>(selected));
            settings->writeFile();
            brls::Logger::info("Haptic set to {}", selected);
        }
    );
}

void SettingsTab::initRumbleFreqLowSlider() {
    constexpr float MIN_FREQ = 40.0f;
    constexpr float MAX_FREQ = 320.0f;

    float currentFreq = settings->getRumbleFreqLow();
    float normalized = (currentFreq - MIN_FREQ) / (MAX_FREQ - MIN_FREQ);
    normalized = std::max(0.0f, std::min(1.0f, normalized));

    rumbleFreqLowSlider->detail->setWidth(100);
    rumbleFreqLowSlider->detail->setShrink(0);
    rumbleFreqLowSlider->init(
        "akira/settings/rumble_low_freq"_i18n,
        normalized,
        [this](float value) {
            constexpr float MIN_FREQ = 40.0f;
            constexpr float MAX_FREQ = 320.0f;
            float freq = MIN_FREQ + value * (MAX_FREQ - MIN_FREQ);
            freq = static_cast<float>(static_cast<int>(freq));
            settings->setRumbleFreqLow(freq);
            rumbleFreqLowSlider->detail->setText(brls::getStr("akira/settings/hz_format", static_cast<int>(freq)));
            settings->writeFile();

            auto* inputMgr = brls::Application::getPlatform()->getInputManager();
            inputMgr->sendRumbleRaw(0, freq * 0.8f, settings->getRumbleFreqHigh() * 0.8f, 0.8f, 0.8f);
            brls::delay(300, [inputMgr]() {
                inputMgr->sendRumbleRaw(0, 0.0f, 0.0f, 0.0f, 0.0f);
            });
        }
    );

    rumbleFreqLowSlider->detail->setText(brls::getStr("akira/settings/hz_format", static_cast<int>(currentFreq)));
}

void SettingsTab::initRumbleFreqHighSlider() {
    constexpr float MIN_FREQ = 40.0f;
    constexpr float MAX_FREQ = 320.0f;

    float currentFreq = settings->getRumbleFreqHigh();
    float normalized = (currentFreq - MIN_FREQ) / (MAX_FREQ - MIN_FREQ);
    normalized = std::max(0.0f, std::min(1.0f, normalized));

    rumbleFreqHighSlider->detail->setWidth(100);
    rumbleFreqHighSlider->detail->setShrink(0);
    rumbleFreqHighSlider->init(
        "akira/settings/rumble_high_freq"_i18n,
        normalized,
        [this](float value) {
            constexpr float MIN_FREQ = 40.0f;
            constexpr float MAX_FREQ = 320.0f;
            float freq = MIN_FREQ + value * (MAX_FREQ - MIN_FREQ);
            freq = static_cast<float>(static_cast<int>(freq));
            settings->setRumbleFreqHigh(freq);
            rumbleFreqHighSlider->detail->setText(brls::getStr("akira/settings/hz_format", static_cast<int>(freq)));
            settings->writeFile();

            auto* inputMgr = brls::Application::getPlatform()->getInputManager();
            inputMgr->sendRumbleRaw(0, settings->getRumbleFreqLow() * 0.8f, freq * 0.8f, 0.8f, 0.8f);
            brls::delay(300, [inputMgr]() {
                inputMgr->sendRumbleRaw(0, 0.0f, 0.0f, 0.0f, 0.0f);
            });
        }
    );

    rumbleFreqHighSlider->detail->setText(brls::getStr("akira/settings/hz_format", static_cast<int>(currentFreq)));
}

void SettingsTab::initRumbleEnvelopeAttackSlider() {
    constexpr float MIN_ATTACK = 0.20f;
    constexpr float MAX_ATTACK = 1.00f;

    float currentAttack = settings->getRumbleEnvelopeAttack();
    float normalized = (currentAttack - MIN_ATTACK) / (MAX_ATTACK - MIN_ATTACK);
    normalized = std::max(0.0f, std::min(1.0f, normalized));

    rumbleEnvelopeAttackSlider->detail->setWidth(100);
    rumbleEnvelopeAttackSlider->detail->setShrink(0);
    rumbleEnvelopeAttackSlider->init(
        "akira/settings/rumble_attack"_i18n,
        normalized,
        [this](float value) {
            constexpr float MIN_ATTACK = 0.20f;
            constexpr float MAX_ATTACK = 1.00f;
            float attack = MIN_ATTACK + value * (MAX_ATTACK - MIN_ATTACK);
            attack = static_cast<float>(static_cast<int>(attack * 100.0f)) / 100.0f;
            settings->setRumbleEnvelopeAttack(attack);
            rumbleEnvelopeAttackSlider->detail->setText(brls::getStr("akira/settings/percent_format", static_cast<int>(attack * 100.0f)));
            settings->writeFile();
        }
    );

    rumbleEnvelopeAttackSlider->detail->setText(brls::getStr("akira/settings/percent_format", static_cast<int>(currentAttack * 100.0f)));
}

void SettingsTab::initRumbleEnvelopeDecaySlider() {
    constexpr float MIN_DECAY = 0.50f;
    constexpr float MAX_DECAY = 0.95f;

    float currentDecay = settings->getRumbleEnvelopeDecay();
    float normalized = (currentDecay - MIN_DECAY) / (MAX_DECAY - MIN_DECAY);
    normalized = std::max(0.0f, std::min(1.0f, normalized));

    rumbleEnvelopeDecaySlider->detail->setWidth(100);
    rumbleEnvelopeDecaySlider->detail->setShrink(0);
    rumbleEnvelopeDecaySlider->init(
        "akira/settings/rumble_sustain"_i18n,
        normalized,
        [this](float value) {
            constexpr float MIN_DECAY = 0.50f;
            constexpr float MAX_DECAY = 0.95f;
            float decay = MIN_DECAY + value * (MAX_DECAY - MIN_DECAY);
            decay = static_cast<float>(static_cast<int>(decay * 100.0f)) / 100.0f;
            settings->setRumbleEnvelopeDecay(decay);
            rumbleEnvelopeDecaySlider->detail->setText(brls::getStr("akira/settings/percent_format", static_cast<int>(decay * 100.0f)));
            settings->writeFile();
        }
    );

    rumbleEnvelopeDecaySlider->detail->setText(brls::getStr("akira/settings/percent_format", static_cast<int>(currentDecay * 100.0f)));
}

void SettingsTab::initGyroSourceSelector() {
    std::vector<std::string> options = {"akira/settings/gyro_auto"_i18n, "akira/settings/gyro_left"_i18n, "akira/settings/gyro_right"_i18n};
    int currentIndex = static_cast<int>(settings->getGyroSource());

    gyroSourceSelector->init(
        "akira/settings/gyro_source"_i18n,
        options,
        currentIndex,
        [](int selected) {},
        [this](int selected) {
            settings->setGyroSource(static_cast<GyroSource>(selected));
            settings->writeFile();
        }
    );
}

void SettingsTab::initSleepOnExitToggle() {
    bool currentValue = settings->getSleepOnExit();

    sleepOnExitToggle->init(
        "akira/settings/sleep_on_exit"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setSleepOnExit(isOn);
            settings->writeFile();
        }
    );
}

void SettingsTab::initButtonMappingCell() {
    buttonMappingCell->setText("akira/settings/button_mapping"_i18n);
    buttonMappingCell->setDetailText("akira/common/configure"_i18n);

    buttonMappingCell->registerClickAction([](brls::View*) {
        auto* remapView = new ControllerRemapView();
        brls::Application::pushActivity(new brls::Activity(remapView), brls::TransitionAnimation::NONE);
        return true;
    });
}

void SettingsTab::initEnableDitheringToggle() {
    bool currentValue = settings->getEnableDithering();

    enableDitheringToggle->init(
        "akira/settings/enable_dithering"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setEnableDithering(isOn);
            settings->writeFile();
        }
    );
}

void SettingsTab::initDitheringStrengthSlider() {
    constexpr float MIN_STRENGTH = 1.0f;
    constexpr float MAX_STRENGTH = 10.0f;

    float current = settings->getDitheringStrength();
    float normalized = (current - MIN_STRENGTH) / (MAX_STRENGTH - MIN_STRENGTH);
    normalized = std::max(0.0f, std::min(1.0f, normalized));

    ditheringStrengthSlider->detail->setWidth(100);
    ditheringStrengthSlider->detail->setShrink(0);
    ditheringStrengthSlider->slider->setStep(1.0f / (MAX_STRENGTH - MIN_STRENGTH));
    ditheringStrengthSlider->init(
        "akira/settings/dithering_strength"_i18n,
        normalized,
        [this](float value) {
            constexpr float MIN_STRENGTH = 1.0f;
            constexpr float MAX_STRENGTH = 10.0f;
            float strength = MIN_STRENGTH + value * (MAX_STRENGTH - MIN_STRENGTH);
            strength = static_cast<float>(static_cast<int>(strength));
            settings->setDitheringStrength(strength);
            ditheringStrengthSlider->detail->setText(std::format("{}", static_cast<int>(strength)));
            settings->writeFile();
        }
    );

    ditheringStrengthSlider->detail->setText(std::format("{}", static_cast<int>(current)));
}

void SettingsTab::initRcasEnabledToggle() {
    bool currentValue = settings->getRcasEnabled();

    rcasEnabledToggle->init(
        "RCAS (Sharpening)",
        currentValue,
        [this](bool isOn) {
            settings->setRcasEnabled(isOn);
            settings->writeFile();
        }
    );
}

void SettingsTab::initRcasSharpnessSlider() {
    float current = settings->getRcasSharpness();
    float normalized = 1.0f - (current / 2.0f);

    rcasSharpnessSlider->init(
        "RCAS Strength",
        normalized,
        [this](float value) {
            float sharpness = (1.0f - value) * 2.0f;
            settings->setRcasSharpness(sharpness);
            int percent = static_cast<int>(value * 100.0f);
            rcasSharpnessSlider->detail->setText(std::format("{}%", percent));
            settings->writeFile();
        }
    );

    int percent = static_cast<int>(normalized * 100.0f);
    rcasSharpnessSlider->detail->setText(std::format("{}%", percent));
}

void SettingsTab::initEnableThreadAffinityToggle() {
    bool currentValue = settings->getEnableThreadAffinity();

    enableThreadAffinityToggle->init(
        "akira/settings/thread_affinity"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setEnableThreadAffinity(isOn);
            settings->writeFile();
            brls::Logger::info("Thread affinity set to {} (requires restart)", isOn ? "true" : "false");
        }
    );
}

void SettingsTab::initLowLatencyModeToggle() {
    bool currentValue = settings->getLowLatencyMode();

    lowLatencyModeToggle->init(
        "akira/settings/low_latency_mode"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setLowLatencyMode(isOn);
            settings->writeFile();
            brls::Logger::info("Low latency mode set to {}", isOn ? "true" : "false");
        }
    );
}

void SettingsTab::initHolepunchRetryToggle() {
    bool currentValue = settings->getHolepunchRetry();

    holepunchRetryToggle->init(
        "akira/settings/holepunch_retry"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setHolepunchRetry(isOn);
            settings->writeFile();
            brls::Logger::info("Holepunch retry set to {}", isOn ? "true" : "false");
        }
    );
}

void SettingsTab::initPsnAccountSection() {
    std::string currentOnlineId = settings->getPsnOnlineId(nullptr);
    psnOnlineIdInput->init(
        "akira/settings/psn_online_id"_i18n,
        currentOnlineId,
        [this](std::string text) {
            settings->setPsnOnlineId(nullptr, text);
            settings->writeFile();
            brls::Logger::info("PSN Online ID set to {}", text);
        },
        "akira/settings/psn_online_id_placeholder"_i18n,
        "akira/settings/psn_online_id_hint"_i18n
    );

    std::string currentAccountId = settings->getPsnAccountId(nullptr);
    psnAccountIdInput->init(
        "akira/settings/psn_account_id"_i18n,
        currentAccountId,
        [this](std::string text) {
            settings->setPsnAccountId(nullptr, text);
            settings->writeFile();
            brls::Logger::info("PSN Account ID set");
        },
        "akira/settings/psn_account_id_placeholder"_i18n,
        "akira/settings/psn_account_id_hint"_i18n
    );

    lookupBtn->setStyle(&BUTTONSTYLE_BLUE);
    lookupBtn->setBackgroundColor(nvgRGBA(92, 157, 255, 255));

    lookupBtn->registerClickAction([this](brls::View* view) {
        std::string onlineId = psnOnlineIdInput->getValue();
        if (onlineId.empty()) {
            brls::Application::notify("akira/settings/enter_online_id_first"_i18n);
            return true;
        }

        brls::Application::notify("akira/settings/looking_up"_i18n);

        DiscoveryManager::getInstance()->lookupPsnAccountId(
            onlineId,
            [this](const std::string& accountId) {
                brls::sync([this, accountId]() {
                    psnAccountIdInput->setValue(accountId);
                    settings->setPsnAccountId(nullptr, accountId);
                    settings->writeFile();
                    updateCredentialsDisplay();
                    brls::Application::notify("akira/settings/account_id_found"_i18n);
                    brls::Logger::info("Found account ID for PSN user");
                });
            },
            [](const std::string& error) {
                brls::sync([error]() {
                    brls::Application::notify(brls::getStr("akira/settings/lookup_failed", error));
                    brls::Logger::error("PSN account lookup failed: {}", error);
                });
            }
        );

        return true;
    });
}

void SettingsTab::initCompanionSection() {
    std::string currentHost = settings->getCompanionHost();
    companionHostInput->init(
        "akira/settings/companion_host"_i18n,
        currentHost,
        [this](std::string text) {
            settings->setCompanionHost(text);
            settings->writeFile();
            brls::Logger::info("Companion host set to {}", text);
        },
        "akira/settings/companion_host_placeholder"_i18n,
        "akira/settings/companion_host_hint"_i18n
    );

    std::string currentPort = std::format("{}", settings->getCompanionPort());
    companionPortInput->init(
        "akira/settings/companion_port"_i18n,
        currentPort,
        [this](std::string text) {
            int port = std::atoi(text.c_str());
            if (port > 0 && port <= 65535) {
                settings->setCompanionPort(port);
                settings->writeFile();
                brls::Logger::info("Companion port set to {}", port);
            } else {
                brls::Application::notify("akira/settings/invalid_port"_i18n);
            }
        },
        "akira/settings/companion_port_placeholder"_i18n,
        "akira/settings/companion_port_hint"_i18n
    );

    fetchPsnBtn->setStyle(&BUTTONSTYLE_GREEN);
    fetchPsnBtn->setBackgroundColor(nvgRGBA(74, 222, 128, 255));

    fetchPsnBtn->registerClickAction([this](brls::View* view) {
        std::string host = companionHostInput->getValue();
        std::string portStr = companionPortInput->getValue();

        if (host.empty()) {
            brls::Application::notify("akira/settings/enter_companion_host_first"_i18n);
            return true;
        }

        int port = std::atoi(portStr.c_str());
        if (port <= 0 || port > 65535) {
            port = 8080;
        }

        brls::Application::notify("akira/settings/fetching_credentials"_i18n);

        DiscoveryManager::getInstance()->fetchCompanionCredentials(
            host, port,
            [this](const std::string& onlineId, const std::string& accountId,
                   const std::string& accessToken, const std::string& refreshToken,
                   int64_t expiresAt, const std::string& duid) {
                brls::sync([this, onlineId, accountId, accessToken, refreshToken, expiresAt, duid]() {
                    if (!onlineId.empty()) {
                        psnOnlineIdInput->setValue(onlineId);
                        settings->setPsnOnlineId(nullptr, onlineId);
                        brls::Logger::info("PSN Online ID set to {}", onlineId);
                    }
                    if (!accountId.empty()) {
                        psnAccountIdInput->setValue(accountId);
                        settings->setPsnAccountId(nullptr, accountId);
                        brls::Logger::info("PSN Account ID set");
                    }
                    if (!accessToken.empty()) {
                        settings->setPsnAccessToken(accessToken);
                    }
                    if (!refreshToken.empty()) {
                        settings->setPsnRefreshToken(refreshToken);
                    }
                    if (expiresAt > 0) {
                        settings->setPsnTokenExpiresAt(expiresAt);
                        brls::Logger::info("PSN token expires at {}", expiresAt);
                    }
                    if (!duid.empty()) {
                        settings->setGlobalDuid(duid);
                        brls::Logger::info("DUID set from companion");
                    }
                    settings->writeFile();
                    brls::Application::notify("akira/settings/credentials_fetched"_i18n);
                    brls::Logger::info("Fetched PSN credentials from companion");
                    updateCredentialsDisplay();
                });
            },
            [](const std::string& error) {
                brls::sync([error]() {
                    brls::Application::notify(brls::getStr("akira/settings/fetch_failed", error));
                    brls::Logger::error("Failed to fetch PSN credentials: {}", error);
                });
            }
        );

        return true;
    });

    refreshTokenBtn->setStyle(&BUTTONSTYLE_GREEN);
    refreshTokenBtn->setBackgroundColor(nvgRGBA(74, 222, 128, 255));

    refreshTokenBtn->registerClickAction([this](brls::View* view) {
        std::string refreshToken = settings->getPsnRefreshToken();
        if (refreshToken.empty()) {
            brls::Application::notify("akira/settings/no_refresh_token"_i18n);
            return true;
        }

        brls::Application::notify("akira/settings/refreshing_token"_i18n);

        DiscoveryManager::getInstance()->refreshPsnToken(
            [this]() {
                brls::sync([this]() {
                    brls::Application::notify("akira/settings/token_refreshed"_i18n);
                    brls::Logger::info("PSN token refreshed");
                    updateCredentialsDisplay();
                });
            },
            [](const std::string& error) {
                brls::sync([error]() {
                    brls::Application::notify(brls::getStr("akira/settings/refresh_failed", error));
                    brls::Logger::error("Failed to refresh PSN token: {}", error);
                });
            }
        );

        return true;
    });

    clearPsnBtn->setStyle(&BUTTONSTYLE_BLUE);
    clearPsnBtn->setBackgroundColor(nvgRGBA(92, 157, 255, 255));

    clearPsnBtn->registerClickAction([this](brls::View* view) {
        auto* dialog = new brls::Dialog("akira/settings/clear_psn_confirm"_i18n);

        dialog->addButton("akira/common/cancel"_i18n, [dialog]() {
            dialog->close();
        });

        dialog->addButton("akira/settings/clear_all"_i18n, [this, dialog]() {
            dialog->close();
            settings->clearPsnTokenData();
            settings->writeFile();
            updateCredentialsDisplay();
            brls::Application::notify("akira/settings/psn_data_cleared"_i18n);
        });

        dialog->open();
        return true;
    });
}

std::string SettingsTab::censorString(const std::string& str) {
    if (str.empty()) return "akira/common/not_set"_i18n;
    if (str.length() <= 5) return str;
    return "****" + str.substr(str.length() - 5);
}

void SettingsTab::updateCredentialsDisplay() {
    credOnlineIdCell->setText("akira/settings/online_id"_i18n);
    std::string onlineId = settings->getPsnOnlineId(nullptr);
    credOnlineIdCell->setDetailText(onlineId.empty() ? "akira/common/not_set"_i18n : onlineId);

    credAccountIdCell->setText("akira/settings/account_id"_i18n);
    std::string accountId = settings->getPsnAccountId(nullptr);
    credAccountIdCell->setDetailText(accountId.empty() ? "akira/common/not_set"_i18n : accountId);

    credAccessTokenCell->setText("akira/settings/access_token"_i18n);
    std::string accessToken = settings->getPsnAccessToken();
    if (credentialsRevealed) {
        if (!accessToken.empty()) {
            std::string displayToken = accessToken.length() > 40 ? accessToken.substr(0, 36) + "..." : accessToken;
            credAccessTokenCell->setDetailText(displayToken);
        } else {
            credAccessTokenCell->setDetailText("akira/common/not_set"_i18n);
        }
    } else {
        credAccessTokenCell->setDetailText(censorString(accessToken));
    }

    credRefreshTokenCell->setText("akira/settings/refresh_token"_i18n);
    std::string refreshToken = settings->getPsnRefreshToken();
    if (credentialsRevealed) {
        if (!refreshToken.empty()) {
            std::string displayToken = refreshToken.length() > 40 ? refreshToken.substr(0, 36) + "..." : refreshToken;
            credRefreshTokenCell->setDetailText(displayToken);
        } else {
            credRefreshTokenCell->setDetailText("akira/common/not_set"_i18n);
        }
    } else {
        credRefreshTokenCell->setDetailText(censorString(refreshToken));
    }

    credTokenExpiryCell->setText("akira/settings/token_expires"_i18n);
    int64_t expiresAt = settings->getPsnTokenExpiresAt();
    if (expiresAt > 0) {
        std::time_t expTime = static_cast<std::time_t>(expiresAt);
        std::tm* tm = std::localtime(&expTime);
        std::ostringstream oss;
        oss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");

        std::time_t now = std::time(nullptr);
        if (now > expTime) {
            credTokenExpiryCell->setDetailText(oss.str() + "akira/settings/expired_suffix"_i18n);
        } else {
            credTokenExpiryCell->setDetailText(oss.str());
        }
    } else {
        credTokenExpiryCell->setDetailText("akira/common/not_set"_i18n);
    }

    credDuidCell->setText("akira/settings/duid"_i18n);
    std::string duid = settings->getGlobalDuid();
    if (credentialsRevealed) {
        if (!duid.empty()) {
            std::string displayDuid = duid.length() > 40 ? duid.substr(0, 36) + "..." : duid;
            credDuidCell->setDetailText(displayDuid);
        } else {
            credDuidCell->setDetailText("akira/common/not_set"_i18n);
        }
    } else {
        credDuidCell->setDetailText(censorString(duid));
    }
}

void SettingsTab::initPortGuessingToggle() {
    bool currentValue = settings->getPortGuessing();

    portGuessingToggle->init(
        "akira/settings/port_guessing"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setPortGuessing(isOn);
            settings->writeFile();
            brls::Logger::info("Port Guessing set to {}", isOn ? "true" : "false");
        }
    );
}

void SettingsTab::initPortGuessingCountSlider() {
    int current = settings->getPortGuessingCount();
    constexpr int minVal = 1;
    constexpr int maxVal = 75;
    float normalized = static_cast<float>(current - minVal) / (maxVal - minVal);
    normalized = std::max(0.0f, std::min(1.0f, normalized));

    portGuessingCountSlider->detail->setWidth(60);
    portGuessingCountSlider->detail->setShrink(0);
    portGuessingCountSlider->init(
        "akira/settings/port_guess_count"_i18n,
        normalized,
        [this](float value) {
            int count = static_cast<int>(1 + value * 74);
            count = std::max(1, std::min(75, count));
            settings->setPortGuessingCount(count);
            portGuessingCountSlider->detail->setText(std::format("{}", count));
            settings->writeFile();
        }
    );
    portGuessingCountSlider->detail->setText(std::format("{}", current));
    portGuessingCountSlider->slider->setStep(1.0f / 74.0f);
}

void SettingsTab::initPortGuessingSocksSlider() {
    int current = settings->getPortGuessingSocks();
    constexpr int minVal = 1;
    constexpr int maxVal = 250;
    float normalized = static_cast<float>(current - minVal) / (maxVal - minVal);
    normalized = std::max(0.0f, std::min(1.0f, normalized));

    portGuessingSocksSlider->detail->setWidth(60);
    portGuessingSocksSlider->detail->setShrink(0);
    portGuessingSocksSlider->init(
        "akira/settings/probe_socket_count"_i18n,
        normalized,
        [this](float value) {
            int count = static_cast<int>(1 + value * 249);
            count = std::max(1, std::min(250, count));
            settings->setPortGuessingSocks(count);
            portGuessingSocksSlider->detail->setText(std::format("{}", count));
            settings->writeFile();
        }
    );
    portGuessingSocksSlider->detail->setText(std::format("{}", current));
    portGuessingSocksSlider->slider->setStep(1.0f / 249.0f);
}

void SettingsTab::initPowerUserSection() {
    lastPowerUserClick = std::chrono::steady_clock::now();

    versionLabel->registerClickAction([this](brls::View* view) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPowerUserClick).count();

        if (elapsed > 3000) {
            powerUserClickCount = 0;
        }

        lastPowerUserClick = now;
        powerUserClickCount++;

        if (powerUserClickCount >= 7 && !settings->getPowerUserMenuUnlocked()) {
            settings->setPowerUserMenuUnlocked(true);
            settings->writeFile();
            brls::Application::notify("akira/settings/power_user_unlocked"_i18n);
            brls::Logger::info("Power User Menu unlocked");
            updatePowerUserVisibility();
            brls::Application::giveFocus(unlockBitrateMaxToggle);
            powerUserClickCount = 0;
        }

        return true;
    });

    unlockBitrateMaxToggle->init(
        "akira/settings/unlock_bitrate_max"_i18n,
        settings->getUnlockBitrateMax(),
        [this](bool isOn) {
            settings->setUnlockBitrateMax(isOn);
            settings->writeFile();
            initLocalBitrateSlider();
            initRemoteBitrateSlider();
            brls::Logger::info("Unlock Bitrate Max set to {}", isOn ? "true" : "false");
        }
    );

    ipcStatsToggle->init(
        "akira/settings/ipc_stats"_i18n,
        settings->getIpcStatsEnabled(),
        [this](bool isOn) {
            settings->setIpcStatsEnabled(isOn);
            settings->writeFile();
            brls::Logger::info("IPC Stats set to {}", isOn ? "true" : "false");
        }
    );

    autoReconnectToggle->init(
        "akira/settings/auto_reconnect"_i18n,
        settings->getAutoReconnect(),
        [this](bool isOn) {
            settings->setAutoReconnect(isOn);
            settings->writeFile();
            brls::Logger::info("Auto Reconnect set to {}", isOn ? "true" : "false");
        }
    );

    updatePowerUserVisibility();
}

void SettingsTab::updatePowerUserVisibility() {
    if (settings->getPowerUserMenuUnlocked()) {
        powerUserSection->setVisibility(brls::Visibility::VISIBLE);
    } else {
        powerUserSection->setVisibility(brls::Visibility::GONE);
    }
}


void SettingsTab::runGhashBenchmark() {
    auto* benchmarkView = new BenchmarkView();
    brls::Application::pushActivity(new brls::Activity(benchmarkView));
    benchmarkView->startBenchmark();
}

void SettingsTab::initRequestIdrOnFecFailureToggle() {
    bool currentValue = settings->getRequestIdrOnFecFailure();

    requestIdrOnFecFailureToggle->init(
        "akira/settings/request_idr"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setRequestIdrOnFecFailure(isOn);
            settings->writeFile();
        }
    );
}

void SettingsTab::initPacketLossMaxSlider() {
    float currentValue = settings->getPacketLossMax();
    int currentPercent = static_cast<int>(currentValue * 100.0f);

    packetLossMaxSlider->detail->setWidth(60);
    packetLossMaxSlider->detail->setShrink(0);
    packetLossMaxSlider->init(
        "akira/settings/packet_loss_max"_i18n,
        currentValue,
        [this](float value) {
            int percent = static_cast<int>(value * 100.0f);
            settings->setPacketLossMax(value);
            packetLossMaxSlider->detail->setText(brls::getStr("akira/settings/percent_format", percent));
            settings->writeFile();
        }
    );
    packetLossMaxSlider->detail->setText(brls::getStr("akira/settings/percent_format", currentPercent));
    packetLossMaxSlider->slider->setStep(0.05f);
}

void SettingsTab::initEnableFileLoggingToggle() {
    bool currentValue = settings->getEnableFileLogging();

    enableFileLoggingToggle->init(
        "akira/settings/file_logging"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setEnableFileLogging(isOn);
            if (!isOn) {
                settings->setDebugLwipLog(false);
                settings->setDebugWireguardLog(false);
                settings->setDebugRenderLog(false);
                settings->setDebugChiakiLog(false);
                settings->setDebugDiscoveryLog(false);
                settings->setDebugFfmpegLog(false);
                debugLwipLogToggle->setOn(false, false);
                debugWireguardLogToggle->setOn(false, false);
                debugRenderLogToggle->setOn(false, false);
                debugChiakiLogToggle->setOn(false, false);
                debugDiscoveryLogToggle->setOn(false, false);
                debugFfmpegLogToggle->setOn(false, false);
            }
            settings->writeFile();
        }
    );
}

void SettingsTab::initDebugLwipLogToggle() {
    bool currentValue = settings->getDebugLwipLog();

    debugLwipLogToggle->init(
        "akira/settings/lwip_log"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setDebugLwipLog(isOn);
            settings->writeFile();
        }
    );
}

void SettingsTab::initDebugWireguardLogToggle() {
    bool currentValue = settings->getDebugWireguardLog();

    debugWireguardLogToggle->init(
        "akira/settings/wireguard_log"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setDebugWireguardLog(isOn);
            settings->writeFile();
        }
    );
}

void SettingsTab::initDebugRenderLogToggle() {
    bool currentValue = settings->getDebugRenderLog();

    debugRenderLogToggle->init(
        "akira/settings/render_log"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setDebugRenderLog(isOn);
            settings->writeFile();
        }
    );
}

void SettingsTab::initDebugChiakiLogToggle() {
    bool currentValue = settings->getDebugChiakiLog();

    debugChiakiLogToggle->init(
        "akira/settings/chiaki_log"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setDebugChiakiLog(isOn);
            settings->writeFile();
        }
    );
}

void SettingsTab::initDebugDiscoveryLogToggle() {
    bool currentValue = settings->getDebugDiscoveryLog();

    debugDiscoveryLogToggle->init(
        "akira/settings/discovery_log"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setDebugDiscoveryLog(isOn);
            settings->writeFile();
        }
    );
}

void SettingsTab::initDebugFfmpegLogToggle() {
    bool currentValue = settings->getDebugFfmpegLog();

    debugFfmpegLogToggle->init(
        "akira/settings/ffmpeg_log"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setDebugFfmpegLog(isOn);
            settings->writeFile();
        }
    );
}
