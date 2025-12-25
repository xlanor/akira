#include "views/host_list_tab.hpp"
#include "views/stream_view.hpp"
#include "views/enter_pin_view.hpp"
#include "views/host_settings_view.hpp"
#include "core/host.hpp"
#include "core/io.hpp"

HostListTab* HostListTab::currentInstance = nullptr;
bool HostListTab::isConnecting = false;
bool HostListTab::isRegistering = false;
bool HostListTab::isActive = true;


class HostItemView : public brls::Box {
public:
    HostItemView(Host* host) : host(host) {
        this->setAxis(brls::Axis::ROW);
        this->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setPadding(15);
        this->setMarginBottom(10);
        this->setBackgroundColor(nvgRGBA(40, 40, 40, 255));
        this->setCornerRadius(8);

        // Left side: Host info
        auto* infoBox = new brls::Box();
        infoBox->setAxis(brls::Axis::COLUMN);

        nameLabel = new brls::Label();
        nameLabel->setText(host->getHostName());
        nameLabel->setFontSize(18);
        nameLabel->setMarginBottom(5);
        infoBox->addView(nameLabel);

        addrLabel = new brls::Label();
        addrLabel->setText(host->getHostAddr());
        addrLabel->setFontSize(14);
        addrLabel->setTextColor(nvgRGBA(150, 150, 150, 255));
        infoBox->addView(addrLabel);

        statusLabel = new brls::Label();
        statusLabel->setFontSize(12);
        statusLabel->setTextColor(nvgRGBA(100, 150, 100, 255));
        infoBox->addView(statusLabel);

        this->addView(infoBox);

        buttonBox = new brls::Box();
        buttonBox->setAxis(brls::Axis::ROW);

        createConnectButton();
        createWakeButton();
        createRegisterButton();
        createSettingsButton();
        createDeleteButton();

        this->addView(buttonBox);

        updateState();
    }

