#ifndef AKIRA_HOST_LIST_TAB_HPP
#define AKIRA_HOST_LIST_TAB_HPP

#include <borealis.hpp>
#include <unordered_map>

#include "core/discovery_manager.hpp"
#include "core/settings_manager.hpp"

class HostItemView;

class HostListTab : public brls::Box {
    friend class HostItemView;

public:
    HostListTab();
    ~HostListTab() override;

    static brls::View* create();

    void willAppear(bool resetState) override;
    void willDisappear(bool resetState) override;

    void updateHostItem(Host* host);

    void syncHostList();

private:
    BRLS_BIND(brls::Box, hostContainer, "host/container");
    BRLS_BIND(brls::Box, emptyMessage, "empty/message");
    BRLS_BIND(brls::Button, findRemoteBtn, "host/findRemoteBtn");

    void initFindRemoteButton();

    SettingsManager* settings = nullptr;
    DiscoveryManager* discovery = nullptr;

    std::unordered_map<Host*, HostItemView*> hostItems;

    static HostListTab* currentInstance;
    static bool isConnecting;
    static bool isRegistering;
    static bool isActive;
};

#endif // AKIRA_HOST_LIST_TAB_HPP
