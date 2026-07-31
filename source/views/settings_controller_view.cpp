#include "views/settings_controller_view.hpp"
#include "views/controller_remap_view.hpp"

#include <borealis/core/i18n.hpp>
#include <algorithm>

using namespace brls::literals;

SettingsControllerView::SettingsControllerView() {
    this->inflateFromXMLRes("xml/settings/controller.xml");

    settings = SettingsManager::getInstance();

    initHapticSelector();
    initRumbleFreqLowSlider();
    initRumbleFreqHighSlider();
    initRumbleEnvelopeAttackSlider();
    initRumbleEnvelopeDecaySlider();
    initGyroSourceSelector();
    initSleepOnExitToggle();
    initButtonMappingCell();

}

void SettingsControllerView::initHapticSelector() {
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

void SettingsControllerView::initRumbleFreqLowSlider() {
    constexpr float MIN_FREQ = 40.0f;
    constexpr float MAX_FREQ = 320.0f;

    float currentFreq = settings->getRumbleFreqLow();
    float normalized = (currentFreq - MIN_FREQ) / (MAX_FREQ - MIN_FREQ);
    normalized = std::max(0.0f, std::min(1.0f, normalized));

    rumbleFreqLowSlider->detail->setWidth(100);
    rumbleFreqLowSlider->detail->setShrink(0);
    rumbleFreqLowSlider->slider->setDiscreteStep(5.0f / (MAX_FREQ - MIN_FREQ));
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

void SettingsControllerView::initRumbleFreqHighSlider() {
    constexpr float MIN_FREQ = 40.0f;
    constexpr float MAX_FREQ = 320.0f;

    float currentFreq = settings->getRumbleFreqHigh();
    float normalized = (currentFreq - MIN_FREQ) / (MAX_FREQ - MIN_FREQ);
    normalized = std::max(0.0f, std::min(1.0f, normalized));

    rumbleFreqHighSlider->detail->setWidth(100);
    rumbleFreqHighSlider->detail->setShrink(0);
    rumbleFreqHighSlider->slider->setDiscreteStep(5.0f / (MAX_FREQ - MIN_FREQ));
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

void SettingsControllerView::initRumbleEnvelopeAttackSlider() {
    constexpr float MIN_ATTACK = 0.20f;
    constexpr float MAX_ATTACK = 1.00f;

    float currentAttack = settings->getRumbleEnvelopeAttack();
    float normalized = (currentAttack - MIN_ATTACK) / (MAX_ATTACK - MIN_ATTACK);
    normalized = std::max(0.0f, std::min(1.0f, normalized));

    rumbleEnvelopeAttackSlider->detail->setWidth(100);
    rumbleEnvelopeAttackSlider->detail->setShrink(0);
    rumbleEnvelopeAttackSlider->slider->setDiscreteStep(0.01f / (MAX_ATTACK - MIN_ATTACK));
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

void SettingsControllerView::initRumbleEnvelopeDecaySlider() {
    constexpr float MIN_DECAY = 0.50f;
    constexpr float MAX_DECAY = 0.95f;

    float currentDecay = settings->getRumbleEnvelopeDecay();
    float normalized = (currentDecay - MIN_DECAY) / (MAX_DECAY - MIN_DECAY);
    normalized = std::max(0.0f, std::min(1.0f, normalized));

    rumbleEnvelopeDecaySlider->detail->setWidth(100);
    rumbleEnvelopeDecaySlider->detail->setShrink(0);
    rumbleEnvelopeDecaySlider->slider->setDiscreteStep(0.01f / (MAX_DECAY - MIN_DECAY));
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

void SettingsControllerView::initGyroSourceSelector() {
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

void SettingsControllerView::initSleepOnExitToggle() {
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

void SettingsControllerView::initButtonMappingCell() {
    buttonMappingCell->setText("akira/settings/button_mapping"_i18n);
    buttonMappingCell->setDetailText("akira/common/configure"_i18n);

    buttonMappingCell->registerClickAction([](brls::View*) {
        auto* remapView = new ControllerRemapView();
        brls::Application::pushActivity(new brls::Activity(remapView), brls::TransitionAnimation::NONE);
        return true;
    });
}
