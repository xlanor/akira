#include "views/pair_view.hpp"

#include "cloud/service.hpp"
#include "core/settings_manager.hpp"
#include "core/trophy_manager.hpp"
#include "views/host_list_tab.hpp"

#include <borealis/core/i18n.hpp>

#include <cstdio>
#include <string>

using namespace brls::literals;
using namespace akira::pair;

namespace {

std::string spaced(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); i++) {
        if (i > 0)
            out += "  ";
        out += s[i];
    }
    return out;
}

const char* statusKey(ListenerEvent event) {
    switch (event) {
        case ListenerEvent::Listening:
            return "akira/pair/status_waiting";
        case ListenerEvent::ClientConnected:
            return "akira/pair/status_connecting";
        case ListenerEvent::Imported:
            return "akira/pair/status_imported";
        case ListenerEvent::BadCode:
            return "akira/pair/status_bad_code";
        case ListenerEvent::LockedOut:
            return "akira/pair/status_locked";
        case ListenerEvent::TimedOut:
            return "akira/pair/status_timed_out";
        case ListenerEvent::Error:
        default:
            return "akira/pair/status_error";
    }
}

}

PairView::PairView(bool createProfile) : createProfile(createProfile) {
    this->inflateFromXMLRes("xml/views/pair_view.xml");

    codeLabel = dynamic_cast<brls::Label*>(this->getView("pair/code"));
    addrIpLabel = dynamic_cast<brls::Label*>(this->getView("pair/addrIp"));
    addrPortLabel = dynamic_cast<brls::Label*>(this->getView("pair/addrPort"));
    timerLabel = dynamic_cast<brls::Label*>(this->getView("pair/timer"));
    statusLabel = dynamic_cast<brls::Label*>(this->getView("pair/status"));
    closeBtn = dynamic_cast<brls::Button*>(this->getView("pair/close"));

    if (closeBtn) {
        closeBtn->registerClickAction([](brls::View*) {
            brls::Application::popActivity();
            return true;
        });
    }

    this->registerAction("akira/common/back"_i18n, brls::ControllerButton::BUTTON_B,
        [](brls::View*) {
            brls::Application::popActivity();
            return true;
        });

    setFocusable(true);

    listenPort = SettingsManager::getInstance()->getCompanionPort();
    std::string ip = PairListener::localIpv4();
    if (addrIpLabel) {
        addrIpLabel->setText(ip.empty() ? "-" : ip);
        addrIpLabel->setTextColor(nvgRGB(0x5c, 0xc8, 0xff));
    }
    if (addrPortLabel) {
        addrPortLabel->setText(brls::getStr("akira/pair/port_fmt", listenPort));
        addrPortLabel->setTextColor(nvgRGB(0xf2, 0xb0, 0x4a));
    }

    advertiser.start(listenPort);
    startListening();
}

PairView::~PairView() {
    *alive = false;
    advertiser.stop();
    listener.stop();
}

brls::View* PairView::create() {
    return new PairView();
}

void PairView::startListening() {
    std::string code = PairListener::generateCode();
    if (codeLabel)
        codeLabel->setText(spaced(code));

    windowStart = std::chrono::steady_clock::now();
    lastShownSecond = -1;
    counting = true;

    auto guard = alive;
    listener.start(
        listenPort, code,
        [this, guard](ListenerEvent event) {
            brls::sync([this, guard, event]() {
                if (!*guard)
                    return;
                this->onEvent(event);
            });
        },
        [guard, create = createProfile](const PairedCredentials& creds) {
            PairedCredentials copy = creds;
            brls::sync([guard, copy, create]() {
                if (!*guard)
                    return;
                PairView::applyCredentials(copy, create);
            });
        });
}

void PairView::draw(NVGcontext* vg, float x, float y, float width, float height,
                    brls::Style style, brls::FrameContext* ctx) {
    if (counting && timerLabel) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::steady_clock::now() - windowStart)
                           .count();
        int remaining = PairListener::kWindowSeconds - static_cast<int>(elapsed);
        if (remaining < 0)
            remaining = 0;
        if (remaining != lastShownSecond) {
            lastShownSecond = remaining;
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%d:%02d", remaining / 60, remaining % 60);
            timerLabel->setText(brls::getStr("akira/pair/timer_fmt", std::string(buf)));
        }
    }

    Box::draw(vg, x, y, width, height, style, ctx);
}

void PairView::onEvent(ListenerEvent event) {
    if (event == ListenerEvent::TimedOut) {
        startListening();
        return;
    }

    if (event == ListenerEvent::Imported) {
        brls::Application::popActivity();
        return;
    }

    if (event == ListenerEvent::LockedOut) {
        counting = false;
        if (timerLabel)
            timerLabel->setText("");
    }

    if (statusLabel)
        statusLabel->setText(brls::getStr(statusKey(event)));
}

void PairView::applyCredentials(const PairedCredentials& creds, bool createProfile) {
    SettingsManager* settings = SettingsManager::getInstance();

    if (createProfile) {
        int64_t id = settings->addProfile(Profile{});
        settings->setActiveProfileId(id);
    }

    if (!creds.onlineId.empty())
        settings->setPsnOnlineId(nullptr, creds.onlineId);
    if (!creds.accountId.empty())
        settings->setPsnAccountId(nullptr, creds.accountId);
    if (!creds.accessToken.empty())
        settings->setPsnAccessToken(creds.accessToken);
    if (!creds.refreshToken.empty())
        settings->setPsnRefreshToken(creds.refreshToken);
    if (creds.expiresAt > 0)
        settings->setPsnTokenExpiresAt(creds.expiresAt);
    if (!creds.npsso.empty())
        settings->setPsnNpsso(creds.npsso);
    if (!creds.duid.empty())
        settings->setGlobalDuid(creds.duid);

    if (creds.hasMobile) {
        settings->setPsnMobileSsoAccessToken(creds.mobileAccessToken);
        settings->setPsnMobileSsoRefreshToken(creds.mobileRefreshToken);
        if (creds.mobileExpiresAt > 0)
            settings->setPsnMobileSsoExpiresAt(creds.mobileExpiresAt);
    }

    settings->writeFile();
    brls::Logger::info("Imported PSN credentials from pairing push");

    TrophyManager::getInstance()->onActiveProfileChanged();
    /*
     * A new token can mean a different account behind the same profile, and the
     * cached catalog carries the previous one's owned games.
     */
    cloud::Service::instance().clearCatalogCache();
    cloud::Service::instance().markActiveProfileDirty();
    cloud::Service::instance().refreshActiveProfile(true);
    HostListTab::notifyActiveProfileChanged();
}
