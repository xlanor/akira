#ifndef AKIRA_HOST_LIST_TAB_HPP
#define AKIRA_HOST_LIST_TAB_HPP

#include <borealis.hpp>
#include <unordered_map>

#include "core/discovery_manager.hpp"
#include "core/settings_manager.hpp"
#include "views/psn_action_button.hpp"
#include "views/profile_chip_view.hpp"
#include "views/recently_played_rail.hpp"
#include "views/add_host_tab.hpp"

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

    static void notifyActiveProfileChanged();

private:
    BRLS_BIND(brls::Box, hostContainer, "host/container");
    BRLS_BIND(brls::Box, emptyMessage, "empty/message");
    BRLS_BIND(brls::Box, emptyAddSlot, "empty/addSlot");
    BRLS_BIND(brls::Box, chipSlot, "host/chipSlot");
    BRLS_BIND(brls::Box, railSlot, "host/railSlot");
    BRLS_BIND(brls::Button, findRemoteBtn, "host/findRemoteBtn");

    ProfileChipView* profileChip = nullptr;
    RecentlyPlayedRail* recentRail = nullptr;

    void initFindRemoteButton();

    PsnActionButton findRemoteGate;

    SettingsManager* settings = nullptr;
    DiscoveryManager* discovery = nullptr;

    bool entrancePlayed = false;

    std::unordered_map<Host*, HostItemView*> hostItems;

    static HostListTab* currentInstance;
    static bool isConnecting;
    static bool isRegistering;
    static bool isActive;
};

#endif // AKIRA_HOST_LIST_TAB_HPP
