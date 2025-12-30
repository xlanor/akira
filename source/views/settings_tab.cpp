#include "views/settings_tab.hpp"
#include "views/benchmark_view.hpp"
#include "core/discovery_manager.hpp"
#include "crypto/libnx/gmac.h"

#include <ctime>
#include <iomanip>
#include <sstream>

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

    initLocalResolutionSelector();
    initRemoteResolutionSelector();
    initLocalFpsSelector();
    initRemoteFpsSelector();
    initLocalBitrateSlider();
    initRemoteBitrateSlider();
    initHapticSelector();
    initInvertABToggle();
    initHolepunchRetryToggle();
    initPsnAccountSection();
    initCompanionSection();
    initPowerUserSection();
    initExperimentalCryptoToggle();

    runBenchmarkBtn->registerClickAction([this](brls::View*) {
        runGhashBenchmark();
        return true;
    });

    revealCredentialsBtn->registerClickAction([this](brls::View*) {
        credentialsRevealed = !credentialsRevealed;
        revealCredentialsBtn->setText(credentialsRevealed ? "Hide Secrets" : "Reveal Secrets");
        updateCredentialsDisplay();
        return true;
    });

    updateCredentialsDisplay();
}

brls::View* SettingsTab::create() {
    return new SettingsTab();
}

void SettingsTab::initLocalResolutionSelector() {
    std::vector<std::string> options = {"360p", "540p", "720p", "1080p"};

    int currentIndex = 2;
    auto current = settings->getLocalVideoResolution();
    switch (current) {
        case CHIAKI_VIDEO_RESOLUTION_PRESET_360p: currentIndex = 0; break;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_540p: currentIndex = 1; break;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_720p: currentIndex = 2; break;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p: currentIndex = 3; break;
    }

    localResolutionSelector->init(
        "Local Resolution",
        options,
        currentIndex,
        [](int selected) {},
        [this](int selected) {
            ChiakiVideoResolutionPreset preset;
            switch (selected) {
                case 0: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_360p; break;
                case 1: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_540p; break;
                case 2: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_720p; break;
                case 3: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_1080p; break;
                default: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_720p; break;
            }
            settings->setLocalVideoResolution(preset);
            updateLocalBitrateSlider();
            settings->writeFile();
            brls::Logger::info("Local resolution set to {}", SettingsManager::resolutionToString(preset));
        }
    );
}

void SettingsTab::initRemoteResolutionSelector() {
    std::vector<std::string> options = {"360p", "540p", "720p", "1080p"};

    int currentIndex = 2;
    auto current = settings->getRemoteVideoResolution();
    switch (current) {
        case CHIAKI_VIDEO_RESOLUTION_PRESET_360p: currentIndex = 0; break;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_540p: currentIndex = 1; break;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_720p: currentIndex = 2; break;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p: currentIndex = 3; break;
    }

    remoteResolutionSelector->init(
        "Remote Resolution",
        options,
        currentIndex,
        [](int selected) {},
        [this](int selected) {
            ChiakiVideoResolutionPreset preset;
            switch (selected) {
                case 0: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_360p; break;
                case 1: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_540p; break;
                case 2: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_720p; break;
                case 3: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_1080p; break;
                default: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_720p; break;
            }
            settings->setRemoteVideoResolution(preset);
            updateRemoteBitrateSlider();
            settings->writeFile();
            brls::Logger::info("Remote resolution set to {}", SettingsManager::resolutionToString(preset));
        }
    );
}

