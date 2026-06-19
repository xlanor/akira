#include "views/account_edit_view.hpp"
#include "core/discovery_manager.hpp"

#include <borealis/core/i18n.hpp>
using namespace brls::literals;

#include <ctime>
#include <format>
#include <iomanip>
#include <sstream>

AccountEditView::AccountEditView(const std::string& accountId)
    : originalAccountId(accountId)
{
    this->inflateFromXMLRes("xml/views/account_edit.xml");

    settings = SettingsManager::getInstance();

    Account* existing = accountId.empty() ? nullptr : settings->findAccount(accountId);
    isNew = (existing == nullptr);
    if (existing) {
        draft = *existing;
    }

    titleLabel->setText(isNew ? "akira/account/add_title"_i18n : "akira/account/edit_title"_i18n);

    onlineIdInput->init(
        "akira/account/online_id"_i18n,
        draft.onlineId,
        [](std::string text) {},
        "akira/account/online_id_placeholder"_i18n,
        "akira/account/online_id_hint"_i18n
    );

    accountIdInput->init(
        "akira/account/account_id"_i18n,
        draft.accountId,
        [](std::string text) {},
        "akira/account/account_id_placeholder"_i18n,
        "akira/account/account_id_hint"_i18n
    );

    companionHostInput->init(
        "akira/account/companion_host"_i18n,
        settings->getCompanionHost(),
        [](std::string text) {},
        "akira/account/companion_host_placeholder"_i18n,
        "akira/account/companion_host_hint"_i18n
    );

    companionPortInput->init(
        "akira/account/companion_port"_i18n,
        std::format("{}", settings->getCompanionPort()),
        [](std::string text) {},
        "akira/account/companion_port_placeholder"_i18n,
        "akira/account/companion_port_hint"_i18n
    );

    lookupBtn->registerClickAction([this](brls::View* view) {
        onLookupClicked();
        return true;
    });

    fetchBtn->registerClickAction([this](brls::View* view) {
        onFetchClicked();
        return true;
    });

    refreshBtn->registerClickAction([this](brls::View* view) {
        onRefreshClicked();
        return true;
    });

    cancelBtn->registerClickAction([this](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });

    saveBtn->registerClickAction([this](brls::View* view) {
        onSaveClicked();
        return true;
    });

    deleteBtn->registerClickAction([this](brls::View* view) {
        onDeleteClicked();
        return true;
    });
    deleteBtn->setVisibility(isNew ? brls::Visibility::GONE : brls::Visibility::VISIBLE);

    this->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });

    updateInfo();
}

AccountEditView::~AccountEditView()
{
}

brls::View* AccountEditView::create()
{
    return nullptr;
}

void AccountEditView::updateInfo()
{
    std::string info = draft.isRemote() ? "akira/account/remote"_i18n : "akira/account/local"_i18n;
    if (draft.tokenExpiresAt > 0) {
        std::time_t expTime = static_cast<std::time_t>(draft.tokenExpiresAt);
        std::tm* tm = std::localtime(&expTime);
        std::ostringstream oss;
        oss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
        std::time_t now = std::time(nullptr);
        info += "  |  " + "akira/account/token_expires"_i18n + ": " + oss.str();
        if (now > expTime) {
            info += "akira/account/expired_suffix"_i18n;
        }
    }
    infoLabel->setText(info);
}

void AccountEditView::onLookupClicked()
{
    std::string onlineId = onlineIdInput->getValue();
    if (onlineId.empty()) {
        brls::Application::notify("akira/account/enter_online_id_first"_i18n);
        return;
    }

    brls::Application::notify("akira/account/looking_up"_i18n);

    DiscoveryManager::getInstance()->lookupPsnAccountId(
        onlineId,
        [this](const std::string& accountId) {
            brls::sync([this, accountId]() {
                accountIdInput->setValue(accountId);
                draft.accountId = accountId;
                brls::Application::notify("akira/account/account_id_found"_i18n);
            });
        },
        [](const std::string& error) {
            brls::sync([error]() {
                brls::Application::notify(brls::getStr("akira/account/lookup_failed", error));
            });
        }
    );
}

