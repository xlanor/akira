#include "views/host_list_tab.hpp"
#include "ui/theme.hpp"
#include "ui/motion.hpp"
#include "views/stream_view.hpp"
#include "views/connection_view.hpp"
#include "views/enter_pin_view.hpp"
#include "core/host.hpp"
#include "core/trophy_manager.hpp"
#include "psn/auth.hpp"
#include "psn/models.hpp"
#include "stream/session.hpp"
#include "util/shared_view_holder.hpp"

#include <borealis/core/i18n.hpp>
using namespace brls::literals;

#include <ctime>
#include <algorithm>
#include <cctype>

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
bool HostListTab::isActive = false;


class HostItemView : public brls::Box {
public:
    HostItemView(Host* host) : host(host), hostName(host->getHostName()) {
        this->setAxis(brls::Axis::COLUMN);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setJustifyContent(brls::JustifyContent::FLEX_START);
        this->setWidth(174);
        this->setHeight(238);
        this->setPadding(14);
        this->setMarginRight(14);
        this->setMarginBottom(14);
        this->setBackgroundColor(brls::Application::getTheme().getColor("color/card"));
        this->setCornerRadius(14);
        this->setFocusable(true);

        consoleImg = new brls::Image();
        consoleImg->setImageFromRes(host->isPS5() ? "img/console/ps5.png" : "img/console/ps4.png");
        consoleImg->setScalingType(brls::ImageScalingType::FIT);
        consoleImg->setDimensions(146, 118);
        consoleImg->setMarginBottom(12);
        this->addView(consoleImg);

        nameLabel = new brls::Label();
        nameLabel->setFontSize(22);
        nameLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        nameLabel->setMarginBottom(6);
        nameLabel->setAutoAnimate(true);
        this->addView(nameLabel);

        typePill = new brls::Box();
        typePill->setAxis(brls::Axis::ROW);
        typePill->setCornerRadius(11);
        typePill->setPaddingLeft(11);
        typePill->setPaddingRight(11);
        typePill->setPaddingTop(2);
        typePill->setPaddingBottom(3);
        typePill->setMarginBottom(8);

        typeLabel = new brls::Label();
        typeLabel->setFontSize(16);
        typePill->addView(typeLabel);
        this->addView(typePill);

        statusPill = new brls::Box();
        statusPill->setAxis(brls::Axis::ROW);
        statusPill->setCornerRadius(11);
        statusPill->setPaddingLeft(11);
        statusPill->setPaddingRight(11);
        statusPill->setPaddingTop(2);
        statusPill->setPaddingBottom(3);

        statusLabel = new brls::Label();
        statusLabel->setFontSize(16);
        statusPill->addView(statusLabel);
        this->addView(statusPill);

        this->registerClickAction([this](brls::View*) {
            doPrimaryAction();
            return true;
        });
        this->registerAction("akira/hosts/options"_i18n, brls::ControllerButton::BUTTON_Y,
            [this](brls::View*) { openContextMenu(); return true; }, false);

        akira::ui::motion::liftOnFocus(this, focusAnim);

        updateState();
    }

    void playEntrance(int delayMs) {
        akira::ui::motion::fadeIn(this, entranceAnim, delayMs, 300);
    }