void SettingsTab::initLocalFpsSelector() {
    std::vector<std::string> options = {"30 FPS", "60 FPS"};

    int currentIndex = 1;
    if (settings->getLocalVideoFPS() == CHIAKI_VIDEO_FPS_PRESET_30) {
        currentIndex = 0;
    }

    localFpsSelector->init(
        "Local Frame Rate",
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
    std::vector<std::string> options = {"30 FPS", "60 FPS"};

    int currentIndex = 1;
    if (settings->getRemoteVideoFPS() == CHIAKI_VIDEO_FPS_PRESET_30) {
        currentIndex = 0;
    }

    remoteFpsSelector->init(
        "Remote Frame Rate",
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
        "Local Network Bitrate",
        normalizedValue,
        [this](float value) {
            auto resolution = settings->getLocalVideoResolution();
            int maxBitrate = settings->getMaxBitrateForResolution(resolution);
            int minBitrate = settings->getMinBitrateForResolution(resolution);
            int bitrate = minBitrate + static_cast<int>(value * (maxBitrate - minBitrate));
            settings->setLocalVideoBitrate(bitrate);
            localBitrateSlider->detail->setText(std::to_string(bitrate) + " kbps");
            settings->writeFile();
            brls::Logger::info("Local bitrate set to {}", bitrate);
        }
    );

    localBitrateSlider->detail->setText(std::to_string(currentBitrate) + " kbps");
}

void SettingsTab::updateLocalBitrateSlider() {
    auto resolution = settings->getLocalVideoResolution();
    int defaultBitrate = SettingsManager::getDefaultBitrateForResolution(resolution);
    int maxBitrate = settings->getMaxBitrateForResolution(resolution);
    int minBitrate = settings->getMinBitrateForResolution(resolution);

    settings->setLocalVideoBitrate(defaultBitrate);

    float normalizedValue = static_cast<float>(defaultBitrate - minBitrate) / (maxBitrate - minBitrate);
    localBitrateSlider->slider->setProgress(normalizedValue);
    localBitrateSlider->detail->setText(std::to_string(defaultBitrate) + " kbps");
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
        "Remote Play Bitrate",
        normalizedValue,
        [this](float value) {
            auto resolution = settings->getRemoteVideoResolution();
            int maxBitrate = settings->getMaxBitrateForResolution(resolution);
            int minBitrate = settings->getMinBitrateForResolution(resolution);
            int bitrate = minBitrate + static_cast<int>(value * (maxBitrate - minBitrate));
            settings->setRemoteVideoBitrate(bitrate);
            remoteBitrateSlider->detail->setText(std::to_string(bitrate) + " kbps");
            settings->writeFile();
            brls::Logger::info("Remote bitrate set to {}", bitrate);
        }
    );

    remoteBitrateSlider->detail->setText(std::to_string(currentBitrate) + " kbps");
}

void SettingsTab::updateRemoteBitrateSlider() {
    auto resolution = settings->getRemoteVideoResolution();
    int defaultBitrate = SettingsManager::getDefaultBitrateForResolution(resolution);
    int maxBitrate = settings->getMaxBitrateForResolution(resolution);
    int minBitrate = settings->getMinBitrateForResolution(resolution);

    settings->setRemoteVideoBitrate(defaultBitrate);

    float normalizedValue = static_cast<float>(defaultBitrate - minBitrate) / (maxBitrate - minBitrate);
    remoteBitrateSlider->slider->setProgress(normalizedValue);
    remoteBitrateSlider->detail->setText(std::to_string(defaultBitrate) + " kbps");
}

void SettingsTab::initHapticSelector() {
    std::vector<std::string> options = {"Disabled", "Weak", "Strong"};

    int currentIndex = static_cast<int>(settings->getHaptic(nullptr));

    hapticSelector->init(
        "Haptic Feedback",
        options,
        currentIndex,
        [](int selected) {},
        [this](int selected) {
            settings->setHaptic(nullptr, static_cast<HapticPreset>(selected));
            settings->writeFile();
            brls::Logger::info("Haptic set to {}", selected);
        }
    );
}

void SettingsTab::initInvertABToggle() {
    bool currentValue = settings->getInvertAB();

    invertABToggle->init(
        "Invert A and B",
        currentValue,
        [this](bool isOn) {
            settings->setInvertAB(isOn);
            settings->writeFile();
            brls::Logger::info("Invert A/B set to {}", isOn ? "true" : "false");
        }
    );
}

