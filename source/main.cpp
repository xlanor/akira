/*
 * Akira - PlayStation Remote Play for Nintendo Switch
 * Built with Borealis UI Framework
 */

#include <switch.h>
#include <borealis.hpp>
#include <SDL2/SDL.h>
#include <arpa/inet.h>
#include <fstream>

#include <chiaki/common.h>
#include <chiaki/log.h>
#include "crypto/libnx/gmac.h"

#include "views/host_list_tab.hpp"
#include "views/settings_tab.hpp"
#include "views/add_host_tab.hpp"
#include "views/build_info_tab.hpp"
#include "views/config_view_tab.hpp"
#include "views/network_utilities_tab.hpp"
#include "views/stream_view.hpp"
#include "views/enter_pin_view.hpp"
#include "views/connection_view.hpp"
#include "core/io.hpp"
#include "core/settings_manager.hpp"

static std::string getLocalIpAddress() {
    u32 ip = 0;
    Result rc = nifmGetCurrentIpAddress(&ip);
    if (R_SUCCEEDED(rc) && ip != 0) {
        struct in_addr addr;
        addr.s_addr = ip;
        return std::string(inet_ntoa(addr));
    }
    return "Not connected";
}

static std::string getAppVersion() {
    std::ifstream file("romfs:/build_info.txt");
    if (!file.is_open()) {
        return "";
    }
    std::string line;
    if (std::getline(file, line)) {
        size_t spacePos = line.find(' ');
        if (spacePos != std::string::npos) {
            return "v" + line.substr(spacePos + 1);
        }
    }
    return "";
}

void initCustomTheme()
{
    brls::Theme::getLightTheme().addColor("color/card", nvgRGB(245, 246, 247));
    brls::Theme::getDarkTheme().addColor("color/card", nvgRGB(51, 52, 53));
}

static void chiaki_to_brls_log(ChiakiLogLevel level, const char* msg, void* user)
{
    switch (level)
    {
        case CHIAKI_LOG_ERROR:
            brls::Logger::error("{}", msg);
            break;
        case CHIAKI_LOG_WARNING:
            brls::Logger::warning("{}", msg);
            break;
        case CHIAKI_LOG_INFO:
            brls::Logger::info("{}", msg);
            break;
        case CHIAKI_LOG_DEBUG:
        case CHIAKI_LOG_VERBOSE:
            // filter out for now 
            break;
    }
}

static const char* appletTypeToString(AppletType type)
{
    switch (type)
    {
        case AppletType_None:              return "None";
        case AppletType_Default:           return "Default";
        case AppletType_Application:       return "Application";
        case AppletType_SystemApplet:      return "SystemApplet";
        case AppletType_LibraryApplet:     return "LibraryApplet";
        case AppletType_OverlayApplet:     return "OverlayApplet";
        case AppletType_SystemApplication: return "SystemApplication";
        default:                           return "Unknown";
    }
}

class MainActivity : public brls::Activity
{
public:
    CONTENT_FROM_XML_RES("activity/main.xml");

    void onContentAvailable() override
    {
        brls::Logger::info("Main activity content available");

        auto* appletFrame = dynamic_cast<brls::AppletFrame*>(this->getContentView());
        if (appletFrame) {
            std::string version = getAppVersion();
            if (!version.empty()) {
                appletFrame->setTitle("Akira " + version);
            }

            auto* header = appletFrame->getHeader();
            if (header) {
                auto* attribution = new brls::Label();
                attribution->setText("Akira uses chiaki-ng for its remote capabilities");
                attribution->setFontSize(20);
                attribution->setTextColor(nvgRGBA(170, 170, 170, 255));
                attribution->setMarginRight(20);
                attribution->setGrow(1.0f);
                attribution->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
                header->addView(attribution);
            }

            auto* footer = appletFrame->getFooter();
            if (footer) {
                // Navigate BottomBar structure: Box > Box > Box (row with status items)
                auto& outerChildren = footer->getChildren();
                if (!outerChildren.empty()) {
                    auto* containerBox = dynamic_cast<brls::Box*>(outerChildren[0]);
                    if (containerBox && !containerBox->getChildren().empty()) {
                        auto* rowBox = dynamic_cast<brls::Box*>(containerBox->getChildren()[0]);
                        if (rowBox) {
                            auto* ipLabel = new brls::Label();
                            ipLabel->setText("IP: " + getLocalIpAddress());
                            ipLabel->setFontSize(18);
                            ipLabel->setTextColor(nvgRGBA(150, 150, 150, 255));
                            ipLabel->setVerticalAlign(brls::VerticalAlign::CENTER);
                            rowBox->addView(ipLabel);
                        }
                    }
                }
            }
        }
    }
};

