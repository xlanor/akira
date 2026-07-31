#include "views/settings_developer_view.hpp"
#include "views/host_list_tab.hpp"
#include "views/update_flow.hpp"

#include <borealis/core/i18n.hpp>

using namespace brls::literals;

SettingsDeveloperView::SettingsDeveloperView() {
    this->inflateFromXMLRes("xml/settings/developer.xml");

    settings = SettingsManager::getInstance();

    fakeHostsToggle->init(
        "akira/settings/dev_fake_hosts"_i18n,
        settings->getDevFakeHosts(),
        [this](bool isOn) {
            settings->setDevFakeHosts(isOn);
            settings->writeFile();
            HostListTab::notifyActiveProfileChanged();
        });

    simulateUpdateCell->setText("akira/settings/simulate_update"_i18n);
    simulateUpdateCell->registerClickAction([](brls::View*) {
        akira::UpdateFlow::simulate();
        return true;
    });
}
