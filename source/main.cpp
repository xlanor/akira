/*
 * Akira - PlayStation Remote Play for Nintendo Switch
 * Built with Borealis UI Framework
 */

#include <switch.h>
#include <borealis.hpp>
#include <borealis/core/thread_pool.hpp>
#include <borealis/views/hint.hpp>
#include <borealis/views/widgets/battery.hpp>
#include <borealis/views/widgets/wireless.hpp>
#include <SDL2/SDL.h>
#include <arpa/inet.h>
#include <array>
#include <fstream>
#include <string_view>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include <chiaki/common.h>
#include <chiaki/log.h>
#include <curl/curl.h>
#include "crypto/libnx/gmac.h"
#include "ui/theme.hpp"

#include "views/host_list_tab.hpp"
#include "views/vendored/switchfin/recycling_grid.hpp"
#include "views/psn_gated_box.hpp"
#include "views/trophy_list_tab.hpp"
#include "views/add_host_tab.hpp"
#include "views/build_info_tab.hpp"
#include "views/config_view_tab.hpp"
#include "views/network_utilities_tab.hpp"
#include "views/stream_view.hpp"
#include "views/enter_pin_view.hpp"
#include "views/connection_view.hpp"
#include "views/settings_frame_view.hpp"
#include "views/setup_account_view.hpp"
#include "stream/session.hpp"
#include "core/settings_manager.hpp"
#include "core/update_manager.hpp"
#include "views/update_flow.hpp"
#include "core/thread_affinity.h"

#include <borealis/core/i18n.hpp>
using namespace brls::literals;

extern "C" {
#include <libavutil/log.h>
}

static void ffmpeg_log_callback(void*, int level, const char* fmt, va_list vl) {
    if (!SettingsManager::getInstance()->getDebugFfmpegLog())
        return;
    if (level > av_log_get_level())
        return;
    std::array<char, 512> buf{};
    vsnprintf(buf.data(), buf.size(), fmt, vl);
    std::string_view msg(buf.data());
    if (msg.ends_with('\n'))
        msg.remove_suffix(1);
    if (msg.empty())
        return;
    if (level <= AV_LOG_ERROR)
        brls::Logger::error("ffmpeg: {}", msg);
    else if (level <= AV_LOG_WARNING)
        brls::Logger::warning("ffmpeg: {}", msg);
    else
        brls::Logger::info("ffmpeg: {}", msg);
}

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
    akira::ui::setActiveTheme(SettingsManager::getInstance()->getUiTheme());
    akira::ui::applyToBorealis();
}

