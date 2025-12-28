#include "views/host_list_tab.hpp"
#include "views/stream_view.hpp"
#include "views/connection_view.hpp"
#include "views/enter_pin_view.hpp"
#include "views/host_settings_view.hpp"
#include "core/host.hpp"
#include "core/io.hpp"
#include "util/shared_view_holder.hpp"

#include <ctime>

static const brls::ButtonStyle BUTTONSTYLE_BLUE = {
    .shadowType              = brls::ShadowType::GENERIC,
    .hideHighlightBackground = true,
    .highlightPadding = "",
    .borderThickness  = "",
    .enabledBackgroundColor = "",
    .enabledLabelColor      = "brls/button/primary_enabled_text",
    .enabledBorderColor     = "",
    .disabledBackgroundColor = "",
    .disabledLabelColor      = "brls/button/primary_disabled_text",
    .disabledBorderColor     = "",
};

static const brls::ButtonStyle BUTTONSTYLE_ORANGE = {
    .shadowType              = brls::ShadowType::GENERIC,
    .hideHighlightBackground = true,
    .highlightPadding = "",
    .borderThickness  = "",
    .enabledBackgroundColor = "",
    .enabledLabelColor      = "brls/button/primary_enabled_text",
    .enabledBorderColor     = "",
    .disabledBackgroundColor = "",
    .disabledLabelColor      = "brls/button/primary_disabled_text",
    .disabledBorderColor     = "",
};

HostListTab* HostListTab::currentInstance = nullptr;
bool HostListTab::isConnecting = false;
bool HostListTab::isRegistering = false;
bool HostListTab::isActive = true;


