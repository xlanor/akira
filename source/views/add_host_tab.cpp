#include "views/add_host_tab.hpp"
#include "core/host.hpp"

AddHostTab::AddHostTab() {
    this->inflateFromXMLRes("xml/tabs/add_host.xml");

    settings = SettingsManager::getInstance();

    hostNameInput->init(
        "Console Name",
        "",
        [](std::string text) {},
        "e.g., Living Room PS5",
        "Name to identify this console"
    );

    hostAddrInput->init(
        "IP Address",
        "",
        [](std::string text) {},
        "e.g., 192.168.1.100",
        "PlayStation's IP address"
    );

    std::vector<std::string> targetOptions = {"PS4", "PS5"};
    targetSelector->init(
        "Console Type",
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
        statusLabel->setText("Please enter a console name");
        statusLabel->setTextColor(nvgRGBA(255, 100, 100, 255));
        return;
    }

    if (addr.empty()) {
        statusLabel->setText("Please enter an IP address");
        statusLabel->setTextColor(nvgRGBA(255, 100, 100, 255));
        return;
    }

    if (!SettingsManager::isValidHostAddress(addr)) {
        statusLabel->setText("Invalid IP address or hostname");
        statusLabel->setTextColor(nvgRGBA(255, 100, 100, 255));
        return;
    }

    auto* hostsMap = settings->getHostsMap();
    if (hostsMap->find(name) != hostsMap->end()) {
        statusLabel->setText("A console with this name already exists");
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

    statusLabel->setText("Console added successfully!");
    statusLabel->setTextColor(nvgRGBA(100, 200, 100, 255));

    brls::Logger::info("Added host: {} at {}", name, addr);

    hostNameInput->setValue("");
    hostAddrInput->setValue("");
}
