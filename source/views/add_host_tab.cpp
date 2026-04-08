#include "views/add_host_tab.hpp"
#include "core/host.hpp"

#include <borealis/core/i18n.hpp>
using namespace brls::literals;

AddHostTab::AddHostTab() {
    this->inflateFromXMLRes("xml/tabs/add_host.xml");

    settings = SettingsManager::getInstance();

    hostNameInput->init(
        "akira/add_host/console_name"_i18n,
        "",
        [](std::string text) {},
        "akira/add_host/console_name_placeholder"_i18n,
        "akira/add_host/console_name_hint"_i18n
    );

    hostAddrInput->init(
        "akira/add_host/ip_address"_i18n,
        "",
        [](std::string text) {},
        "akira/add_host/ip_placeholder"_i18n,
        "akira/add_host/ip_hint"_i18n
    );

    std::vector<std::string> targetOptions = {"PS4", "PS5"};
    targetSelector->init(
        "akira/add_host/console_type"_i18n,
        targetOptions,
        1,  // Default to PS5
        [](int selected) {},
        [](int selected) {}
    );

    saveBtn->registerClickAction([this](brls::View* view) {
        onSaveClicked();
        return true;
    });
}

brls::View* AddHostTab::create() {
    return new AddHostTab();
}

void AddHostTab::onSaveClicked() {
    std::string name = hostNameInput->getValue();
    std::string addr = hostAddrInput->getValue();
    int targetIndex = targetSelector->getSelection();

    if (name.empty()) {
        statusLabel->setText("akira/add_host/error_no_name"_i18n);
        statusLabel->setTextColor(nvgRGBA(255, 100, 100, 255));
        return;
    }

    if (addr.empty()) {
        statusLabel->setText("akira/add_host/error_no_ip"_i18n);
        statusLabel->setTextColor(nvgRGBA(255, 100, 100, 255));
        return;
    }

    if (!SettingsManager::isValidHostAddress(addr)) {
        statusLabel->setText("akira/add_host/error_invalid_ip"_i18n);
        statusLabel->setTextColor(nvgRGBA(255, 100, 100, 255));
        return;
    }

    auto* hostsMap = settings->getHostsMap();
    if (hostsMap->find(name) != hostsMap->end()) {
        statusLabel->setText("akira/add_host/error_duplicate_name"_i18n);
        statusLabel->setTextColor(nvgRGBA(255, 100, 100, 255));
        return;
    }

    Host* host = settings->getOrCreateHost(name);
    host->setHostType(HostType::Manual);
    settings->setHostAddr(host, addr);
    settings->setDiscovered(host, true);

    ChiakiTarget target = targetIndex == 0 ? CHIAKI_TARGET_PS4_10 : CHIAKI_TARGET_PS5_1;
    settings->setChiakiTarget(host, target);

    settings->writeFile();

    statusLabel->setText("akira/add_host/success"_i18n);
    statusLabel->setTextColor(nvgRGBA(100, 200, 100, 255));

    brls::Logger::info("Added host: {} at {}", name, addr);

    hostNameInput->setValue("");
    hostAddrInput->setValue("");
}
