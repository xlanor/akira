#include "views/settings_account_view.hpp"
#include "core/discovery_manager.hpp"
#include "psn/auth.hpp"

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
            settings->setPsnAccountId(nullptr, text);
            settings->writeFile();
            brls::Logger::info("PSN Account ID set");
        },
        "akira/settings/psn_account_id_placeholder"_i18n,
        "akira/settings/psn_account_id_hint"_i18n
    );

    std::string currentHost = settings->getCompanionHost();
    companionHostInput->init(
        "akira/settings/companion_host"_i18n,
        currentHost,
        [this](std::string text) {
            settings->setCompanionHost(text);
            settings->writeFile();
            brls::Logger::info("Companion host set to {}", text);
        },
        "akira/settings/companion_host_placeholder"_i18n,
        "akira/settings/companion_host_hint"_i18n
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

    fetchPsnBtn->setStyle(&BUTTONSTYLE_GREEN);
    fetchPsnBtn->setBackgroundColor(nvgRGBA(74, 222, 128, 255));

    fetchPsnBtn->registerClickAction([this](brls::View* view) {
        std::string host = companionHostInput->getValue();
        std::string portStr = companionPortInput->getValue();

        if (host.empty()) {
            brls::Application::notify("akira/settings/enter_companion_host_first"_i18n);
            return true;
        }

        int port = std::atoi(portStr.c_str());
        if (port <= 0 || port > 65535) {
            port = 8080;
        }

        brls::Application::notify("akira/settings/fetching_credentials"_i18n);

        DiscoveryManager::getInstance()->fetchCompanionCredentials(
            host, port,
            [](const std::string& onlineId, const std::string& accountId,
               const std::string& accessToken, const std::string& refreshToken,
               int64_t expiresAt, const std::string& duid) {
                SettingsManager* settings = SettingsManager::getInstance();

                if (!onlineId.empty()) {
                    settings->setPsnOnlineId(nullptr, onlineId);
                    brls::Logger::info("PSN Online ID set to {}", onlineId);
                }
                if (!accountId.empty()) {
                    settings->setPsnAccountId(nullptr, accountId);
                    brls::Logger::info("PSN Account ID set");
                    if (SettingsAccountView::currentInstance) {
                        SettingsAccountView::currentInstance->psnAccountIdInput->setValue(accountId);
                    }
                }
                if (!accessToken.empty()) {
                    settings->setPsnAccessToken(accessToken);
                }
                if (!refreshToken.empty()) {
                    settings->setPsnRefreshToken(refreshToken);
                }
                if (expiresAt > 0) {
                    settings->setPsnTokenExpiresAt(expiresAt);
                    brls::Logger::info("PSN token expires at {}", expiresAt);
                }
                if (!duid.empty()) {
                    settings->setGlobalDuid(duid);
                    brls::Logger::info("DUID set from companion");
                }
                settings->writeFile();
                brls::Application::notify("akira/settings/credentials_fetched"_i18n);
                brls::Logger::info("Fetched PSN credentials from companion");

                if (SettingsAccountView::currentInstance) {
                    SettingsAccountView::currentInstance->updateCredentialsDisplay();
                }
            },
            [](const std::string& error) {
                brls::Application::notify(brls::getStr("akira/settings/fetch_failed", error));
                brls::Logger::error("Failed to fetch PSN credentials: {}", error);
            }
        );

        return true;
    });

    refreshTokenBtn->setStyle(&BUTTONSTYLE_GREEN);

    refreshTokenGate.attach(
        refreshTokenBtn,
        nvgRGBA(74, 222, 128, 255),
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
        if (refreshToken.empty()) {
            brls::Application::notify("akira/settings/no_refresh_token"_i18n);
            return true;
        }

        brls::Application::notify("akira/settings/refreshing_token"_i18n);

        psn::Auth::instance().refresh(
            []() {
                brls::Application::notify("akira/settings/token_refreshed"_i18n);
                brls::Logger::info("PSN token refreshed");
                if (SettingsAccountView::currentInstance) {
                    SettingsAccountView::currentInstance->updateCredentialsDisplay();
                }
            },
            [](psn::AuthError kind, const std::string& error) {
                brls::Application::notify(brls::getStr("akira/settings/refresh_failed", error));
                brls::Logger::error("Failed to refresh PSN token ({}): {}",
                    kind == psn::AuthError::Invalid ? "invalid" : "transient", error);
            }
        );

        refreshTokenGate.apply();

        return true;
    });

    clearPsnBtn->setStyle(&BUTTONSTYLE_BLUE);
    clearPsnBtn->setBackgroundColor(nvgRGBA(92, 157, 255, 255));

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
    credOnlineIdCell->setText("akira/settings/online_id"_i18n);
    std::string onlineId = settings->getPsnOnlineId(nullptr);
    credOnlineIdCell->setDetailText(onlineId.empty() ? "akira/common/not_set"_i18n : onlineId);

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
