#include "views/host_list_tab.hpp"
#include "views/stream_view.hpp"
#include "views/connection_view.hpp"
#include "views/enter_pin_view.hpp"
#include "views/host_settings_view.hpp"
#include "core/host.hpp"
#include "stream/session.hpp"
#include "util/shared_view_holder.hpp"

#include <borealis/core/i18n.hpp>
using namespace brls::literals;

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
        if (host->isRemote() && displayName.length() > 9 && displayName.substr(displayName.length() - 9) == " (Remote)") {
            displayName = displayName.substr(0, displayName.length() - 9);
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

        auto* remoteBadgeLabel = new brls::Label();
        remoteBadgeLabel->setText("akira/hosts/remote"_i18n);
        remoteBadgeLabel->setFontSize(11);
        remoteBadgeLabel->setTextColor(nvgRGBA(255, 255, 255, 255));
        remoteBadge->addView(remoteBadgeLabel);

        nameRow->addView(remoteBadge);

        autoBadge = new brls::Box();
        autoBadge->setBackgroundColor(nvgRGBA(6, 182, 212, 255));
        autoBadge->setCornerRadius(4);
        autoBadge->setPaddingTop(2);
        autoBadge->setPaddingBottom(2);
        autoBadge->setPaddingLeft(6);
        autoBadge->setPaddingRight(6);
        autoBadge->setMarginLeft(8);
        autoBadge->setVisibility(host->isAuto() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);

        auto* autoBadgeLabel = new brls::Label();
        autoBadgeLabel->setText("akira/hosts/auto"_i18n);
        autoBadgeLabel->setFontSize(11);
        autoBadgeLabel->setTextColor(nvgRGBA(255, 255, 255, 255));
        autoBadge->addView(autoBadgeLabel);

        nameRow->addView(autoBadge);

        manualBadge = new brls::Box();
        manualBadge->setBackgroundColor(nvgRGBA(168, 85, 247, 255));
        manualBadge->setCornerRadius(4);
        manualBadge->setPaddingTop(2);
        manualBadge->setPaddingBottom(2);
        manualBadge->setPaddingLeft(6);
        manualBadge->setPaddingRight(6);
        manualBadge->setMarginLeft(8);
        manualBadge->setVisibility(host->isManual() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);

        auto* manualBadgeLabel = new brls::Label();
        manualBadgeLabel->setText("akira/hosts/manual"_i18n);
        manualBadgeLabel->setFontSize(11);
        manualBadgeLabel->setTextColor(nvgRGBA(255, 255, 255, 255));
        manualBadge->addView(manualBadgeLabel);

        nameRow->addView(manualBadge);

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

        createAccountButton();
        createConnectButton();
        createWakeButton();
        createRegisterButton();
        createLinkButton();
        createSettingsButton();
        createDeleteButton();

        this->addView(buttonBox);

        updateState();
    }

    void updateState() {
        std::string status = host->getTargetString() + " - " + host->getStateString();
        if (host->isRegistered()) {
            status += " (" + "akira/common/registered"_i18n + ")";
        }
        statusLabel->setText(status);

        remoteBadge->setVisibility(host->isRemote() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);

        autoBadge->setVisibility(host->isAuto() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        manualBadge->setVisibility(host->isManual() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);

        if (host->isRemote()) {
            std::string text = "akira/hosts/psn_remote_play"_i18n;
            std::string acctId = host->getRemoteAccountId();
            if (!acctId.empty()) {
                Account* a = SettingsManager::getInstance()->findAccount(acctId);
                std::string label = (a && !a->onlineId.empty()) ? a->onlineId : acctId;
                text += "  ·  " + brls::getStr("akira/hosts/account_label", label);
            }
            addrLabel->setText(text);
        } else {
            addrLabel->setText(host->getHostAddr());
        }

        int usableCount = 0;
        bool activeUsable = false;
        auto& profiles = host->getProfiles();
        for (size_t i = 0; i < profiles.size(); i++) {
            if (!profileUsable(profiles[i])) continue;
            usableCount++;
            if ((int)i == host->getActiveProfileIndex()) activeUsable = true;
        }
        bool accountsAvailable = !SettingsManager::getInstance()->getAccounts().empty();
        bool connectable = host->isRemote() ? host->hasRpKey() : activeUsable;

        bool showLink = host->isRemote() && host->needsLink();
        bool showConnect = connectable && !host->isStandby() && !host->needsLink();
        bool showWake = host->isStandby() && connectable && !host->isRemote();
        bool showRegister = !host->isRemote() && usableCount == 0 && accountsAvailable && !host->needsLink();
        bool showSettings = (host->isRemote() ? host->hasRpKey() : usableCount >= 1) && !host->needsLink();
        bool showDelete = host->isInConfig() && !host->needsLink();
        bool showAccount = usableCount >= 1 && !host->isRemote() && !host->needsLink();

        if (showAccount) {
            std::string accountLabel;
            int activeIdx = host->getActiveProfileIndex();
            if (activeIdx >= 0 && activeIdx < (int)profiles.size() && profileUsable(profiles[activeIdx])) {
                accountLabel = profiles[activeIdx].psnOnlineId.empty()
                    ? profiles[activeIdx].psnAccountId
                    : profiles[activeIdx].psnOnlineId;
            } else {
                accountLabel = "akira/hosts/select_account"_i18n;
            }
            if (accountLabel.empty()) accountLabel = "akira/account/unnamed"_i18n;
            accountBtn->setText(brls::getStr("akira/hosts/account_label", accountLabel));
        }

        linkBtn->setVisibility(showLink ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        connectBtn->setVisibility(showConnect ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        wakeBtn->setVisibility(showWake ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        registerBtn->setVisibility(showRegister ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        settingsBtn->setVisibility(showSettings ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        deleteBtn->setVisibility(showDelete ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        accountBtn->setVisibility(showAccount ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }

    Host* getHost() { return host; }
    std::string getHostName() const { return hostName; }

private:
    Host* host;
    std::string hostName;
    brls::Label* nameLabel;
    brls::Box* remoteBadge;
    brls::Box* autoBadge;
    brls::Box* manualBadge;
    brls::Label* addrLabel;
    brls::Label* statusLabel;
    brls::Box* buttonBox;
    brls::Button* connectBtn;
    brls::Button* wakeBtn;
    brls::Button* registerBtn;
    brls::Button* linkBtn;
    brls::Button* settingsBtn;
    brls::Button* deleteBtn;
    brls::Button* accountBtn;

    void createConnectButton() {
        connectBtn = new brls::Button();
        connectBtn->setText("akira/hosts/connect"_i18n);
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
                auto* dialog = new brls::Dialog("akira/hosts/psn_account_id_required"_i18n);
                dialog->addButton("akira/common/ok"_i18n, [dialog]() {
                    dialog->close();
                });
                dialog->open();
                return true;
            }

            if (!needsAccountId && onlineId.empty()) {
                auto* dialog = new brls::Dialog("akira/hosts/psn_online_id_required"_i18n);
                dialog->addButton("akira/common/ok"_i18n, [dialog]() {
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
        wakeBtn->setText("akira/hosts/wake"_i18n);
        wakeBtn->setStyle(&BUTTONSTYLE_ORANGE);
        wakeBtn->setShrink(0);
        wakeBtn->setMarginRight(10);
        wakeBtn->setBackgroundColor(nvgRGBA(255, 203, 92, 255));

        wakeBtn->registerClickAction([this](brls::View* view) {
            brls::Logger::info("Wake {}", host->getHostName());

            int result = host->wakeup();
            if (result == 0) {
                brls::Application::notify(brls::getStr("akira/hosts/wake_sent", host->getHostName()));
            } else {
                brls::Application::notify(brls::getStr("akira/hosts/wake_failed", host->getHostName()));
            }

            return true;
        });
        buttonBox->addView(wakeBtn);
    }

    static bool profileUsable(const HostProfile& p) {
        if (!p.hasRpKey) return false;
        if (p.psnAccountId.empty()) return true;
        return SettingsManager::getInstance()->findAccount(p.psnAccountId) != nullptr;
    }

    void createAccountButton() {
        accountBtn = new brls::Button();
        accountBtn->setStyle(&brls::BUTTONSTYLE_BORDERED);
        accountBtn->setShrink(0);
        accountBtn->setMarginRight(10);
        accountBtn->registerClickAction([this](brls::View* view) {
            showAccountMenu(host);
            return true;
        });
        buttonBox->addView(accountBtn);
    }

    void createRegisterButton() {
        registerBtn = new brls::Button();
        registerBtn->setText("akira/hosts/add_account"_i18n);
        registerBtn->setStyle(&brls::BUTTONSTYLE_BORDERED);
        registerBtn->setShrink(0);
        registerBtn->setMarginRight(10);
        registerBtn->registerClickAction([this](brls::View* view) {
            if (HostListTab::isRegistering) {
                return true;
            }
            startAddAccount(host);
            return true;
        });
        buttonBox->addView(registerBtn);
    }

    static void beginPinRegistration(Host* host) {
        if (HostListTab::isRegistering) {
            return;
        }

        HostListTab::isRegistering = true;
        Host* hostPtr = host;

        host->setOnRegistSuccess([hostPtr]() {
            brls::sync([hostPtr]() {
                HostListTab::isRegistering = false;
                brls::Application::notify("akira/hosts/registration_success"_i18n);
                SettingsManager::getInstance()->writeFile();
                if (HostListTab::currentInstance) {
                    HostListTab::currentInstance->updateHostItem(hostPtr);
                }
            });
        });

        host->setOnRegistFailed([]() {
            brls::sync([]() {
                HostListTab::isRegistering = false;
                brls::Application::notify("akira/hosts/registration_failed"_i18n);
            });
        });

        host->setOnRegistCanceled([]() {
            brls::sync([]() {
                HostListTab::isRegistering = false;
                brls::Application::notify("akira/hosts/registration_canceled"_i18n);
            });
        });

        auto* pinView = new EnterPinView(host, PinViewType::Registration);
        pinView->setOnCancel([]() {
            HostListTab::isRegistering = false;
        });
        pinView->setOnPinEntered([hostPtr](const std::string& pin) {
            int pinValue = std::atoi(pin.c_str());
            int result = hostPtr->registerHost(pinValue);
            if (result != HOST_REGISTER_OK) {
                std::string errorMsg;
                switch (result) {
                    case HOST_REGISTER_ERROR_SETTING_PSNACCOUNTID:
                        errorMsg = "akira/hosts/error_psn_account_id"_i18n;
                        break;
                    case HOST_REGISTER_ERROR_SETTING_PSNONLINEID:
                        errorMsg = "akira/hosts/error_psn_online_id"_i18n;
                        break;
                    case HOST_REGISTER_ERROR_UNDEFINED_TARGET:
                        errorMsg = "akira/hosts/error_console_type"_i18n;
                        break;
                    default:
                        errorMsg = "akira/hosts/error_registration_failed"_i18n;
                        break;
                }
                brls::Logger::error("Registration failed: {}", errorMsg);
                brls::Application::notify(errorMsg);
            }
        });

        brls::Application::pushActivity(new brls::Activity(pinView));
    }

    static void startRegistration(Host* host, const Account& account) {
        auto* settings = SettingsManager::getInstance();
        settings->setPsnOnlineId(host, account.onlineId);
        settings->setPsnAccountId(host, account.accountId);
        beginPinRegistration(host);
    }

    static void startAddAccount(Host* host) {
        auto* settings = SettingsManager::getInstance();
        auto& accounts = settings->getAccounts();

        if (accounts.empty()) {
            brls::Application::notify("akira/hosts/no_accounts_configured"_i18n);
            return;
        }

        std::vector<Account> candidates;
        for (const auto& account : accounts) {
            int idx = host->findProfileByAccount(account.accountId);
            bool alreadyRegistered = (idx >= 0 && host->getProfiles()[idx].hasRpKey);
            if (!alreadyRegistered) {
                candidates.push_back(account);
            }
        }

        if (candidates.empty()) {
            brls::Application::notify("akira/hosts/all_accounts_registered"_i18n);
            return;
        }

        if (candidates.size() == 1) {
            startRegistration(host, candidates[0]);
            return;
        }

        std::vector<std::string> labels;
        for (const auto& account : candidates) {
            std::string label = account.label();
            if (label.empty()) label = "akira/account/unnamed"_i18n;
            labels.push_back(label);
        }

        auto* dropdown = new brls::Dropdown(
            "akira/hosts/select_account_register"_i18n,
            labels,
            [host, candidates](int selected) {
                if (selected < 0 || selected >= (int)candidates.size()) return;
                startRegistration(host, candidates[selected]);
            }
        );
        brls::Application::pushActivity(new brls::Activity(dropdown));
    }

    static void showAccountMenu(Host* host) {
        std::vector<std::string> options;
        std::vector<int> mapping;

        auto& profiles = host->getProfiles();
        for (size_t i = 0; i < profiles.size(); i++) {
            if (!profileUsable(profiles[i])) continue;
            std::string label = profiles[i].psnOnlineId.empty()
                ? profiles[i].psnAccountId
                : profiles[i].psnOnlineId;
            if (label.empty()) label = "akira/account/unnamed"_i18n;
            if ((int)i == host->getActiveProfileIndex()) label += " *";
            options.push_back(label);
            mapping.push_back((int)i);
        }

        options.push_back("akira/hosts/register_another"_i18n);
        mapping.push_back(-1);
        options.push_back("akira/hosts/remove_account"_i18n);
        mapping.push_back(-2);

        auto* dropdown = new brls::Dropdown(
            "akira/hosts/select_account"_i18n,
            options,
            [host, mapping](int selected) {
                if (selected < 0 || selected >= (int)mapping.size()) return;
                int idx = mapping[selected];
                if (idx >= 0) {
                    host->setActiveProfile(idx);
                    SettingsManager::getInstance()->writeFile();
                    if (HostListTab::currentInstance) {
                        HostListTab::currentInstance->updateHostItem(host);
                    }
                } else if (idx == -1) {
                    startAddAccount(host);
                } else {
                    showRemoveAccountMenu(host);
                }
            }
        );
        brls::Application::pushActivity(new brls::Activity(dropdown));
    }

    static void showRemoveAccountMenu(Host* host) {
        std::vector<std::string> labels;
        std::vector<std::string> accountIds;
        auto& profiles = host->getProfiles();
        for (size_t i = 0; i < profiles.size(); i++) {
            if (!profileUsable(profiles[i])) continue;
            std::string label = profiles[i].psnOnlineId.empty()
                ? profiles[i].psnAccountId
                : profiles[i].psnOnlineId;
            if (label.empty()) label = "akira/account/unnamed"_i18n;
            labels.push_back(label);
            accountIds.push_back(profiles[i].psnAccountId);
        }
        if (labels.empty()) return;

        auto* dropdown = new brls::Dropdown(
            "akira/hosts/remove_account"_i18n,
            labels,
            [host, accountIds, labels](int selected) {
                if (selected < 0 || selected >= (int)accountIds.size()) return;
                std::string accountId = accountIds[selected];
                std::string label = labels[selected];
                auto* dialog = new brls::Dialog(brls::getStr("akira/hosts/remove_account_confirm", label));
                dialog->addButton("akira/common/cancel"_i18n, []() {});
                dialog->addButton("akira/common/delete"_i18n, [host, accountId]() {
                    int idx = host->findProfileByAccount(accountId);
                    if (idx >= 0) host->removeProfile(idx);
                    SettingsManager::getInstance()->writeFile();
                    brls::Application::notify("akira/hosts/account_removed"_i18n);
                    if (HostListTab::currentInstance) {
                        HostListTab::currentInstance->updateHostItem(host);
                    }
                });
                dialog->open();
            }
        );
        brls::Application::pushActivity(new brls::Activity(dropdown));
    }

    void createLinkButton() {
        linkBtn = new brls::Button();
        linkBtn->setText("akira/hosts/link"_i18n);
        linkBtn->setStyle(&BUTTONSTYLE_BLUE);
        linkBtn->setShrink(0);
        linkBtn->setMarginRight(10);
        linkBtn->setBackgroundColor(nvgRGBA(59, 130, 246, 255));
        linkBtn->registerClickAction([this](brls::View* view) {
            brls::Logger::info("Link button clicked for {}", host->getHostName());

            auto* settings = SettingsManager::getInstance();
            auto* hostsMap = settings->getHostsMap();

            std::vector<std::string> hostNames;
            std::vector<Host*> hostPtrs;
            for (auto& [name, h] : *hostsMap) {
                if (h && h->hasRpKey() && !h->isRemote()) {
                    hostNames.push_back(h->getHostName());
                    hostPtrs.push_back(h.get());
                }
            }

            if (hostNames.empty()) {
                brls::Application::notify("akira/hosts/no_registered_hosts"_i18n);
                return true;
            }

            Host* remoteHost = host;
            auto* dropdown = new brls::Dropdown(
                "akira/hosts/select_host_to_link"_i18n,
                hostNames,
                [remoteHost, hostPtrs](int selected) {
                    if (selected < 0 || selected >= (int)hostPtrs.size()) return;

                    Host* localHost = hostPtrs[selected];
                    auto* settings = SettingsManager::getInstance();

                    std::string remoteName = remoteHost->getHostName();
                    if (remoteName.length() > 9 && remoteName.substr(remoteName.length() - 9) == " (Remote)") {
                        remoteName = remoteName.substr(0, remoteName.length() - 9);
                    }

                    settings->setHostNickname(localHost, remoteName);
                    remoteHost->copyRegistrationFrom(localHost, remoteHost->getRemoteAccountId());
                    remoteHost->setNeedsLink(false);
                    localHost->setRemoteDuid(remoteHost->getRemoteDuid());
                    settings->writeFile();

                    brls::Application::notify(brls::getStr("akira/hosts/linked_to", remoteName));

                    brls::sync([]() {
                        if (HostListTab::currentInstance) {
                            HostListTab::currentInstance->syncHostList();
                            if (HostListTab::currentInstance->findRemoteBtn) {
                                brls::Application::giveFocus(HostListTab::currentInstance->findRemoteBtn);
                            }
                        }
                    });
                }
            );
            brls::Application::pushActivity(new brls::Activity(dropdown));

            return true;
        });
        buttonBox->addView(linkBtn);
    }

    void createSettingsButton() {
        settingsBtn = new brls::Button();
        settingsBtn->setText("akira/hosts/settings"_i18n);
        settingsBtn->setStyle(&brls::BUTTONSTYLE_BORDERED);
        settingsBtn->setShrink(0);
        settingsBtn->setMarginRight(10);
        settingsBtn->registerClickAction([this](brls::View* view) {
            brls::Logger::info("Settings button clicked for {}", host->getHostName());

            auto* settingsView = new HostSettingsView(host);
            settingsView->setOnSaved([]() {
                brls::sync([]() {
                    if (HostListTab::currentInstance) {
                        HostListTab::currentInstance->syncHostList();
                        if (HostListTab::currentInstance->findRemoteBtn) {
                            brls::Application::giveFocus(HostListTab::currentInstance->findRemoteBtn);
                        }
                    }
                });
            });
            brls::Application::pushActivity(new brls::Activity(settingsView));

            return true;
        });
        buttonBox->addView(settingsBtn);
    }

    void createDeleteButton() {
        deleteBtn = new brls::Button();
        deleteBtn->setText("akira/hosts/delete"_i18n);
        deleteBtn->setStyle(&brls::BUTTONSTYLE_BORDERED);
        deleteBtn->setShrink(0);
        std::string hostName = host->getHostName();
        Host* hostPtr = host;
        deleteBtn->registerClickAction([hostName, hostPtr](brls::View* view) {
            brls::Logger::info("Delete button clicked for {}", hostName);

            auto* dialog = new brls::Dialog(brls::getStr("akira/hosts/delete_confirm", hostName));
            dialog->addButton("akira/common/cancel"_i18n, []() {});
            dialog->addButton("akira/common/delete"_i18n, [hostPtr]() {
                auto* settings = SettingsManager::getInstance();
                settings->removeHost(hostPtr);
                settings->writeFile();
                brls::Application::notify("akira/hosts/host_deleted"_i18n);
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
        if (!host) {
            brls::Logger::warning("onHostDiscovered: host is null");
            return;
        }
        brls::Logger::info("onHostDiscovered: isActive={}, currentInstance={}",
            HostListTab::isActive ? "true" : "false",
            HostListTab::currentInstance ? "valid" : "null");
        if (!HostListTab::isActive) {
            brls::Logger::info("onHostDiscovered: isActive=false, skipping {}", host->getHostName());
            return;
        }
        if (!HostListTab::currentInstance) {
            brls::Logger::warning("onHostDiscovered: currentInstance is null");
            return;
        }
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
        if (!settings->hasAnyRemoteAccount()) {
            brls::Application::notify("akira/hosts/no_psn_token"_i18n);
            return true;
        }

        auto onComplete = [this]() {
            syncHostList();
            if (findRemoteBtn) {
                brls::Application::giveFocus(findRemoteBtn);
            }
        };

        brls::Application::notify("akira/hosts/finding_remote"_i18n);
        discovery->refreshRemoteDevices(onComplete);

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
            auto* item = new HostItemView(host.get());
            hostItems[host.get()] = item;
            hostContainer->addView(item);
        }
    }

    bool hasHosts = !hostItems.empty();
    if (emptyMessage) {
        emptyMessage->setVisibility(hasHosts ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    }

    if (hasHosts) {
        brls::Application::giveFocus(hostContainer);
    } else if (findRemoteBtn) {
        brls::Application::giveFocus(findRemoteBtn);
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