class HostItemView : public brls::Box {
public:
    HostItemView(Host* host) : host(host), hostName(host->getHostName()) {
        this->setAxis(brls::Axis::ROW);
        this->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setPadding(15);
        this->setMarginBottom(10);
        this->setBackgroundColor(brls::Application::getTheme().getColor("color/card"));
        this->setCornerRadius(8);

        auto* infoBox = new brls::Box();
        infoBox->setAxis(brls::Axis::COLUMN);
        infoBox->setGrow(1);
        infoBox->setShrink(1);

        auto* nameRow = new brls::Box();
        nameRow->setAxis(brls::Axis::ROW);
        nameRow->setAlignItems(brls::AlignItems::CENTER);
        nameRow->setMarginBottom(5);

        nameLabel = new brls::Label();
        std::string displayName = host->getHostName();
        if (displayName.rfind("[Remote] ", 0) == 0) {
            displayName = displayName.substr(9);
        }
        nameLabel->setText(displayName);
        nameLabel->setFontSize(18);
        nameRow->addView(nameLabel);

        remoteBadge = new brls::Box();
        remoteBadge->setBackgroundColor(nvgRGBA(59, 130, 246, 255));
        remoteBadge->setCornerRadius(4);
        remoteBadge->setPaddingTop(2);
        remoteBadge->setPaddingBottom(2);
        remoteBadge->setPaddingLeft(6);
        remoteBadge->setPaddingRight(6);
        remoteBadge->setMarginLeft(8);
        remoteBadge->setVisibility(host->isRemote() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);

        auto* badgeLabel = new brls::Label();
        badgeLabel->setText("Remote");
        badgeLabel->setFontSize(11);
        badgeLabel->setTextColor(nvgRGBA(255, 255, 255, 255));
        remoteBadge->addView(badgeLabel);

        nameRow->addView(remoteBadge);

        infoBox->addView(nameRow);

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
        buttonBox->setShrink(0);

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

        remoteBadge->setVisibility(host->isRemote() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);

        if (host->isRemote()) {
            addrLabel->setText("PSN Remote Play");
        } else {
            addrLabel->setText(host->getHostAddr());
        }

        bool showConnect = host->hasRpKey() && !host->isStandby();
        bool showWake = host->isStandby() && host->hasRpKey() && !host->isRemote();
        bool showRegister = host->isDiscovered() && !host->hasRpKey() && !host->isRemote();
        bool showSettings = host->hasRpKey();
        bool showDelete = host->hasRpKey() || !host->isDiscovered();

        connectBtn->setVisibility(showConnect ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        wakeBtn->setVisibility(showWake ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        registerBtn->setVisibility(showRegister ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        settingsBtn->setVisibility(showSettings ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        deleteBtn->setVisibility(showDelete ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }

    Host* getHost() { return host; }
    std::string getHostName() const { return hostName; }

private:
    Host* host;
    std::string hostName;
    brls::Label* nameLabel;
    brls::Box* remoteBadge;
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
        connectBtn->setShrink(0);
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

            if (host->isRemote()) {
                auto connectionView = SharedViewHolder::holdNew<ConnectionView>(host);
                brls::Application::pushActivity(new brls::Activity(connectionView.get()));
                connectionView->setupAndStart();
            } else {
                auto streamView = SharedViewHolder::holdNew<StreamView>(host);
                streamView->setupCallbacks();
                brls::Application::pushActivity(new brls::Activity(streamView.get()));
                streamView->startStream();
            }

            HostListTab::isConnecting = false;

            return true;
        });
        buttonBox->addView(connectBtn);
    }

    void createWakeButton() {
        wakeBtn = new brls::Button();
        wakeBtn->setText("Wake");
        wakeBtn->setStyle(&BUTTONSTYLE_ORANGE);
        wakeBtn->setShrink(0);
        wakeBtn->setMarginRight(10);
        wakeBtn->setBackgroundColor(nvgRGBA(255, 203, 92, 255));

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
        registerBtn->setShrink(0);
        registerBtn->setMarginRight(10);
        registerBtn->registerClickAction([this](brls::View* view) {
            if (HostListTab::isRegistering) {
                return true;
            }

            brls::Logger::info("Register button clicked for {}", host->getHostName());

            auto* settings = SettingsManager::getInstance();
            bool isPS5 = host->isPS5();
            bool needsAccountId = isPS5 || host->getChiakiTarget() >= CHIAKI_TARGET_PS4_9;
            std::string accountId = settings->getPsnAccountId(host);
            std::string onlineId = settings->getPsnOnlineId(host);

            if (needsAccountId && accountId.empty()) {
                auto* dialog = new brls::Dialog("PSN Account ID Required\n\nPlease configure your PSN Account ID in Settings before registering.\n");
                dialog->addButton("OK", [dialog]() {
                    dialog->close();
                });
                dialog->open();
                return true;
            }

            if (!needsAccountId && onlineId.empty()) {
                auto* dialog = new brls::Dialog("PSN Online ID Required\n\nFor PS4 firmware < 9.0, please enter your PSN username in Settings before registering.");
                dialog->addButton("OK", [dialog]() {
                    dialog->close();
                });
                dialog->open();
                return true;
            }

            HostListTab::isRegistering = true;

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
        settingsBtn->setShrink(0);
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
        deleteBtn->setShrink(0);
        std::string hostName = host->getHostName();
        deleteBtn->registerClickAction([hostName](brls::View* view) {
            brls::Logger::info("Delete button clicked for {}", hostName);

            auto* dialog = new brls::Dialog("Delete \"" + hostName + "\"?\n\nThis will remove the host and its registration data.");
            dialog->addButton("Cancel", []() {});
            dialog->addButton("Delete", [hostName]() {
                auto* settings = SettingsManager::getInstance();
                settings->removeHost(hostName);
                settings->writeFile();
                brls::Application::notify("Host deleted");
                if (HostListTab::currentInstance) {
                    // If this was the last host, give focus to Find Remote button first
                    // to avoid dangling focus pointer when host list becomes empty
                    if (HostListTab::currentInstance->hostItems.size() <= 1 &&
                        HostListTab::currentInstance->findRemoteBtn) {
                        brls::Application::giveFocus(HostListTab::currentInstance->findRemoteBtn);
                    }
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

    initFindRemoteButton();
    syncHostList();
}

void HostListTab::initFindRemoteButton() {
    findRemoteBtn->setStyle(&BUTTONSTYLE_BLUE);
    findRemoteBtn->setBackgroundColor(nvgRGBA(92, 157, 255, 255));

    findRemoteBtn->registerClickAction([this](brls::View* view) {
        std::string refreshToken = settings->getPsnRefreshToken();

        if (refreshToken.empty()) {
            brls::Application::notify("No PSN token set");
            return true;
        }

        int64_t expiresAt = settings->getPsnTokenExpiresAt();
        int64_t now = std::time(nullptr);

        if (expiresAt > 0 && now > expiresAt) {
            brls::Application::notify("Token expired, refreshing...");
            discovery->refreshPsnToken(
                [this]() {
                    brls::sync([this]() {
                        brls::Application::notify("Finding remote devices...");
                        discovery->refreshRemoteDevices();
                    });
                },
                [](const std::string& error) {
                    brls::sync([error]() {
                        brls::Application::notify("Token refresh failed: " + error);
                    });
                }
            );
        } else {
            brls::Application::notify("Finding remote devices...");
            discovery->refreshRemoteDevices();
        }

        return true;
    });
}

HostListTab::~HostListTab() {
    if (currentInstance == this) {
        currentInstance = nullptr;
    }

    hostItems.clear();

    discovery->setOnHostDiscovered(nullptr);
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
    // borealis really hates it when you remove a view.
    // just rebuild the damn thing.
    brls::Application::giveFocus(this);

    hostContainer->clearViews();
    hostItems.clear();

    auto* hostsMap = settings->getHostsMap();
    if (hostsMap) {
        for (auto& [name, host] : *hostsMap) {
            if (!host) {
                brls::Logger::error("Null host in hosts map for key: {}", name);
                continue;
            }
            auto* item = new HostItemView(host);
            hostItems[host] = item;
            hostContainer->addView(item);
        }
    }

    bool hasHosts = !hostItems.empty();
    if (emptyMessage) {
        emptyMessage->setVisibility(hasHosts ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    }

    brls::Application::giveFocus(hostContainer);

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
