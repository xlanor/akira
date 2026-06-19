#ifndef AKIRA_ACCOUNT_EDIT_VIEW_HPP
#define AKIRA_ACCOUNT_EDIT_VIEW_HPP

#include <borealis.hpp>
#include <functional>

#include "core/account.hpp"
#include "core/settings_manager.hpp"

class AccountEditView : public brls::Box {
public:
    using SaveCallback = std::function<void()>;

    explicit AccountEditView(const std::string& accountId);
    ~AccountEditView() override;

    void setOnSaved(SaveCallback callback) { onSaved = std::move(callback); }

    static brls::View* create();

private:
    BRLS_BIND(brls::Label, titleLabel, "account_edit/title");
    BRLS_BIND(brls::InputCell, onlineIdInput, "account_edit/onlineId");
    BRLS_BIND(brls::Button, lookupBtn, "account_edit/lookupBtn");
    BRLS_BIND(brls::InputCell, accountIdInput, "account_edit/accountId");
    BRLS_BIND(brls::InputCell, companionHostInput, "account_edit/companionHost");
    BRLS_BIND(brls::InputCell, companionPortInput, "account_edit/companionPort");
    BRLS_BIND(brls::Button, fetchBtn, "account_edit/fetchBtn");
    BRLS_BIND(brls::Button, refreshBtn, "account_edit/refreshBtn");
    BRLS_BIND(brls::Label, infoLabel, "account_edit/info");
    BRLS_BIND(brls::Button, cancelBtn, "account_edit/cancelBtn");
    BRLS_BIND(brls::Button, deleteBtn, "account_edit/deleteBtn");
    BRLS_BIND(brls::Button, saveBtn, "account_edit/saveBtn");

    SettingsManager* settings = nullptr;
    SaveCallback onSaved;

    std::string originalAccountId;
    bool isNew = false;
    Account draft;

    void updateInfo();
    void onLookupClicked();
    void onFetchClicked();
    void onRefreshClicked();
    void onSaveClicked();
    void onDeleteClicked();
};

#endif // AKIRA_ACCOUNT_EDIT_VIEW_HPP