static void chiaki_to_brls_log(ChiakiLogLevel level, const char* msg, void* user)
{
    auto* settings = SettingsManager::getInstance();
    if (settings->isStreamingActive() && !settings->getDebugChiakiLog())
        return;

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
            brls::Logger::info("{}", msg);
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

class HeaderStatusView : public brls::Box
{
public:
    HeaderStatusView()
    {
        this->setAxis(brls::Axis::ROW);
        this->setAlignItems(brls::AlignItems::CENTER);

        brls::Platform* platform = brls::Application::getPlatform();

        auto* battery = new brls::BatteryWidget();
        battery->setVisibility(platform->canShowBatteryLevel() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        battery->setMarginRight(21);
        this->addView(battery);

        auto* wireless = new brls::WirelessWidget();
        wireless->setVisibility(platform->canShowWirelessLevel() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        wireless->setMarginRight(21);
        this->addView(wireless);

        timeLabel = new brls::Label();
        timeLabel->setVerticalAlign(brls::VerticalAlign::CENTER);
        timeLabel->setFontSize(21.5f);
        this->addView(timeLabel);
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override
    {
        auto in_time_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        auto tm        = *std::localtime(&in_time_t);
        std::stringstream ss;
        ss << std::put_time(&tm, "%H:%M:%S");
        if (ss.str() != lastTime)
        {
            lastTime = ss.str();
            timeLabel->setText(lastTime);
        }
        brls::Box::draw(vg, x, y, width, height, style, ctx);
    }

private:
    brls::Label* timeLabel = nullptr;
    std::string lastTime;
};

static void decorateAkiraHeader(brls::AppletFrame* appletFrame)
{
    if (!appletFrame)
        return;

    std::string version = getAppVersion();
    appletFrame->setTitle("Akira");
    if (!version.empty()) {
        if (auto* titleLabel = dynamic_cast<brls::Label*>(appletFrame->getView("brls/applet_frame/title_label"))) {
            titleLabel->setMarginTop(0);
            titleLabel->setFontSize(34);
            if (auto* titleBox = dynamic_cast<brls::Box*>(titleLabel->getParent())) {
                titleBox->setAlignItems(brls::AlignItems::BASELINE);

                auto* versionLabel = new brls::Label();
                versionLabel->setText(version);
                versionLabel->setFontSize(19);
                versionLabel->setTextColor(akira::ui::active().textDim);
                versionLabel->setMarginLeft(8);

                const auto& kids = titleBox->getChildren();
                size_t insertAt = kids.size();
                for (size_t i = 0; i < kids.size(); i++) {
                    if (kids[i] == titleLabel) {
                        insertAt = i + 1;
                        break;
                    }
                }
                titleBox->addView(versionLabel, insertAt);
            }
        }
    }

    auto* header = appletFrame->getHeader();
    if (header) {
        header->setAlignItems(brls::AlignItems::CENTER);

        auto* headerRight = new brls::Box();
        headerRight->setAxis(brls::Axis::COLUMN);
        headerRight->setJustifyContent(brls::JustifyContent::CENTER);
        headerRight->setAlignItems(brls::AlignItems::FLEX_END);
        headerRight->setGrow(1.0f);
        headerRight->setMarginRight(20);

        headerRight->addView(new HeaderStatusView());

        auto* attribution = new brls::Label();
        attribution->setText("akira/app/about"_i18n);
        attribution->setFontSize(14);
        attribution->setTextColor(akira::ui::active().textDim);
        attribution->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
        headerRight->addView(attribution);

        header->addView(headerRight);
    }

    auto* footer = appletFrame->getFooter();
    if (footer && !footer->getChildren().empty()) {
        if (auto* containerBox = dynamic_cast<brls::Box*>(footer->getChildren()[0])) {
            if (!containerBox->getChildren().empty()) {
                if (auto* rowBox = dynamic_cast<brls::Box*>(containerBox->getChildren()[0])) {
                    for (auto* child : rowBox->getChildren()) {
                        if (dynamic_cast<brls::Hints*>(child))
                            continue;
                        if (auto* box = dynamic_cast<brls::Box*>(child)) {
                            box->setVisibility(brls::Visibility::GONE);
                            break;
                        }
                    }
                }
            }
        }
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
            decorateAkiraHeader(appletFrame);

            auto* footer = appletFrame->getFooter();
            if (footer) {
                auto& outerChildren = footer->getChildren();
                if (!outerChildren.empty()) {
                    auto* containerBox = dynamic_cast<brls::Box*>(outerChildren[0]);
                    if (containerBox && !containerBox->getChildren().empty()) {
                        auto* rowBox = dynamic_cast<brls::Box*>(containerBox->getChildren()[0]);
                        if (rowBox) {
                            for (auto* child : rowBox->getChildren()) {
                                if (dynamic_cast<brls::Hints*>(child))
                                    continue;
                                if (auto* box = dynamic_cast<brls::Box*>(child)) {
                                    box->setVisibility(brls::Visibility::GONE);
                                    break;
                                }
                            }

                            auto* ipLabel = new brls::Label();
                            ipLabel->setText("akira/app/ip_prefix"_i18n + getLocalIpAddress());
                            ipLabel->setFontSize(18);
                            ipLabel->setTextColor(akira::ui::active().textDim);
                            ipLabel->setVerticalAlign(brls::VerticalAlign::CENTER);
                            rowBox->addView(ipLabel);
                        }
                    }
                }
            }
        }

        this->registerAction("akira/tabs/trophies"_i18n, brls::ControllerButton::BUTTON_LB, [](brls::View*) {
            if (!SettingsManager::getInstance()->getActiveProfileTrophiesEnabled()) {
                brls::Application::notify("akira/trophies/disabled_for_profile"_i18n);
                return true;
            }
            auto* view = TrophyListTab::create();
            view->registerAction("akira/common/back"_i18n, brls::ControllerButton::BUTTON_B, [](brls::View*) {
                brls::Application::popActivity();
                return true;
            }, false);
            auto* frame = new brls::AppletFrame(view);
            decorateAkiraHeader(frame);
            brls::Application::pushActivity(new brls::Activity(frame));
            return true;
        }, false);

        this->registerAction("akira/tabs/settings"_i18n, brls::ControllerButton::BUTTON_RB, [](brls::View*) {
            if (SettingsManager::getInstance()->getProfiles().empty()) {
                auto* setupFrame = new brls::AppletFrame(new SetupAccountView());
                decorateAkiraHeader(setupFrame);
                brls::Application::pushActivity(new brls::Activity(setupFrame));
                return true;
            }
            auto* frame = new brls::AppletFrame(new SettingsFrameView());
            decorateAkiraHeader(frame);
            brls::Application::pushActivity(new brls::Activity(frame));
            return true;
        }, false);

        akira::UpdateFlow::checkOnLaunch();
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

    akira::UpdateManager::setSelfPath(argc > 0 && argv[0] ? argv[0] : "");

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

    av_log_set_callback(ffmpeg_log_callback);

    std::string overrideLocale = SettingsManager::getInstance()->getDebugLocale();
    if (!overrideLocale.empty()) {
        brls::Platform::APP_LOCALE_DEFAULT = overrideLocale;
    } else {
        brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_AUTO;
    }

    initCustomTheme();

    if (!brls::Application::init())
    {
        brls::Logger::error("Unable to init Borealis application");
        return EXIT_FAILURE;
    }

    brls::getStyle().addMetric("brls/tab_frame/sidebar_width", 369.0f);
    brls::getStyle().addMetric("brls/sidebar/item_font_size", 24.0f);

    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0)
    {
        brls::Logger::error("SDL_Init failed: {}", SDL_GetError());
        return EXIT_FAILURE;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    ChiakiErrorCode err = chiaki_lib_init();
    if (err != CHIAKI_ERR_SUCCESS)
    {
        brls::Logger::error("Chiaki lib init failed: {}", chiaki_error_string(err));
        return EXIT_FAILURE;
    }

    if (SettingsManager::getInstance()->getEnableThreadAffinity()) {
        chiaki_thread_affinity_init();
        akira_thread_set_affinity(AKIRA_THREAD_NAME_MAIN);
        brls::Logger::info("Thread affinity enabled");
    }

    static ChiakiLog chiakiLog;
#ifdef MUTE_CHIAKI_LOGS
    chiaki_log_init(&chiakiLog, 0, chiaki_to_brls_log, nullptr);
#else
    chiaki_log_init(&chiakiLog, CHIAKI_LOG_ALL, chiaki_to_brls_log, nullptr);
#endif
    SettingsManager::getInstance()->setLogger(&chiakiLog);
    Session::GetInstance()->SetLogger(&chiakiLog);

    static FILE* logFile = nullptr;
    if (SettingsManager::getInstance()->getEnableFileLogging()) {
        std::string logPath = SettingsManager::getLogFilePath();
        logFile = fopen(logPath.c_str(), "w");
        if (logFile) {
            brls::Logger::setLogOutput(logFile);
            brls::Logger::info("File logging enabled: {}", logPath);
        }
    }

    brls::Logger::setAsyncLogging(true);
    brls::Application::getRunLoopEvent()->subscribe([]() {
        brls::async([]() {
            brls::Logger::flushAsyncLogs();
        }, true);
    });
    brls::Logger::info("Async logging enabled via thread pool");

    chiaki_libnx_set_ghash_mode(CHIAKI_LIBNX_GHASH_PMULL);
    brls::Logger::info("GHASH mode: PMULL");

    brls::Logger::info("Chiaki library initialized");

    AppletType appletType = appletGetAppletType();
    brls::Logger::info("Applet type: {} ({})", appletTypeToString(appletType), static_cast<int>(appletType));

    Session::GetInstance()->SetMesaConfig();

    brls::Application::registerXMLView("HostListTab", HostListTab::create);
    brls::Application::registerXMLView("PsnGatedBox", PsnGatedBox::create);
    brls::Application::registerXMLView("RecyclingGrid", RecyclingGrid::create);
    brls::Application::registerXMLView("TrophyListTab", TrophyListTab::create);
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
    curl_global_cleanup();

    nvExit();

    return EXIT_SUCCESS;
}
