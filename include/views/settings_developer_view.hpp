#ifndef AKIRA_SETTINGS_DEVELOPER_VIEW_HPP
#define AKIRA_SETTINGS_DEVELOPER_VIEW_HPP

#include <borealis.hpp>
#include <borealis/views/cells/cell_bool.hpp>

#include "core/settings_manager.hpp"

class SettingsDeveloperView : public brls::Box {
public:
    SettingsDeveloperView();

private:
    BRLS_BIND(brls::BooleanCell, fakeHostsToggle, "settings/devFakeHosts");

    SettingsManager* settings = nullptr;
};

#endif // AKIRA_SETTINGS_DEVELOPER_VIEW_HPP
