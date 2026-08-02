#include "views/settings_debug_view.hpp"
#include "views/discovery_log_view.hpp"

#include <borealis/core/i18n.hpp>

using namespace brls::literals;

SettingsDebugView::SettingsDebugView() {
    this->inflateFromXMLRes("xml/settings/debug_logging.xml");

    settings = SettingsManager::getInstance();

    initEnableFileLoggingToggle();
    initDebugLwipLogToggle();
    initDebugWireguardLogToggle();
    initDebugRenderLogToggle();
    initDebugChiakiLogToggle();
    initDebugDiscoveryLogToggle();
    initDebugFfmpegLogToggle();

    openDiscoveryLogBtn->registerClickAction([](brls::View*) {
        brls::Application::pushActivity(new brls::Activity(new DiscoveryLogView()));
        return true;
    });
}

void SettingsDebugView::initEnableFileLoggingToggle() {
    bool currentValue = settings->getEnableFileLogging();

    enableFileLoggingToggle->init(
        "akira/settings/file_logging"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setEnableFileLogging(isOn);
            if (!isOn) {
                settings->setDebugLwipLog(false);
                settings->setDebugWireguardLog(false);
                settings->setDebugRenderLog(false);
                settings->setDebugChiakiLog(false);
                settings->setDebugDiscoveryLog(false);
                settings->setDebugFfmpegLog(false);
                debugLwipLogToggle->setOn(false, false);
                debugWireguardLogToggle->setOn(false, false);
                debugRenderLogToggle->setOn(false, false);
                debugChiakiLogToggle->setOn(false, false);
                debugDiscoveryLogToggle->setOn(false, false);
                debugFfmpegLogToggle->setOn(false, false);
            }
            settings->writeFile();
        }
    );
}

void SettingsDebugView::initDebugLwipLogToggle() {
    bool currentValue = settings->getDebugLwipLog();

    debugLwipLogToggle->init(
        "akira/settings/lwip_log"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setDebugLwipLog(isOn);
            settings->writeFile();
        }
    );
}

void SettingsDebugView::initDebugWireguardLogToggle() {
    bool currentValue = settings->getDebugWireguardLog();

    debugWireguardLogToggle->init(
        "akira/settings/wireguard_log"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setDebugWireguardLog(isOn);
            settings->writeFile();
        }
    );
}

void SettingsDebugView::initDebugRenderLogToggle() {
    bool currentValue = settings->getDebugRenderLog();

    debugRenderLogToggle->init(
        "akira/settings/render_log"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setDebugRenderLog(isOn);
            settings->writeFile();
        }
    );
}

void SettingsDebugView::initDebugChiakiLogToggle() {
    bool currentValue = settings->getDebugChiakiLog();

    debugChiakiLogToggle->init(
        "akira/settings/chiaki_log"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setDebugChiakiLog(isOn);
            settings->writeFile();
        }
    );
}

void SettingsDebugView::initDebugDiscoveryLogToggle() {
    bool currentValue = settings->getDebugDiscoveryLog();

    debugDiscoveryLogToggle->init(
        "akira/settings/discovery_log"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setDebugDiscoveryLog(isOn);
            settings->writeFile();
        }
    );
}

void SettingsDebugView::initDebugFfmpegLogToggle() {
    bool currentValue = settings->getDebugFfmpegLog();

    debugFfmpegLogToggle->init(
        "akira/settings/ffmpeg_log"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setDebugFfmpegLog(isOn);
            settings->writeFile();
        }
    );
}
