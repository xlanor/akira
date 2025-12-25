#include "views/settings_tab.hpp"
#include "core/discovery_manager.hpp"

SettingsTab::SettingsTab() {
    this->inflateFromXMLRes("xml/tabs/settings.xml");

    settings = SettingsManager::getInstance();

    initResolutionSelector();
    initFpsSelector();
    initHapticSelector();
    initPsnAccountSection();
}

brls::View* SettingsTab::create() {
    return new SettingsTab();
}

void SettingsTab::initResolutionSelector() {
    std::vector<std::string> options = {"360p", "540p", "720p", "1080p"};

    int currentIndex = 2;  // Default 720p
    auto current = settings->getVideoResolution(nullptr);
    switch (current) {
        case CHIAKI_VIDEO_RESOLUTION_PRESET_360p: currentIndex = 0; break;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_540p: currentIndex = 1; break;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_720p: currentIndex = 2; break;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p: currentIndex = 3; break;
    }

    resolutionSelector->init(
        "Resolution",
        options,
        currentIndex,
        [](int selected) {},  // on focus lost
        [this](int selected) {
            ChiakiVideoResolutionPreset preset;
            switch (selected) {
                case 0: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_360p; break;
                case 1: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_540p; break;
                case 2: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_720p; break;
                case 3: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_1080p; break;
                default: preset = CHIAKI_VIDEO_RESOLUTION_PRESET_720p; break;
            }
            settings->setVideoResolution(nullptr, preset);
            settings->writeFile();
            brls::Logger::info("Resolution set to {}", SettingsManager::resolutionToString(preset));
        }
    );
}

void SettingsTab::initFpsSelector() {
    std::vector<std::string> options = {"30 FPS", "60 FPS"};

    int currentIndex = 1;  // Default 60
    if (settings->getVideoFPS(nullptr) == CHIAKI_VIDEO_FPS_PRESET_30) {
        currentIndex = 0;
    }

    fpsSelector->init(
        "Frame Rate",
        options,
        currentIndex,
        [](int selected) {},
        [this](int selected) {
            auto preset = selected == 0 ? CHIAKI_VIDEO_FPS_PRESET_30 : CHIAKI_VIDEO_FPS_PRESET_60;
            settings->setVideoFPS(nullptr, preset);
            settings->writeFile();
            brls::Logger::info("FPS set to {}", SettingsManager::fpsToString(preset));
        }
    );
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

void SettingsTab::initPsnAccountSection() {
    // Initialize PSN Online ID input
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

    // Initialize Account ID input
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

    // Set up lookup button
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