void AccountEditView::onFetchClicked()
{
    std::string host = companionHostInput->getValue();
    std::string portStr = companionPortInput->getValue();

    if (host.empty()) {
        brls::Application::notify("akira/account/enter_companion_host_first"_i18n);
        return;
    }

    int port = std::atoi(portStr.c_str());
    if (port <= 0 || port > 65535) {
        port = 8080;
    }

    settings->setCompanionHost(host);
    settings->setCompanionPort(port);
    settings->writeFile();

    brls::Application::notify("akira/account/fetching_credentials"_i18n);

    DiscoveryManager::getInstance()->fetchCompanionCredentials(
        host, port,
        [this](const std::string& onlineId, const std::string& accountId,
               const std::string& accessToken, const std::string& refreshToken,
               int64_t expiresAt, const std::string& duid) {
            brls::sync([this, onlineId, accountId, accessToken, refreshToken, expiresAt, duid]() {
                if (!onlineId.empty()) {
                    onlineIdInput->setValue(onlineId);
                    draft.onlineId = onlineId;
                }
                if (!accountId.empty()) {
                    accountIdInput->setValue(accountId);
                    draft.accountId = accountId;
                }
                if (!accessToken.empty()) draft.accessToken = accessToken;
                if (!refreshToken.empty()) draft.refreshToken = refreshToken;
                if (expiresAt > 0) draft.tokenExpiresAt = expiresAt;
                if (!duid.empty()) draft.duid = duid;
                updateInfo();
                brls::Application::notify("akira/account/credentials_fetched"_i18n);
            });
        },
        [](const std::string& error) {
            brls::sync([error]() {
                brls::Application::notify(brls::getStr("akira/account/fetch_failed", error));
            });
        }
    );
}

void AccountEditView::onRefreshClicked()
{
    draft.onlineId = onlineIdInput->getValue();
    draft.accountId = accountIdInput->getValue();

    if (draft.refreshToken.empty()) {
        brls::Application::notify("akira/account/no_refresh_token"_i18n);
        return;
    }
    if (draft.accountId.empty()) {
        brls::Application::notify("akira/account/account_id_required"_i18n);
        return;
    }

    settings->upsertAccount(draft);

    brls::Application::notify("akira/account/refreshing_token"_i18n);

    DiscoveryManager::getInstance()->refreshPsnTokenForAccount(
        draft.accountId,
        [this]() {
            brls::sync([this]() {
                if (Account* a = settings->findAccount(draft.accountId)) {
                    draft = *a;
                }
                updateInfo();
                brls::Application::notify("akira/account/token_refreshed"_i18n);
            });
        },
        [this](const std::string& error) {
            brls::sync([this, error]() {
                brls::Application::notify(brls::getStr("akira/account/refresh_failed", error));
            });
        }
    );
}

void AccountEditView::onSaveClicked()
{
    draft.onlineId = onlineIdInput->getValue();
    draft.accountId = accountIdInput->getValue();

    if (draft.onlineId.empty() && draft.accountId.empty()) {
        brls::Application::notify("akira/account/error_empty"_i18n);
        return;
    }

    if (draft.accountId.empty()) {
        brls::Application::notify("akira/account/account_id_needed"_i18n);
        return;
    }

    if (!originalAccountId.empty() && originalAccountId != draft.accountId) {
        settings->removeAccount(originalAccountId);
    }

    settings->upsertAccount(draft);

    if (settings->getDefaultAccountId().empty() && !draft.accountId.empty()) {
        settings->setDefaultAccountId(draft.accountId);
    }

    settings->writeFile();

    brls::Application::notify("akira/account/saved"_i18n);

    if (onSaved) {
        onSaved();
    }

    brls::Application::popActivity();
}

void AccountEditView::onDeleteClicked()
{
    std::string id = originalAccountId.empty() ? draft.accountId : originalAccountId;
    std::string name = draft.label();
    if (name.empty()) name = "akira/account/unnamed"_i18n;

    auto* dialog = new brls::Dialog(brls::getStr("akira/account/delete_confirm", name));
    dialog->addButton("akira/common/cancel"_i18n, []() {});
    dialog->addButton("akira/common/delete"_i18n, [this, id]() {
        settings->removeAccount(id);
        settings->writeFile();
        brls::Application::notify("akira/account/deleted"_i18n);
        if (onSaved) {
            onSaved();
        }
        brls::Application::popActivity();
    });
    dialog->open();
}
