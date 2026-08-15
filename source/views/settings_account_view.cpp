#include "views/settings_account_view.hpp"
#include "views/host_list_tab.hpp"
#include "views/pair_view.hpp"
#include "ui/theme.hpp"
#include "core/discovery_manager.hpp"
#include "core/trophy_manager.hpp"
#include "psn/auth.hpp"
#include "psn/token_refresher.hpp"

#include <borealis/core/i18n.hpp>
#include <format>

#include <ctime>
#include <iomanip>
#include <sstream>

using namespace brls::literals;

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

static const brls::ButtonStyle BUTTONSTYLE_GREEN = {
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

SettingsAccountView* SettingsAccountView::currentInstance = nullptr;

SettingsAccountView::SettingsAccountView() {
    this->inflateFromXMLRes("xml/settings/account.xml");

    currentInstance = this;
    settings = SettingsManager::getInstance();

    initAuthSection();

    profileSwitcher = new ProfileSwitcherView();
    profileSwitcher->onProfileChanged = [this]() {
        updateCredentialsDisplay();
        trophiesEnabledCell->setOn(settings->getActiveProfileTrophiesEnabled(), false);
    };
    profileCardSlot->addView(profileSwitcher);

    trophiesEnabledCell->init(
        "akira/settings/trophies_enabled"_i18n,
        settings->getActiveProfileTrophiesEnabled(),
        [this](bool isOn) {
            settings->setActiveProfileTrophiesEnabled(isOn);
            settings->writeFile();
        });

    revealCredentialsBtn->registerClickAction([this](brls::View*) {
        credentialsRevealed = !credentialsRevealed;
        revealCredentialsBtn->setText(credentialsRevealed ? "akira/settings/hide_secrets"_i18n : "akira/settings/reveal_secrets"_i18n);
        updateCredentialsDisplay();
        return true;
    });

    updateCredentialsDisplay();

}

SettingsAccountView::~SettingsAccountView() {
    if (currentInstance == this) {
        currentInstance = nullptr;
    }
}

void SettingsAccountView::willAppear(bool resetState) {
    Box::willAppear(resetState);
    if (profileSwitcher) profileSwitcher->refresh();
    refreshTokenGate.start();
}

void SettingsAccountView::willDisappear(bool resetState) {
    Box::willDisappear(resetState);
    refreshTokenGate.stop();
}

void SettingsAccountView::initAuthSection() {
    std::string currentAccountId = settings->getPsnAccountId(nullptr);
    psnAccountIdInput->init(
        "akira/settings/psn_account_id"_i18n,
        currentAccountId,
        [this](std::string text) {
            if (text == settings->getPsnAccountId(nullptr))
                return;
            settings->setPsnAccountId(nullptr, text);
            settings->writeFile();
            if (profileSwitcher) profileSwitcher->refresh();
            HostListTab::notifyActiveProfileChanged();
            brls::Logger::info("PSN Account ID set");
        },
        "akira/settings/psn_account_id_placeholder"_i18n,
        "akira/settings/psn_account_id_hint"_i18n
    );

    std::string currentOnlineId = settings->getPsnOnlineId(nullptr);
    psnOnlineIdInput->init(
        "akira/settings/online_id"_i18n,
        currentOnlineId,
        [this](std::string text) {
            if (text == settings->getPsnOnlineId(nullptr))
                return;
            settings->setPsnOnlineId(nullptr, text);
            settings->writeFile();
            if (profileSwitcher) profileSwitcher->refresh();
            HostListTab::notifyActiveProfileChanged();
            brls::Logger::info("PSN Online ID set");
        },
        "",
        ""
    );

    std::string currentPort = std::format("{}", settings->getCompanionPort());
    companionPortInput->init(
        "akira/settings/companion_port"_i18n,
        currentPort,
        [this](std::string text) {
            int port = std::atoi(text.c_str());
            if (port > 0 && port <= 65535) {
                settings->setCompanionPort(port);
                settings->writeFile();
                brls::Logger::info("Companion port set to {}", port);
            } else {
                brls::Application::notify("akira/settings/invalid_port"_i18n);
            }
        },
        "akira/settings/companion_port_placeholder"_i18n,
        "akira/settings/companion_port_hint"_i18n
    );

    pairBtn->setStyle(&brls::BUTTONSTYLE_PRIMARY);

    pairBtn->registerClickAction([](brls::View*) {
        brls::Application::pushActivity(new brls::Activity(new PairView()));
        return true;
    });

    refreshTokenBtn->setStyle(&BUTTONSTYLE_GREEN);

    refreshTokenGate.attach(
        refreshTokenBtn,
        akira::ui::active().success,
        "akira/settings/refresh_token_btn"_i18n,
        "akira/settings/refresh_token_busy"_i18n,
        "akira/settings/refresh_token_wait",
        []() { return psn::Auth::instance().refreshStatus(); }
    );

    refreshTokenBtn->registerClickAction([this](brls::View* view) {
        if (!refreshTokenGate.isReady()) {
            return true;
        }

        std::string refreshToken = settings->getPsnRefreshToken();
        std::string npsso = settings->getPsnNpsso();
        if (refreshToken.empty() && npsso.empty()) {
            brls::Application::notify("akira/settings/no_refresh_token"_i18n);
            return true;
        }

        brls::Application::notify("akira/settings/refreshing_token"_i18n);

        psn::TokenRefresher::instance().refreshNow([]() {
            brls::Application::notify("akira/settings/token_refreshed"_i18n);
            if (SettingsAccountView::currentInstance) {
                SettingsAccountView::currentInstance->updateCredentialsDisplay();
            }
        });

        refreshTokenGate.apply();

        return true;
    });

    clearPsnBtn->setStyle(&BUTTONSTYLE_BLUE);
    clearPsnBtn->setBackgroundColor(akira::ui::active().accent);

    clearPsnBtn->registerClickAction([this](brls::View* view) {
        auto* dialog = new brls::Dialog("akira/settings/clear_psn_confirm"_i18n);

        dialog->addButton("akira/common/cancel"_i18n, [dialog]() {
            dialog->close();
        });

        dialog->addButton("akira/settings/clear_all"_i18n, [this, dialog]() {
            dialog->close();
            settings->clearPsnTokenData();
            settings->writeFile();
            updateCredentialsDisplay();
            brls::Application::notify("akira/settings/psn_data_cleared"_i18n);
        });

        dialog->open();
        return true;
    });
}

std::string SettingsAccountView::censorString(const std::string& str) {
    if (str.empty()) return "akira/common/not_set"_i18n;
    if (str.length() <= 5) return str;
    return "****" + str.substr(str.length() - 5);
}

void SettingsAccountView::updateCredentialsDisplay() {
    psnAccountIdInput->setValue(settings->getPsnAccountId(nullptr));
    psnOnlineIdInput->setValue(settings->getPsnOnlineId(nullptr));

    credAccessTokenCell->setText("akira/settings/access_token"_i18n);
    std::string accessToken = settings->getPsnAccessToken();
    if (credentialsRevealed) {
        if (!accessToken.empty()) {
            std::string displayToken = accessToken.length() > 40 ? accessToken.substr(0, 36) + "..." : accessToken;
            credAccessTokenCell->setDetailText(displayToken);
        } else {
            credAccessTokenCell->setDetailText("akira/common/not_set"_i18n);
        }
    } else {
        credAccessTokenCell->setDetailText(censorString(accessToken));
    }

    credRefreshTokenCell->setText("akira/settings/refresh_token"_i18n);
    std::string refreshToken = settings->getPsnRefreshToken();
    if (credentialsRevealed) {
        if (!refreshToken.empty()) {
            std::string displayToken = refreshToken.length() > 40 ? refreshToken.substr(0, 36) + "..." : refreshToken;
            credRefreshTokenCell->setDetailText(displayToken);
        } else {
            credRefreshTokenCell->setDetailText("akira/common/not_set"_i18n);
        }
    } else {
        credRefreshTokenCell->setDetailText(censorString(refreshToken));
    }

    credTokenExpiryCell->setText("akira/settings/token_expires"_i18n);
    int64_t expiresAt = settings->getPsnTokenExpiresAt();
    if (expiresAt > 0) {
        std::time_t expTime = static_cast<std::time_t>(expiresAt);
        std::tm* tm = std::localtime(&expTime);
        std::ostringstream oss;
        oss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");

        std::time_t now = std::time(nullptr);
        if (now > expTime) {
            credTokenExpiryCell->setDetailText(oss.str() + "akira/settings/expired_suffix"_i18n);
        } else {
            credTokenExpiryCell->setDetailText(oss.str());
        }
    } else {
        credTokenExpiryCell->setDetailText("akira/common/not_set"_i18n);
    }

    credSsoAccessTokenCell->setText("akira/settings/sso_access_token"_i18n);
    std::string ssoAccess = settings->getPsnMobileSsoAccessToken();
    if (credentialsRevealed) {
        credSsoAccessTokenCell->setDetailText(ssoAccess.empty()
            ? "akira/common/not_set"_i18n
            : (ssoAccess.length() > 40 ? ssoAccess.substr(0, 36) + "..." : ssoAccess));
    } else {
        credSsoAccessTokenCell->setDetailText(censorString(ssoAccess));
    }

    credSsoRefreshTokenCell->setText("akira/settings/sso_refresh_token"_i18n);
    std::string ssoRefresh = settings->getPsnMobileSsoRefreshToken();
    if (credentialsRevealed) {
        credSsoRefreshTokenCell->setDetailText(ssoRefresh.empty()
            ? "akira/common/not_set"_i18n
            : (ssoRefresh.length() > 40 ? ssoRefresh.substr(0, 36) + "..." : ssoRefresh));
    } else {
        credSsoRefreshTokenCell->setDetailText(censorString(ssoRefresh));
    }

    credSsoExpiryCell->setText("akira/settings/sso_expires"_i18n);
    int64_t ssoExpiresAt = settings->getPsnMobileSsoExpiresAt();
    if (ssoExpiresAt > 0) {
        std::time_t ssoExpTime = static_cast<std::time_t>(ssoExpiresAt);
        std::tm* ssoTm = std::localtime(&ssoExpTime);
        std::ostringstream ssoOss;
        ssoOss << std::put_time(ssoTm, "%Y-%m-%d %H:%M:%S");
        std::time_t now = std::time(nullptr);
        credSsoExpiryCell->setDetailText(now > ssoExpTime
            ? ssoOss.str() + "akira/settings/expired_suffix"_i18n
            : ssoOss.str());
    } else {
        credSsoExpiryCell->setDetailText("akira/common/not_set"_i18n);
    }

    credDuidCell->setText("akira/settings/duid"_i18n);
    std::string duid = settings->getGlobalDuid();
    if (credentialsRevealed) {
        if (!duid.empty()) {
            std::string displayDuid = duid.length() > 40 ? duid.substr(0, 36) + "..." : duid;
            credDuidCell->setDetailText(displayDuid);
        } else {
            credDuidCell->setDetailText("akira/common/not_set"_i18n);
        }
    } else {
        credDuidCell->setDetailText(censorString(duid));
    }
}
