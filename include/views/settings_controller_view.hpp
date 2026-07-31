#ifndef AKIRA_SETTINGS_CONTROLLER_VIEW_HPP
#define AKIRA_SETTINGS_CONTROLLER_VIEW_HPP

#include <borealis.hpp>
#include <borealis/views/cells/cell_bool.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/cells/cell_selector.hpp>
#include <borealis/views/cells/cell_slider.hpp>

#include "core/settings_manager.hpp"

class SettingsControllerView : public brls::Box {
public:
    SettingsControllerView();

private:
    BRLS_BIND(brls::SelectorCell, hapticSelector, "settings/haptic");
    BRLS_BIND(brls::SliderCell, rumbleFreqLowSlider, "settings/rumbleFreqLow");
    BRLS_BIND(brls::SliderCell, rumbleFreqHighSlider, "settings/rumbleFreqHigh");
    BRLS_BIND(brls::SliderCell, rumbleEnvelopeAttackSlider, "settings/rumbleEnvelopeAttack");
    BRLS_BIND(brls::SliderCell, rumbleEnvelopeDecaySlider, "settings/rumbleEnvelopeDecay");
    BRLS_BIND(brls::SelectorCell, gyroSourceSelector, "settings/gyroSource");
    BRLS_BIND(brls::BooleanCell, sleepOnExitToggle, "settings/sleepOnExit");
    BRLS_BIND(brls::DetailCell, buttonMappingCell, "settings/buttonMapping");

    SettingsManager* settings = nullptr;

    void initHapticSelector();
    void initRumbleFreqLowSlider();
    void initRumbleFreqHighSlider();
    void initRumbleEnvelopeAttackSlider();
    void initRumbleEnvelopeDecaySlider();
    void initGyroSourceSelector();
    void initSleepOnExitToggle();
    void initButtonMappingCell();
};

#endif // AKIRA_SETTINGS_CONTROLLER_VIEW_HPP