class AppletWarningActivity : public brls::Activity
{
public:
    CONTENT_FROM_XML_RES("activity/applet_warning.xml");
};

int main(int argc, char* argv[])
{
    brls::Logger::setLogLevel(brls::LogLevel::LOG_INFO);

    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "-d") == 0)
        {
            brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        }
        else if (std::strcmp(argv[i], "-v") == 0)
        {
            brls::Application::enableDebuggingView(true);
        }
    }

    brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_EN_US;

    initCustomTheme();

    if (!brls::Application::init())
    {
        brls::Logger::error("Unable to init Borealis application");
        return EXIT_FAILURE;
    }

    brls::getStyle().addMetric("brls/tab_frame/sidebar_width", 369.0f);

    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0)
    {
        brls::Logger::error("SDL_Init failed: {}", SDL_GetError());
        return EXIT_FAILURE;
    }

    ChiakiErrorCode err = chiaki_lib_init();
    if (err != CHIAKI_ERR_SUCCESS)
    {
        brls::Logger::error("Chiaki lib init failed: {}", chiaki_error_string(err));
        return EXIT_FAILURE;
    }

    static ChiakiLog chiakiLog;
#ifdef MUTE_CHIAKI_LOGS
    chiaki_log_init(&chiakiLog, 0, chiaki_to_brls_log, nullptr);
#else
    chiaki_log_init(&chiakiLog, CHIAKI_LOG_ALL, chiaki_to_brls_log, nullptr);
#endif
    SettingsManager::getInstance()->setLogger(&chiakiLog);
    IO::GetInstance()->SetLogger(&chiakiLog);

    static FILE* logFile = nullptr;
    if (SettingsManager::getInstance()->getEnableFileLogging()) {
        std::string logPath = SettingsManager::getLogFilePath();
        logFile = fopen(logPath.c_str(), "w");
        if (logFile) {
            brls::Logger::setLogOutput(logFile);
            brls::Logger::info("File logging enabled: {}", logPath);
        }
    }

    if (SettingsManager::getInstance()->getEnableExperimentalCrypto()) {
        chiaki_libnx_set_ghash_mode(CHIAKI_LIBNX_GHASH_PMULL);
        brls::Logger::info("GHASH mode: PMULL");
    } else {
        chiaki_libnx_set_ghash_mode(CHIAKI_LIBNX_GHASH_TABLE);
        brls::Logger::info("GHASH mode: TABLE (default)");
    }

    brls::Logger::info("Chiaki library initialized");

    AppletType appletType = appletGetAppletType();
    brls::Logger::info("Applet type: {} ({})", appletTypeToString(appletType), static_cast<int>(appletType));

    IO::GetInstance()->SetMesaConfig();

    brls::Application::registerXMLView("HostListTab", HostListTab::create);
    brls::Application::registerXMLView("SettingsTab", SettingsTab::create);
    brls::Application::registerXMLView("AddHostTab", AddHostTab::create);
    brls::Application::registerXMLView("BuildInfoTab", BuildInfoTab::create);
    brls::Application::registerXMLView("ConfigViewTab", ConfigViewTab::create);
    brls::Application::registerXMLView("NetworkUtilitiesTab", NetworkUtilitiesTab::create);
    brls::Application::registerXMLView("StreamView", StreamView::create);
    brls::Application::registerXMLView("EnterPinView", EnterPinView::create);
    brls::Application::registerXMLView("ConnectionView", ConnectionView::create);

    brls::Application::createWindow("Akira");

    brls::Application::getPlatform()->exitToHomeMode(true);

    if (appletType == AppletType_Application)
    {
        brls::Application::pushActivity(new MainActivity());
    }
    else
    {
        brls::Application::pushActivity(new AppletWarningActivity());
    }

    try{
        while (brls::Application::mainLoop())
        {
        }
    } catch (const std::exception& e) {
        brls::Logger::error("CRASH: {}", e.what());
        fflush(stdout);
    }

    brls::Logger::info("Application exiting");

    SDL_Quit();

    nvExit();

    return EXIT_SUCCESS;
}
