#ifndef AKIRA_ACCOUNT_TAB_HPP
#define AKIRA_ACCOUNT_TAB_HPP

#include <borealis.hpp>

#include "core/settings_manager.hpp"

class AccountTab : public brls::Box {
public:
    AccountTab();

    static brls::View* create();

    void willAppear(bool resetState) override;
    void syncList();
    void openEditor(const std::string& accountId);

private:
    BRLS_BIND(brls::Box, container, "account/container");
    BRLS_BIND(brls::Box, emptyMessage, "account/empty");
    BRLS_BIND(brls::Button, addBtn, "account/addBtn");

    SettingsManager* settings = nullptr;
};

#endif // AKIRA_ACCOUNT_TAB_HPP