    void updateState() {
        std::string displayName = host->getHostName();
        if (host->isRemote() && displayName.length() > 9 && displayName.substr(displayName.length() - 9) == " (Remote)")
            displayName = displayName.substr(0, displayName.length() - 9);
        nameLabel->setText(displayName);

        std::string typeText;
        NVGcolor typeColor;
        if (host->isRemote()) {
            typeText = "akira/hosts/remote"_i18n;
            typeColor = akira::ui::active().accentStrong;
        } else if (host->isManual()) {
            typeText = "akira/hosts/manual"_i18n;
            typeColor = akira::ui::active().media;
        } else if (host->isAuto()) {
            typeText = "akira/hosts/auto"_i18n;
            typeColor = akira::ui::active().accent;
        } else {
            typeText = "akira/hosts/local"_i18n;
            typeColor = akira::ui::active().textMuted;
        }
        typeLabel->setText(typeText);
        typeLabel->setTextColor(typeColor);
        typePill->setBackgroundColor(akira::ui::withAlpha(typeColor, 0x2e));

        std::string pillText;
        NVGcolor fg = akira::ui::active().textMuted;
        bool dim = true;
        if (host->isRemote() && host->needsLink()) {
            pillText = "akira/hosts/link"_i18n;
            fg = akira::ui::active().accent;
        } else if (host->isDiscovered() && !host->hasRpKey() && !host->isRemote()) {
            pillText = "akira/hosts/register"_i18n;
            fg = akira::ui::active().accent;
        } else if (host->isStandby() && host->hasRpKey()) {
            pillText = "akira/hosts/standby"_i18n;
        } else if ((host->hasRpKey() && host->isReady()) || (host->isRemote() && host->hasRpKey())) {
            pillText = "akira/hosts/ready"_i18n;
            fg = akira::ui::active().success;
            dim = false;
        } else {
            pillText = host->getStateString();
        }
        statusLabel->setText(pillText);
        statusLabel->setTextColor(fg);
        statusPill->setBackgroundColor(akira::ui::withAlpha(fg, 0x2e));

        int wantDim = dim ? 1 : 0;
        if (wantDim != lastDim) {
            float target = dim ? 0.5f : 1.0f;
            if (lastDim < 0) {
                consoleImg->setAlpha(target);
                dimAnim.reset(target);
            } else {
                akira::ui::motion::animate(dimAnim, dimAnim.getValue(), target, 260,
                    brls::EasingFunction::quadraticOut,
                    [this](float v) { consoleImg->setAlpha(v); });
            }
            lastDim = wantDim;
        }
    }

    Host* getHost() { return host; }
    std::string getHostName() const { return hostName; }

    void doPrimaryAction() {
        if (host->isRemote() && host->needsLink()) doLink();
        else if (host->isDiscovered() && !host->hasRpKey() && !host->isRemote()) doRegister();
        else if (host->isStandby() && host->hasRpKey() && !host->isRemote()) doWake();
        else if (host->hasRpKey()) doConnect();
    }

    void openContextMenu() {
        std::vector<std::string> options;
        std::vector<int> ids;
        if (host->hasRpKey() && !host->isRemote()) {
            options.push_back("akira/host_settings/console_pin"_i18n);
            ids.push_back(0);
        }
        if (host->isRegistered()) {
            options.push_back("akira/hosts/delete"_i18n);
            ids.push_back(1);
        }
        if (options.empty()) return;
        auto* dropdown = new brls::Dropdown("akira/hosts/options"_i18n, options,
            [this, ids](int sel) {
                if (sel < 0 || sel >= static_cast<int>(ids.size())) return;
                if (ids[sel] == 0) doConsolePIN();
                else doDelete();
            });
        brls::Application::pushActivity(new brls::Activity(dropdown));
    }

private:
    Host* host;
    std::string hostName;
    brls::Image* consoleImg;
    brls::Label* nameLabel;
    brls::Box* typePill;
    brls::Label* typeLabel;
    brls::Box* statusPill;
    brls::Label* statusLabel;
    brls::Animatable entranceAnim{1.0f};
    brls::Animatable focusAnim{0.0f};
    brls::Animatable dimAnim{1.0f};
    int lastDim = -1;

    void doConnect() {
        HostListTab::connectToHost(host);
    }

    void doWake() {
            brls::Logger::info("Wake {}", host->getHostName());

            int result = host->wakeup();
            if (result == 0) {
                brls::Application::notify(brls::getStr("akira/hosts/wake_sent", host->getHostName()));
            } else {
                brls::Application::notify(brls::getStr("akira/hosts/wake_failed", host->getHostName()));
            }
    }

