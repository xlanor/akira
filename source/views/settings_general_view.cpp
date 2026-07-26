#include "views/settings_general_view.hpp"

#include <borealis/core/i18n.hpp>
#include <string>
#include <vector>

using namespace brls::literals;

SettingsGeneralView::SettingsGeneralView() {
    this->inflateFromXMLRes("xml/settings/general.xml");

    settings = SettingsManager::getInstance();

    initLanguageSelector();
    initEnableThreadAffinityToggle();
    initHolepunchRetryToggle();
    initRequestIdrOnFecFailureToggle();
    initPacketLossMaxSlider();
    initVersionUnlock();
}

void SettingsGeneralView::initEnableThreadAffinityToggle() {
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

void SettingsGeneralView::initHolepunchRetryToggle() {
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

void SettingsGeneralView::initRequestIdrOnFecFailureToggle() {
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

void SettingsGeneralView::initPacketLossMaxSlider() {
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
    packetLossMaxSlider->slider->setDiscreteStep(0.05f);
}

void SettingsGeneralView::initLanguageSelector() {
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

void SettingsGeneralView::initVersionUnlock() {
    lastPowerUserClick = std::chrono::steady_clock::now();

    versionLabel->registerClickAction([this](brls::View*) {
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
            powerUserClickCount = 0;
        }

        return true;
    });
}
