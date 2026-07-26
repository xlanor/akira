#ifndef AKIRA_SETTINGS_ACCOUNT_VIEW_HPP
#define AKIRA_SETTINGS_ACCOUNT_VIEW_HPP

#include <borealis.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/cells/cell_input.hpp>
#include <string>

#include "core/settings_manager.hpp"
#include "views/psn_action_button.hpp"

class SettingsAccountView : public brls::Box {
public:
    SettingsAccountView();
    ~SettingsAccountView() override;

    void willAppear(bool resetState) override;
    void willDisappear(bool resetState) override;

    static SettingsAccountView* currentInstance;

private:
    BRLS_BIND(brls::InputCell, psnAccountIdInput, "settings/psnAccountId");
    BRLS_BIND(brls::InputCell, companionHostInput, "settings/companionHost");
    BRLS_BIND(brls::InputCell, companionPortInput, "settings/companionPort");
    BRLS_BIND(brls::Button, fetchPsnBtn, "settings/fetchPsnBtn");
    BRLS_BIND(brls::Button, refreshTokenBtn, "settings/refreshTokenBtn");
    BRLS_BIND(brls::Button, clearPsnBtn, "settings/clearPsnBtn");
    BRLS_BIND(brls::Button, revealCredentialsBtn, "settings/revealCredentials");
    BRLS_BIND(brls::DetailCell, credOnlineIdCell, "settings/credOnlineId");
    BRLS_BIND(brls::DetailCell, credAccessTokenCell, "settings/credAccessToken");
    BRLS_BIND(brls::DetailCell, credRefreshTokenCell, "settings/credRefreshToken");
    BRLS_BIND(brls::DetailCell, credTokenExpiryCell, "settings/credTokenExpiry");
    BRLS_BIND(brls::DetailCell, credDuidCell, "settings/credDuid");

    PsnActionButton refreshTokenGate;

    SettingsManager* settings = nullptr;
    bool credentialsRevealed = false;

    void initAuthSection();
    void updateCredentialsDisplay();
    std::string censorString(const std::string& str);
};

#endif // AKIRA_SETTINGS_ACCOUNT_VIEW_HPP