    void doRegister() {
            if (HostListTab::isRegistering) {
                return;
            }

            brls::Logger::info("Register button clicked for {}", host->getHostName());

            auto* settings = SettingsManager::getInstance();
            bool isPS5 = host->isPS5();
            bool needsAccountId = isPS5 || host->getChiakiTarget() >= CHIAKI_TARGET_PS4_9;
            std::string accountId = settings->getPsnAccountId(host);
            std::string onlineId = settings->getPsnOnlineId(host);

            if (needsAccountId && accountId.empty()) {
                auto* dialog = new brls::Dialog("akira/hosts/psn_account_id_required_register"_i18n);
                dialog->addButton("akira/common/ok"_i18n, [dialog]() {
                    dialog->close();
                });
                dialog->open();
                return;
            }

            if (!needsAccountId && onlineId.empty()) {
                auto* dialog = new brls::Dialog("akira/hosts/psn_online_id_required_register"_i18n);
                dialog->addButton("akira/common/ok"_i18n, [dialog]() {
                    dialog->close();
                });
                dialog->open();
                return;
            }

            HostListTab::isRegistering = true;

            Host* hostPtr = host;

            host->setOnRegistSuccess([hostPtr]() {
                brls::Logger::info("onRegistSuccess callback fired, queuing sync...");
                brls::sync([hostPtr]() {
                    brls::Logger::info("onRegistSuccess: inside brls::sync");
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

    void doLink() {
            brls::Logger::info("Link button clicked for {}", host->getHostName());

            auto* settings = SettingsManager::getInstance();
            auto* hostsMap = settings->getHostsMap();

            std::vector<std::string> hostNames;
            std::vector<Host*> hostPtrs;
            for (auto& [name, h] : *hostsMap) {
                if (h && h->hasRpKey() && !h->isRemote()) {
                    hostNames.push_back(name);
                    hostPtrs.push_back(h.get());
                }
            }

            if (hostNames.empty()) {
                brls::Application::notify("akira/hosts/no_registered_hosts"_i18n);
                return;
            }

            Host* remoteHost = host;
            auto* dropdown = new brls::Dropdown(
                "akira/hosts/select_host_to_link"_i18n,
                hostNames,
                [remoteHost, hostPtrs, hostNames](int selected) {
                    if (selected < 0 || selected >= (int)hostPtrs.size()) return;

                    Host* localHost = hostPtrs[selected];
                    std::string oldName = hostNames[selected];
                    auto* settings = SettingsManager::getInstance();

                    std::string remoteName = remoteHost->getHostName();
                    if (remoteName.length() > 9 && remoteName.substr(remoteName.length() - 9) == " (Remote)") {
                        remoteName = remoteName.substr(0, remoteName.length() - 9);
                    }

                    if (oldName != remoteName) {
                        settings->renameHost(oldName, remoteName);
                    }
                    remoteHost->copyRegistrationFrom(localHost);
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
    }

    void doConsolePIN() {
            std::string current = SettingsManager::getInstance()->getConsolePIN(host);
            brls::Application::getImeManager()->openForText(
                [this](std::string text) {
                    if (!text.empty() && (text.length() != 4 || !std::ranges::all_of(text, ::isdigit))) {
                        brls::Application::notify("akira/host_settings/console_pin_invalid"_i18n);
                        return;
                    }
                    SettingsManager::getInstance()->setConsolePIN(host, text);
                    SettingsManager::getInstance()->writeFile();
                    brls::Application::notify("akira/host_settings/console_pin_saved"_i18n);
                },
                "akira/host_settings/console_pin"_i18n, "akira/host_settings/console_pin_hint"_i18n, 4, current, 0);
    }

    void doDelete() {
            std::string hostName = host->getHostName();
            brls::Logger::info("Delete button clicked for {}", hostName);

            auto* dialog = new brls::Dialog(brls::getStr("akira/hosts/delete_confirm", hostName));
            dialog->addButton("akira/common/cancel"_i18n, []() {});
            dialog->addButton("akira/common/delete"_i18n, [hostName]() {
                auto* settings = SettingsManager::getInstance();
                settings->removeActiveProfileRegistration(hostName);
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
    }
};

static void openAddHostFlow() {
    auto* view = AddHostTab::create();
    view->registerAction("akira/common/back"_i18n, brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, false);
    auto* frame = new brls::AppletFrame(view);
    frame->setTitle("akira/tabs/add_host"_i18n);
    brls::Application::pushActivity(new brls::Activity(frame));
}

class AddHostCard : public brls::Box {
public:
    AddHostCard() {
        this->setAxis(brls::Axis::COLUMN);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setJustifyContent(brls::JustifyContent::CENTER);
        this->setWidth(174);
        this->setHeight(238);
        this->setPadding(14);
        this->setMarginRight(14);
        this->setMarginBottom(14);
        this->setBackgroundColor(brls::Application::getTheme().getColor("color/card"));
        this->setCornerRadius(14);
        this->setBorderColor(akira::ui::withAlpha(akira::ui::active().accent, 0x66));
        this->setBorderThickness(2);
        this->setFocusable(true);

        auto* plus = new brls::Label();
        plus->setText("+");
        plus->setFontSize(64);
        plus->setTextColor(akira::ui::active().accent);
        plus->setMarginBottom(6);
        this->addView(plus);

        auto* label = new brls::Label();
        label->setText("akira/add_host/add_console"_i18n);
        label->setFontSize(22);
        label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        label->setTextColor(akira::ui::active().textMuted);
        this->addView(label);

        this->registerClickAction([](brls::View*) {
            openAddHostFlow();
            return true;
        });
        akira::ui::motion::liftOnFocus(this, focusAnim);
    }

private:
    brls::Animatable focusAnim{0.0f};
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

    profileChip = new ProfileChipView();
    chipSlot->addView(profileChip);
    profileChip->registerClickAction([this](brls::View*) {
        const auto& profiles = settings->getProfiles();
        if (profiles.empty()) {
            brls::Application::notify("akira/hosts/no_profiles_add"_i18n);
            return true;
        }

        std::vector<std::string> names;
        std::vector<int64_t> ids;
        int selected = 0;
        for (size_t i = 0; i < profiles.size(); i++) {
            std::string lbl = profiles[i].label();
            if (lbl.empty())
                lbl = "akira/settings/unnamed_profile"_i18n;
            names.push_back(lbl);
            ids.push_back(profiles[i].id);
            if (profiles[i].id == settings->getActiveProfileId())
                selected = static_cast<int>(i);
        }

        auto* dropdown = new brls::Dropdown(
            "akira/hosts/switch_profile"_i18n, names,
            [ids](int sel) {
                if (sel < 0 || sel >= static_cast<int>(ids.size()))
                    return;
                auto* s = SettingsManager::getInstance();
                s->setActiveProfileId(ids[sel]);
                s->writeFile();
                TrophyManager::getInstance()->onActiveProfileChanged();
                brls::sync([]() {
                    if (HostListTab::currentInstance) {
                        HostListTab::currentInstance->syncHostList();
                        if (HostListTab::currentInstance->profileChip)
                            HostListTab::currentInstance->profileChip->refresh();
                        if (HostListTab::currentInstance->recentRail)
                            HostListTab::currentInstance->recentRail->refresh();
                    }
                });
            },
            selected);
        brls::Application::pushActivity(new brls::Activity(dropdown));
        return true;
    });

    recentRail = new RecentlyPlayedRail();
    recentRail->setResumeHandler([](const psn::PlayedGame& game) {
        HostListTab::resumeGame(game);
    });
    railSlot->addView(recentRail);

    syncHostList();
}

void HostListTab::connectToHost(Host* host) {
    if (!host || HostListTab::isConnecting)
        return;

    brls::Logger::info("Connect to {}", host->getHostName());

    auto* settings = SettingsManager::getInstance();
    bool isPS5 = host->isPS5();
    bool needsAccountId = isPS5 || host->getChiakiTarget() >= CHIAKI_TARGET_PS4_9;
    std::string accountId = settings->getPsnAccountId(host);
    std::string onlineId = settings->getPsnOnlineId(host);

    if (needsAccountId && accountId.empty()) {
        auto* dialog = new brls::Dialog("akira/hosts/psn_account_id_required"_i18n);
        dialog->addButton("akira/common/ok"_i18n, [dialog]() { dialog->close(); });
        dialog->open();
        return;
    }

    if (!needsAccountId && onlineId.empty()) {
        auto* dialog = new brls::Dialog("akira/hosts/psn_online_id_required"_i18n);
        dialog->addButton("akira/common/ok"_i18n, [dialog]() { dialog->close(); });
        dialog->open();
        return;
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
}

void HostListTab::resumeGame(const psn::PlayedGame& game) {
    if (HostListTab::isConnecting)
        return;

    bool wantPS5 = game.category.find("ps5") != std::string::npos;
    bool wantPS4 = game.category.find("ps4") != std::string::npos;

    auto* hosts = SettingsManager::getInstance()->getHostsMap();
    Host* localReady = nullptr;
    Host* remoteHost = nullptr;
    Host* localStandby = nullptr;
    Host* anyMatch = nullptr;

    if (hosts) {
        for (auto& entry : *hosts) {
            Host* h = entry.second.get();
            if (!h)
                continue;
            if (wantPS5 && !h->isPS5())
                continue;
            if (wantPS4 && h->isPS5())
                continue;
            if (!anyMatch)
                anyMatch = h;
            if (h->isRemote()) {
                if (!remoteHost)
                    remoteHost = h;
            } else if (h->isReady()) {
                if (!localReady)
                    localReady = h;
            } else if (h->isStandby()) {
                if (!localStandby)
                    localStandby = h;
            }
        }
    }

    if (localReady) {
        connectToHost(localReady);
        return;
    }
    if (remoteHost) {
        connectToHost(remoteHost);
        return;
    }
    if (localStandby) {
        int result = localStandby->wakeup();
        if (result == 0)
            brls::Application::notify(brls::getStr("akira/hosts/resume_waking", localStandby->getHostName()));
        else
            brls::Application::notify(brls::getStr("akira/hosts/wake_failed", localStandby->getHostName()));
        return;
    }
    if (anyMatch) {
        connectToHost(anyMatch);
        return;
    }
    brls::Application::notify("akira/hosts/resume_no_console"_i18n);
}

void HostListTab::initFindRemoteButton() {
    findRemoteBtn->setStyle(&BUTTONSTYLE_BLUE);

    findRemoteGate.attach(
        findRemoteBtn,
        akira::ui::active().accent,
        "akira/hosts/find_remote"_i18n,
        "akira/hosts/find_remote_busy"_i18n,
        "akira/hosts/find_remote_wait",
        []() { return DiscoveryManager::getInstance()->getRemoteRefreshStatus(); }
    );

    findRemoteBtn->registerClickAction([this](brls::View* view) {
        if (!findRemoteGate.isReady()) {
            return true;
        }

        std::string refreshToken = settings->getPsnRefreshToken();

        if (refreshToken.empty()) {
            brls::Application::notify("akira/hosts/no_psn_token"_i18n);
            return true;
        }

        auto onComplete = [](const psn::AuthResult& result) {
            if (!result.success) {
                brls::Application::notify(brls::getStr("akira/hosts/token_refresh_failed", result.message));
            }

            if (!HostListTab::currentInstance) {
                return;
            }
            HostListTab::currentInstance->syncHostList();
            if (HostListTab::currentInstance->findRemoteBtn) {
                brls::Application::giveFocus(HostListTab::currentInstance->findRemoteBtn);
            }
        };

        if (psn::Auth::instance().tokenValid()) {
            brls::Application::notify("akira/hosts/finding_remote"_i18n);
        } else {
            brls::Application::notify("akira/hosts/token_expired_refreshing"_i18n);
        }

        discovery->refreshRemoteDevices(onComplete, true);
        findRemoteGate.apply();

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
    findRemoteGate.start();
    if (profileChip) profileChip->refresh();
    if (recentRail) recentRail->refresh();
    syncHostList();
}

void HostListTab::willDisappear(bool resetState) {
    Box::willDisappear(resetState);
    isActive = false;
    brls::Logger::debug("HostListTab::willDisappear - pausing discovery callbacks");
    findRemoteGate.stop();
}

brls::View* HostListTab::create() {
    return new HostListTab();
}

void HostListTab::notifyActiveProfileChanged() {
    if (!currentInstance)
        return;
    currentInstance->syncHostList();
    if (currentInstance->profileChip)
        currentInstance->profileChip->refresh();
    if (currentInstance->recentRail)
        currentInstance->recentRail->refresh();
}

void HostListTab::syncHostList() {
    brls::Logger::debug("Syncing host list...");

    if (!hostContainer) {
        brls::Logger::error("syncHostList: hostContainer is null");
        return;
    }
    brls::View* focused = brls::Application::getCurrentFocus();
    bool childFocused = false;
    for (brls::View* v = focused; v; v = v->getParent()) {
        if (v == this) {
            childFocused = true;
            break;
        }
    }

    // borealis really hates it when you remove a view.
    // just rebuild the damn thing.
    if (childFocused)
        brls::Application::giveFocus(this);

    hostContainer->clearViews();
    hostItems.clear();

    bool animateEntrance = !entrancePlayed;
    int shownIndex = 0;

    const int perRow = 6;
    int col = 0;
    brls::Box* row = nullptr;
    auto* hostsMap = settings->getHostsMap();
    if (hostsMap) {
        for (auto& [name, host] : *hostsMap) {
            if (!host) {
                brls::Logger::error("Null host in hosts map for key: {}", name);
                continue;
            }
            if (!host->hasRpKey() && !host->isDiscovered())
                continue;
            if (col == 0) {
                row = new brls::Box();
                row->setAxis(brls::Axis::ROW);
                hostContainer->addView(row);
            }
            auto* item = new HostItemView(host.get());
            hostItems[host.get()] = item;
            row->addView(item);
            if (animateEntrance)
                item->playEntrance(shownIndex * 45);
            shownIndex++;
            col = (col + 1) % perRow;
        }
    }

    bool hasHosts = !hostItems.empty();

    if (settings->getDevFakeHosts()) {
        static std::vector<Host*> fakeHosts;
        if (fakeHosts.empty()) {
            for (int i = 1; i <= 20; i++) {
                Host* h = new Host("PS5-Debug-" + std::to_string(i));
                h->setChiakiTarget(CHIAKI_TARGET_PS5_1);
                h->setState((i % 4 == 0) ? CHIAKI_DISCOVERY_HOST_STATE_STANDBY : CHIAKI_DISCOVERY_HOST_STATE_READY);
                h->setHostType((i % 2 == 0) ? HostType::Auto : HostType::Manual);
                fakeHosts.push_back(h);
            }
        }
        for (Host* h : fakeHosts) {
            if (col == 0) {
                row = new brls::Box();
                row->setAxis(brls::Axis::ROW);
                hostContainer->addView(row);
            }
            row->addView(new HostItemView(h));
            col = (col + 1) % perRow;
        }
        hasHosts = true;
    }

    if (hasHosts && animateEntrance)
        entrancePlayed = true;

    if (emptyAddSlot)
        emptyAddSlot->clearViews();
    auto* addCard = new AddHostCard();
    if (hasHosts) {
        if (col == 0 || !row) {
            row = new brls::Box();
            row->setAxis(brls::Axis::ROW);
            hostContainer->addView(row);
        }
        row->addView(addCard);
    } else if (emptyAddSlot) {
        emptyAddSlot->addView(addCard);
    }

    if (emptyMessage) {
        emptyMessage->setVisibility(hasHosts ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    }

    if (childFocused) {
        if (hasHosts) {
            brls::Application::giveFocus(hostContainer);
        } else if (findRemoteBtn) {
            brls::Application::giveFocus(findRemoteBtn);
        }
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
