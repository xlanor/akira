#ifndef AKIRA_HOST_SETTINGS_VIEW_HPP
#define AKIRA_HOST_SETTINGS_VIEW_HPP

#include <borealis.hpp>
#include <functional>

#include "core/host.hpp"
#include "core/settings_manager.hpp"

class HostSettingsView : public brls::Box {
public:
    using SaveCallback = std::function<void()>;

    explicit HostSettingsView(Host* host);
    ~HostSettingsView() override;

    void setOnSaved(SaveCallback callback) { onSaved = std::move(callback); }

    static brls::View* create();

private:
    BRLS_BIND(brls::Label, titleLabel, "host_settings/title");
    BRLS_BIND(brls::InputCell, hostNameInput, "host_settings/hostName");
    BRLS_BIND(brls::InputCell, hostAddrInput, "host_settings/hostAddr");
    BRLS_BIND(brls::InputCell, psnAccountIdInput, "host_settings/psnAccountId");
    BRLS_BIND(brls::InputCell, psnOnlineIdInput, "host_settings/psnOnlineId");
    BRLS_BIND(brls::Button, lookupBtn, "host_settings/lookupBtn");
    BRLS_BIND(brls::InputCell, consolePINInput, "host_settings/consolePIN");
    BRLS_BIND(brls::SelectorCell, hapticSelector, "host_settings/haptic");
    BRLS_BIND(brls::Button, cancelBtn, "host_settings/cancelBtn");
    BRLS_BIND(brls::Button, saveBtn, "host_settings/saveBtn");
    BRLS_BIND(brls::Label, hintLabel, "host_settings/hint");

    Host* host = nullptr;
    SettingsManager* settings = nullptr;
    SaveCallback onSaved;
    int selectedHaptic = -1;
    std::string originalHostName;

    void initHostNameInput();
    void initHostAddrInput();
    void initPsnAccountIdInput();
    void initLookupButton();
    void initConsolePINInput();
    void initHapticSelector();
    void onSaveClicked();
    void onCancelClicked();
};

#endif // AKIRA_HOST_SETTINGS_VIEW_HPP
