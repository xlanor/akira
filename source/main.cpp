/*
 * Akira - PlayStation Remote Play for Nintendo Switch
 * Built with Borealis UI Framework
 */

#include <switch.h>
#include <borealis.hpp>
#include <SDL2/SDL.h>

#include <chiaki/common.h>
#include <chiaki/log.h>

#include "views/host_list_tab.hpp"
#include "views/settings_tab.hpp"
#include "views/add_host_tab.hpp"
#include "views/build_info_tab.hpp"
#include "views/stream_view.hpp"
#include "views/enter_pin_view.hpp"
#include "core/io.hpp"
#include "core/settings_manager.hpp"

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

    brls::Logger::info("Chiaki library initialized");

    AppletType appletType = appletGetAppletType();
    brls::Logger::info("Applet type: {} ({})", appletTypeToString(appletType), static_cast<int>(appletType));

    IO::GetInstance()->SetMesaConfig();

    brls::Application::registerXMLView("HostListTab", HostListTab::create);
    brls::Application::registerXMLView("SettingsTab", SettingsTab::create);
    brls::Application::registerXMLView("AddHostTab", AddHostTab::create);
    brls::Application::registerXMLView("BuildInfoTab", BuildInfoTab::create);
    brls::Application::registerXMLView("StreamView", StreamView::create);
    brls::Application::registerXMLView("EnterPinView", EnterPinView::create);

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
