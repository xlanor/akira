#include "psn/token_refresher.hpp"

#include <chrono>
#include <ctime>

#include <switch.h>

#include <borealis.hpp>

#include "psn/auth.hpp"
#include "util/http.hpp"

namespace psn {

static bool hasConnectivity()
{
    NifmInternetConnectionType type = NifmInternetConnectionType_WiFi;
    u32 strength = 0;
    NifmInternetConnectionStatus status = NifmInternetConnectionStatus_ConnectingUnknown1;

    Result rc = nifmGetInternetConnectionStatus(&type, &strength, &status);
    if (R_FAILED(rc))
        return false;

    return status == NifmInternetConnectionStatus_Connected;
}

TokenRefresher& TokenRefresher::instance()
{
    static TokenRefresher* refresher = new TokenRefresher();
    return *refresher;
}

TokenRefresher::~TokenRefresher()
{
    stop();
}

void TokenRefresher::start()
{
    if (threadStarted)
        return;

    running = true;
    if (chiaki_thread_create(&thread, threadFunc, this) != CHIAKI_ERR_SUCCESS)
    {
        running = false;
        brls::Logger::error("PSN refresher: failed to start thread");
        return;
    }

    threadStarted = true;
    brls::Logger::info("PSN refresher: started (interval {}s, window {}s)",
        INTERVAL_SECONDS, WINDOW_SECONDS);
}

void TokenRefresher::stop()
{
    if (!threadStarted)
        return;

    {
        std::lock_guard<std::mutex> lock(mutex);
        running = false;
    }
    cond.notify_all();

    chiaki_thread_join(&thread, nullptr);
    threadStarted = false;
}

void TokenRefresher::refreshNow(std::function<void()> onDone)
{
    brls::Logger::info("PSN refresher: manual refresh requested");
    {
        std::lock_guard<std::mutex> lock(mutex);
        pendingDone = std::move(onDone);
    }
    forceNow = true;
    cond.notify_all();
}

void* TokenRefresher::threadFunc(void* user)
{
    static_cast<TokenRefresher*>(user)->run();
    return nullptr;
}

void TokenRefresher::run()
{
    HttpSession session;

    tick(session, false);

    while (true)
    {
        bool force;
        {
            std::unique_lock<std::mutex> lock(mutex);
            cond.wait_for(lock, std::chrono::seconds(INTERVAL_SECONDS),
                [this]() { return !running || forceNow.load(); });

            if (!running)
                break;

            force = forceNow.exchange(false);
        }

        tick(session, force);

        std::function<void()> done;
        {
            std::lock_guard<std::mutex> lock(mutex);
            done.swap(pendingDone);
        }
        if (done)
            brls::sync(std::move(done));
    }

    brls::Logger::info("PSN refresher: thread stopped");
}

void TokenRefresher::tick(HttpSession& session, bool force)
{
    if (!hasConnectivity())
    {
        brls::Logger::debug("PSN refresher: no connectivity, skipping tick");
        return;
    }

    struct Target {
        Auth& auth;
        const char* name;
    };

    Target targets[] = {
        {Auth::instance(), "remote-play"},
        {Auth::mobile(), "mobile-sso"},
    };

    for (Target& target : targets)
    {
        if (target.auth.state() == SessionState::NotLinked)
            continue;

        int64_t secs = target.auth.secondsUntilExpiry();

        if (!force && !target.auth.needsProactiveRefresh(WINDOW_SECONDS))
        {
            brls::Logger::debug("PSN refresher: {} valid for {}s, skipping", target.name, secs);
            continue;
        }

        brls::Logger::info("PSN refresher: refreshing {} (expires in {}s, force={})",
            target.name, secs, force);

        Error err = target.auth.ensureSession(session, true);
        if (err.ok())
            brls::Logger::info("PSN refresher: {} refreshed", target.name);
        else
            brls::Logger::warning("PSN refresher: {} refresh failed: {}", target.name, err.message);
    }
}

}  // namespace psn
