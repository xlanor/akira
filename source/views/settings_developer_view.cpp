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

    static const std::vector<std::string> wsNodes = {
        "",
        "44-231-131-164-pushcl.np.communication.playstation.net",
        "44-234-162-2-pushcl.np.communication.playstation.net",
        "34-215-159-77-pushcl.np.communication.playstation.net",
        "44-233-186-110-pushcl.np.communication.playstation.net",
    };

    std::vector<std::string> wsNodeLabels = { "akira/settings/dev_force_ws_node_off"_i18n };
    for (size_t i = 1; i < wsNodes.size(); i++)
        wsNodeLabels.push_back(wsNodes[i].substr(0, wsNodes[i].find("-pushcl")));

    const std::string currentWsNode = settings->getDevForceWsFqdn();
    int currentWsIndex = 0;
    for (size_t i = 1; i < wsNodes.size(); i++)
    {
        if (wsNodes[i] == currentWsNode)
        {
            currentWsIndex = static_cast<int>(i);
            break;
        }
    }

    forceWsNodeSelector->init(
        "akira/settings/dev_force_ws_node"_i18n,
        wsNodeLabels,
        currentWsIndex,
        [this](int selected) {
            settings->setDevForceWsFqdn(wsNodes[selected]);
            settings->writeFile();
        });

    simulateUpdateCell->setText("akira/settings/simulate_update"_i18n);
    simulateUpdateCell->registerClickAction([](brls::View*) {
        akira::UpdateFlow::simulate();
        return true;
    });
}
