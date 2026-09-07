#ifndef AKIRA_SETTINGS_CONTROLLERS_VIEW_HPP
#define AKIRA_SETTINGS_CONTROLLERS_VIEW_HPP

#include <borealis.hpp>
#include <borealis/views/cells/cell_detail.hpp>

/*
 * Which controllers this console can see, and a way into each one's settings.
 *
 * Replaces the old "Analog Triggers (Bluetooth)" row, which answered "what can
 * the backend see" in the narrowest possible terms - a vid, a pid and the word
 * trained - when the useful answer is which controllers are attached.
 */
class SettingsControllersView : public brls::Box {
public:
    SettingsControllersView();

private:
    void rebuild();

    brls::Box* list = nullptr;
};

#endif // AKIRA_SETTINGS_CONTROLLERS_VIEW_HPP