    void updateState() {
        std::string status = host->getTargetString() + " - " + host->getStateString();
        if (host->isRegistered()) {
            status += " (Registered)";
        }
        statusLabel->setText(status);

        addrLabel->setText(host->getHostAddr());

        bool showConnect = host->isReady() && host->hasRpKey();
        bool showWake = host->isStandby() && host->hasRpKey();
        bool showRegister = host->isDiscovered() && !host->hasRpKey();
        bool showSettings = host->hasRpKey();
        bool showDelete = host->hasRpKey() || !host->isDiscovered();

        connectBtn->setVisibility(showConnect ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        wakeBtn->setVisibility(showWake ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        registerBtn->setVisibility(showRegister ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        settingsBtn->setVisibility(showSettings ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        deleteBtn->setVisibility(showDelete ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }

    Host* getHost() { return host; }

private:
    Host* host;
    brls::Label* nameLabel;
    brls::Label* addrLabel;
    brls::Label* statusLabel;
    brls::Box* buttonBox;
    brls::Button* connectBtn;
    brls::Button* wakeBtn;
    brls::Button* registerBtn;
    brls::Button* settingsBtn;
    brls::Button* deleteBtn;

    void createConnectButton() {
        connectBtn = new brls::Button();
        connectBtn->setText("Connect");
        connectBtn->setStyle(&brls::BUTTONSTYLE_PRIMARY);
        connectBtn->setMarginRight(10);
        connectBtn->registerClickAction([this](brls::View* view) {
            if (HostListTab::isConnecting) {
                return true;
            }

            brls::Logger::info("Connect to {}", host->getHostName());

            auto* settings = SettingsManager::getInstance();

            bool isPS5 = host->isPS5();
            bool needsAccountId = isPS5 || host->getChiakiTarget() >= CHIAKI_TARGET_PS4_9;
            std::string accountId = settings->getPsnAccountId(host);
            std::string onlineId = settings->getPsnOnlineId(host);

            if (needsAccountId && accountId.empty()) {
                auto* dialog = new brls::Dialog("PSN Account ID Required\n\nPlease configure your PSN Account ID in Settings before connecting.");
                dialog->addButton("OK", [dialog]() {
                    dialog->close();
                });
                dialog->open();
                return true;
            }

            if (!needsAccountId && onlineId.empty()) {
                auto* dialog = new brls::Dialog("PSN Online ID Required\n\nFor PS4 firmware < 7.0, please configure your PSN Online ID in Settings before connecting.");
                dialog->addButton("OK", [dialog]() {
                    dialog->close();
                });
                dialog->open();
                return true;
            }

            HostListTab::isConnecting = true;
            HostListTab::isActive = false;

            auto* streamView = new StreamView(host);
            brls::Application::pushActivity(new brls::Activity(streamView));
            streamView->startStream();

            HostListTab::isConnecting = false;

            return true;
        });
        buttonBox->addView(connectBtn);
    }

    void createWakeButton() {
        wakeBtn = new brls::Button();
        wakeBtn->setText("Wake");
        wakeBtn->setStyle(&brls::BUTTONSTYLE_BORDERED);
        wakeBtn->setMarginRight(10);
        wakeBtn->registerClickAction([this](brls::View* view) {
            brls::Logger::info("Wake {}", host->getHostName());

            int result = host->wakeup();
            if (result == 0) {
                brls::Application::notify("Wake-up signal sent to " + host->getHostName());
            } else {
                brls::Application::notify("Failed to wake " + host->getHostName());
            }

            return true;
        });
        buttonBox->addView(wakeBtn);
    }

    void createRegisterButton() {
        registerBtn = new brls::Button();
        registerBtn->setText("Register");
        registerBtn->setStyle(&brls::BUTTONSTYLE_BORDERED);
        registerBtn->setMarginRight(10);
        registerBtn->registerClickAction([this](brls::View* view) {
            if (HostListTab::isRegistering) {
                return true;
            }

            brls::Logger::info("Register button clicked for {}", host->getHostName());
            HostListTab::isRegistering = true;

            // Capture host pointer for callbacks
            Host* hostPtr = host;

            host->setOnRegistSuccess([hostPtr]() {
                brls::Logger::info("onRegistSuccess callback fired, queuing sync...");
                brls::sync([hostPtr]() {
                    brls::Logger::info("onRegistSuccess: inside brls::sync");
                    HostListTab::isRegistering = false;
                    brls::Application::notify("Registration successful!");
                    SettingsManager::getInstance()->writeFile();
                    if (HostListTab::currentInstance) {
                        HostListTab::currentInstance->updateHostItem(hostPtr);
                    }
                });
            });

            host->setOnRegistFailed([]() {
                brls::sync([]() {
                    HostListTab::isRegistering = false;
                    brls::Application::notify("Registration failed. Check PIN and try again.");
                });
            });

            host->setOnRegistCanceled([]() {
                brls::sync([]() {
                    HostListTab::isRegistering = false;
                    brls::Application::notify("Registration canceled");
                });
            });

            auto* pinView = new EnterPinView(host, PinViewType::Registration);
            pinView->setOnCancel([]() {
                brls::Logger::info("PIN entry cancelled");
            });
            pinView->setOnPinEntered([hostPtr](const std::string& pin) {
                brls::Logger::info("PIN entered, starting registration");
                int pinValue = std::atoi(pin.c_str());
                int result = hostPtr->registerHost(pinValue);
                if (result != HOST_REGISTER_OK) {
                    std::string errorMsg;
                    switch (result) {
                        case HOST_REGISTER_ERROR_SETTING_PSNACCOUNTID:
                            errorMsg = "Error: PSN Account ID is required. Configure in Settings.";
                            break;
                        case HOST_REGISTER_ERROR_SETTING_PSNONLINEID:
                            errorMsg = "Error: PSN Online ID is required for PS4 < 7.0.";
                            break;
                        case HOST_REGISTER_ERROR_UNDEFINED_TARGET:
                            errorMsg = "Error: Console type not recognized.";
                            break;
                        default:
                            errorMsg = "Error: Registration failed.";
                            break;
                    }
                    brls::Logger::error("Registration failed: {}", errorMsg);
                    brls::Application::notify(errorMsg);
                }
            });

            brls::Application::pushActivity(new brls::Activity(pinView));

            return true;
        });
        buttonBox->addView(registerBtn);
    }

    void createSettingsButton() {
        settingsBtn = new brls::Button();
        settingsBtn->setText("Settings");
        settingsBtn->setStyle(&brls::BUTTONSTYLE_BORDERED);
        settingsBtn->setMarginRight(10);
        settingsBtn->registerClickAction([this](brls::View* view) {
            brls::Logger::info("Settings button clicked for {}", host->getHostName());

            Host* hostPtr = host;
            auto* settingsView = new HostSettingsView(host);
            settingsView->setOnSaved([hostPtr]() {
                if (HostListTab::currentInstance) {
                    HostListTab::currentInstance->updateHostItem(hostPtr);
                }
            });
            brls::Application::pushActivity(new brls::Activity(settingsView));

            return true;
        });
        buttonBox->addView(settingsBtn);
    }

    void createDeleteButton() {
        deleteBtn = new brls::Button();
        deleteBtn->setText("Delete");
        deleteBtn->setStyle(&brls::BUTTONSTYLE_BORDERED);
        std::string hostName = host->getHostName();
        deleteBtn->registerClickAction([hostName](brls::View* view) {
            brls::Logger::info("Delete button clicked for {}", hostName);

            auto* dialog = new brls::Dialog("Delete \"" + hostName + "\"?\n\nThis will remove the host and its registration data.");
            dialog->addButton("Cancel", [dialog]() {
                dialog->close();
            });
            dialog->addButton("Delete", [hostName, dialog]() {
                auto* settings = SettingsManager::getInstance();
                settings->removeHost(hostName);
                settings->writeFile();
                brls::Application::notify("Host deleted");
                dialog->close();
                if (HostListTab::currentInstance) {
                    HostListTab::currentInstance->syncHostList();
                }
            });
            dialog->open();

            return true;
        });
        buttonBox->addView(deleteBtn);
    }
};


HostListTab::HostListTab() {
    currentInstance = this;
    this->inflateFromXMLRes("xml/tabs/host_list.xml");

    settings = SettingsManager::getInstance();
    discovery = DiscoveryManager::getInstance();

    discovery->setOnHostDiscovered([](Host* host) {
        if (!host || !HostListTab::isActive) return;
        if (!HostListTab::currentInstance) return;
        brls::Logger::info("Host discovered/updated: {}", host->getHostName());
        HostListTab::currentInstance->updateHostItem(host);
    });

    if (!discovery->isServiceEnabled()) {
        discovery->setServiceEnabled(true);
    }
    syncHostList();
}

HostListTab::~HostListTab() {
    if (currentInstance == this) {
        currentInstance = nullptr;
    }

    hostItems.clear();

    discovery->setOnHostDiscovered(nullptr);
    // Don't stop discovery
}

void HostListTab::willAppear(bool resetState) {
    Box::willAppear(resetState);
    isActive = true;
    brls::Logger::debug("HostListTab::willAppear - resuming discovery callbacks");
    syncHostList();  
}

void HostListTab::willDisappear(bool resetState) {
    Box::willDisappear(resetState);
    isActive = false;
    brls::Logger::debug("HostListTab::willDisappear - pausing discovery callbacks");
}

brls::View* HostListTab::create() {
    return new HostListTab();
}

void HostListTab::syncHostList() {
    brls::Logger::debug("Syncing host list...");

    if (!hostContainer) {
        brls::Logger::error("syncHostList: hostContainer is null");
        return;
    }

    auto* hostsMap = settings->getHostsMap();

    for (auto it = hostItems.begin(); it != hostItems.end(); ) {
        Host* host = it->first;
        std::string hostName = host->getHostName();

        bool hostExists = hostsMap && hostsMap->find(hostName) != hostsMap->end();

        if (!hostExists) {
            brls::Logger::debug("Removing item for deleted host: {}", hostName);
            hostContainer->removeView(it->second);
            it = hostItems.erase(it);
        } else {
            ++it;
        }
    }

    if (hostsMap) {
        for (auto& [name, host] : *hostsMap) {
            if (!host) {
                brls::Logger::error("Null host in hosts map for key: {}", name);
                continue;
            }

            auto it = hostItems.find(host);
            if (it == hostItems.end()) {
                brls::Logger::debug("Creating item for new host: {}", name);
                auto* item = new HostItemView(host);
                hostItems[host] = item;
                hostContainer->addView(item);
            } else {
                it->second->updateState();
            }
        }
    }

    bool hasHosts = !hostItems.empty();
    if (emptyMessage) {
        emptyMessage->setVisibility(hasHosts ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    }

    brls::Logger::debug("Host list sync complete, {} hosts", hostItems.size());
}

void HostListTab::updateHostItem(Host* host) {
    if (!host) return;

    auto it = hostItems.find(host);
    if (it != hostItems.end()) {
        it->second->updateState();
        brls::Logger::debug("Updated host item: {}", host->getHostName());
    } else {
        brls::Logger::debug("Host not found in items, syncing: {}", host->getHostName());
        syncHostList();
    }
}
