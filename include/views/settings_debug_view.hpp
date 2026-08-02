#ifndef AKIRA_SETTINGS_DEBUG_VIEW_HPP
#define AKIRA_SETTINGS_DEBUG_VIEW_HPP

#include <borealis.hpp>
#include <borealis/views/cells/cell_bool.hpp>

#include "core/settings_manager.hpp"

class SettingsDebugView : public brls::Box {
public:
    SettingsDebugView();

private:
    BRLS_BIND(brls::BooleanCell, enableFileLoggingToggle, "settings/enableFileLogging");
    BRLS_BIND(brls::BooleanCell, debugLwipLogToggle, "settings/debugLwipLog");
    BRLS_BIND(brls::BooleanCell, debugWireguardLogToggle, "settings/debugWireguardLog");
    BRLS_BIND(brls::BooleanCell, debugRenderLogToggle, "settings/debugRenderLog");
    BRLS_BIND(brls::BooleanCell, debugChiakiLogToggle, "settings/debugChiakiLog");
    BRLS_BIND(brls::BooleanCell, debugDiscoveryLogToggle, "settings/debugDiscoveryLog");
    BRLS_BIND(brls::BooleanCell, debugFfmpegLogToggle, "settings/debugFfmpegLog");
    BRLS_BIND(brls::Button, openDiscoveryLogBtn, "settings/openDiscoveryLog");

    SettingsManager* settings = nullptr;

    void initEnableFileLoggingToggle();
    void initDebugLwipLogToggle();
    void initDebugWireguardLogToggle();
    void initDebugRenderLogToggle();
    void initDebugChiakiLogToggle();
    void initDebugDiscoveryLogToggle();
    void initDebugFfmpegLogToggle();
};

#endif // AKIRA_SETTINGS_DEBUG_VIEW_HPP