void SettingsTab::initHolepunchRetryToggle() {
    bool currentValue = settings->getHolepunchRetry();

    holepunchRetryToggle->init(
        "Enable Holepunch Retry",
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
        "PSN Online ID",
        currentOnlineId,
        [this](std::string text) {
            settings->setPsnOnlineId(nullptr, text);
            settings->writeFile();
            brls::Logger::info("PSN Online ID set to {}", text);
        },
        "Enter your PSN username",
        "Used to look up your Account ID"
    );

    std::string currentAccountId = settings->getPsnAccountId(nullptr);
    psnAccountIdInput->init(
        "Account ID (Base64)",
        currentAccountId,
        [this](std::string text) {
            settings->setPsnAccountId(nullptr, text);
            settings->writeFile();
            brls::Logger::info("PSN Account ID set");
        },
        "Base64 encoded account ID",
        "Required for PS5 and PS4 9.0+"
    );

    lookupBtn->setStyle(&BUTTONSTYLE_BLUE);
    lookupBtn->setBackgroundColor(nvgRGBA(92, 157, 255, 255));

    lookupBtn->registerClickAction([this](brls::View* view) {
        std::string onlineId = psnOnlineIdInput->getValue();
        if (onlineId.empty()) {
            brls::Application::notify("Please enter a PSN Online ID first");
            return true;
        }

        brls::Application::notify("Looking up account ID...");

        DiscoveryManager::getInstance()->lookupPsnAccountId(
            onlineId,
            [this](const std::string& accountId) {
                brls::sync([this, accountId]() {
                    psnAccountIdInput->setValue(accountId);
                    settings->setPsnAccountId(nullptr, accountId);
                    settings->writeFile();
                    updateCredentialsDisplay();
                    brls::Application::notify("Account ID found!");
                    brls::Logger::info("Found account ID for PSN user");
                });
            },
            [](const std::string& error) {
                brls::sync([error]() {
                    brls::Application::notify("Lookup failed: " + error);
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
        "Companion Host",
        currentHost,
        [this](std::string text) {
            settings->setCompanionHost(text);
            settings->writeFile();
            brls::Logger::info("Companion host set to {}", text);
        },
        "IP address of companion app",
        "e.g., 192.168.1.100"
    );

    std::string currentPort = std::to_string(settings->getCompanionPort());
    companionPortInput->init(
        "Companion Port",
        currentPort,
        [this](std::string text) {
            int port = std::atoi(text.c_str());
            if (port > 0 && port <= 65535) {
                settings->setCompanionPort(port);
                settings->writeFile();
                brls::Logger::info("Companion port set to {}", port);
            } else {
                brls::Application::notify("Invalid port number");
            }
        },
        "Port number (1-65535)",
        "Default: 8080"
    );

    fetchPsnBtn->setStyle(&BUTTONSTYLE_GREEN);
    fetchPsnBtn->setBackgroundColor(nvgRGBA(74, 222, 128, 255));

    fetchPsnBtn->registerClickAction([this](brls::View* view) {
        std::string host = companionHostInput->getValue();
        std::string portStr = companionPortInput->getValue();

        if (host.empty()) {
            brls::Application::notify("Please enter companion host first");
            return true;
        }

        int port = std::atoi(portStr.c_str());
        if (port <= 0 || port > 65535) {
            port = 8080;
        }

        brls::Application::notify("Fetching PSN credentials...");

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
                    brls::Application::notify("PSN credentials fetched!");
                    brls::Logger::info("Fetched PSN credentials from companion");
                    updateCredentialsDisplay();
                });
            },
            [](const std::string& error) {
                brls::sync([error]() {
                    brls::Application::notify("Fetch failed: " + error);
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
            brls::Application::notify("No refresh token stored. Fetch credentials first.");
            return true;
        }

        brls::Application::notify("Refreshing PSN token...");

        DiscoveryManager::getInstance()->refreshPsnToken(
            [this]() {
                brls::sync([this]() {
                    brls::Application::notify("Token refreshed successfully!");
                    brls::Logger::info("PSN token refreshed");
                    updateCredentialsDisplay();
                });
            },
            [](const std::string& error) {
                brls::sync([error]() {
                    brls::Application::notify("Refresh failed: " + error);
                    brls::Logger::error("Failed to refresh PSN token: {}", error);
                });
            }
        );

        return true;
    });

    clearPsnBtn->setStyle(&BUTTONSTYLE_BLUE);
    clearPsnBtn->setBackgroundColor(nvgRGBA(92, 157, 255, 255));

    clearPsnBtn->registerClickAction([this](brls::View* view) {
        auto* dialog = new brls::Dialog("Are you sure you want to clear all PSN data?\n\nThis will remove:\n- Access Token\n- Refresh Token\n- Token Expiry\n\nYou will need to fetch credentials again.");

        dialog->addButton("Cancel", [dialog]() {
            dialog->close();
        });

        dialog->addButton("Clear All", [this, dialog]() {
            dialog->close();
            settings->clearPsnTokenData();
            settings->writeFile();
            updateCredentialsDisplay();
            brls::Application::notify("PSN data cleared");
        });

        dialog->open();
        return true;
    });
}

std::string SettingsTab::censorString(const std::string& str) {
    if (str.empty()) return "Not set";
    if (str.length() <= 5) return str;
    return "****" + str.substr(str.length() - 5);
}

void SettingsTab::updateCredentialsDisplay() {
    credOnlineIdCell->setText("Online ID");
    std::string onlineId = settings->getPsnOnlineId(nullptr);
    credOnlineIdCell->setDetailText(onlineId.empty() ? "Not set" : onlineId);

    credAccountIdCell->setText("Account ID");
    std::string accountId = settings->getPsnAccountId(nullptr);
    credAccountIdCell->setDetailText(accountId.empty() ? "Not set" : accountId);

    credAccessTokenCell->setText("Access Token");
    std::string accessToken = settings->getPsnAccessToken();
    if (credentialsRevealed) {
        if (!accessToken.empty()) {
            std::string displayToken = accessToken.length() > 40 ? accessToken.substr(0, 36) + "..." : accessToken;
            credAccessTokenCell->setDetailText(displayToken);
        } else {
            credAccessTokenCell->setDetailText("Not set");
        }
    } else {
        credAccessTokenCell->setDetailText(censorString(accessToken));
    }

    credRefreshTokenCell->setText("Refresh Token");
    std::string refreshToken = settings->getPsnRefreshToken();
    if (credentialsRevealed) {
        if (!refreshToken.empty()) {
            std::string displayToken = refreshToken.length() > 40 ? refreshToken.substr(0, 36) + "..." : refreshToken;
            credRefreshTokenCell->setDetailText(displayToken);
        } else {
            credRefreshTokenCell->setDetailText("Not set");
        }
    } else {
        credRefreshTokenCell->setDetailText(censorString(refreshToken));
    }

    credTokenExpiryCell->setText("Token Expires");
    int64_t expiresAt = settings->getPsnTokenExpiresAt();
    if (expiresAt > 0) {
        std::time_t expTime = static_cast<std::time_t>(expiresAt);
        std::tm* tm = std::localtime(&expTime);
        std::ostringstream oss;
        oss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");

        std::time_t now = std::time(nullptr);
        if (now > expTime) {
            credTokenExpiryCell->setDetailText(oss.str() + " (EXPIRED)");
        } else {
            credTokenExpiryCell->setDetailText(oss.str());
        }
    } else {
        credTokenExpiryCell->setDetailText("Not set");
    }

    credDuidCell->setText("DUID");
    std::string duid = settings->getGlobalDuid();
    if (credentialsRevealed) {
        if (!duid.empty()) {
            std::string displayDuid = duid.length() > 40 ? duid.substr(0, 36) + "..." : duid;
            credDuidCell->setDetailText(displayDuid);
        } else {
            credDuidCell->setDetailText("Not set");
        }
    } else {
        credDuidCell->setDetailText(censorString(duid));
    }
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
            brls::Application::notify("Power User Menu unlocked!");
            brls::Logger::info("Power User Menu unlocked");
            updatePowerUserVisibility();
            brls::Application::giveFocus(unlockBitrateMaxToggle);
            powerUserClickCount = 0;
        }

        return true;
    });

    unlockBitrateMaxToggle->init(
        "Unlock Bitrate Max",
        settings->getUnlockBitrateMax(),
        [this](bool isOn) {
            settings->setUnlockBitrateMax(isOn);
            settings->writeFile();
            initLocalBitrateSlider();
            initRemoteBitrateSlider();
            brls::Logger::info("Unlock Bitrate Max set to {}", isOn ? "true" : "false");
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

void SettingsTab::initExperimentalCryptoToggle() {
    bool currentValue = settings->getEnableExperimentalCrypto();

    experimentalCryptoToggle->init(
        "Enable Experimental PMULL GHASH",
        currentValue,
        [this](bool isOn) {
            settings->setEnableExperimentalCrypto(isOn);
            settings->writeFile();
            if (isOn) {
                chiaki_libnx_set_ghash_mode(CHIAKI_LIBNX_GHASH_PMULL);
            } else {
                chiaki_libnx_set_ghash_mode(CHIAKI_LIBNX_GHASH_TABLE);
            }
            brls::Logger::info("GHASH mode: {}", isOn ? "PMULL" : "TABLE");
        }
    );
}

void SettingsTab::runGhashBenchmark() {
    auto* benchmarkView = new BenchmarkView();
    brls::Application::pushActivity(new brls::Activity(benchmarkView));
    benchmarkView->startBenchmark();
}
