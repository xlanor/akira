#include "views/host_connect_task.hpp"

#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include <borealis.hpp>
#include <borealis/core/i18n.hpp>

#include "core/host.hpp"
#include "psn/auth.hpp"
#include "core/thread_affinity.h"

using namespace brls::literals;

namespace akira::views {

HostConnectTask::HostConnectTask(Host* host)
    : host(host)
{
}

HostConnectTask::~HostConnectTask()
{
    /* The view hands the task back before it tears itself down, so joining here
     * is what guarantees the worker is not still holding a sink that is about
     * to stop existing. */
    if (running.load())
        host->cancelHolepunch();

    if (threadStarted.load())
        chiaki_thread_join(&thread, nullptr);

    host->setOnHolepunchPhase(nullptr);
}

std::string HostConnectTask::title() const
{
    return brls::getStr("akira/connection/connecting_to", host->getHostName());
}

const char* HostConnectTask::logKind() const
{
    return host->isRemote() ? "remote" : "local";
}

void HostConnectTask::cancel()
{
    if (running.load())
        host->cancelHolepunch();
    running.store(false);
}

void HostConnectTask::start(ConnectSink& s)
{
    sink = &s;
    running.store(true);

    host->setOnHolepunchPhase([this](HolepunchPhase phase) {
        if (!sink)
            return;
        sink->progress(phase == HolepunchPhase::Punching ? ConnectionStage::Linking
                                                         : ConnectionStage::Finding);
    });

    const ChiakiErrorCode err = chiaki_thread_create(&thread, threadFunc, this);
    if (err != CHIAKI_ERR_SUCCESS) {
        running.store(false);
        sink->failed("akira/connection/failed_create_thread"_i18n);
        return;
    }

    threadStarted.store(true);
}

void* HostConnectTask::threadFunc(void* user)
{
    akira_thread_set_affinity(AKIRA_THREAD_NAME_CONNECTION);
    static_cast<HostConnectTask*>(user)->run();
    return nullptr;
}

void HostConnectTask::run()
{
    brls::Logger::info("Connection thread started");

    if (!host->isRemote()) {
        brls::Logger::info("Local connection - ready to stream");
        running.store(false);
        sink->succeeded(host, {});
        return;
    }

    if (!psn::Auth::instance().tokenValid()) {
        brls::Logger::info("PSN token expired at connect; attempting refresh before holepunch");

        std::promise<bool> refreshDone;
        auto refreshFuture = refreshDone.get_future();

        psn::Auth::instance().refresh(
            [&refreshDone]() { refreshDone.set_value(true); },
            [&refreshDone](psn::AuthError, const std::string&) { refreshDone.set_value(false); });

        const bool refreshed =
            refreshFuture.wait_for(std::chrono::seconds(30)) == std::future_status::ready
            && refreshFuture.get();

        if (!refreshed || !psn::Auth::instance().tokenValid()) {
            running.store(false);
            sink->failed("akira/connection/psn_token_expired"_i18n);
            return;
        }

        brls::Logger::info("PSN token refreshed; continuing with holepunch");
    }

    brls::Logger::info("Initiating holepunch connection...");

    const ChiakiErrorCode err = host->connectHolepunch();
    if (err != CHIAKI_ERR_SUCCESS) {
        host->cleanupHolepunch();
        running.store(false);
        sink->failed(brls::getStr("akira/connection/holepunch_failed", chiaki_error_string(err)));
        return;
    }

    brls::Logger::info("CTRL holepunch successful! Waiting for PS5 to be ready...");

    /*
     * A PS5 coming out of rest shows "press PS button" and cannot accept the
     * DATA holepunch channel yet. Without this wait that channel times out
     * after about fifteen seconds and the session never establishes.
     */
    for (int i = WAKEUP_WAIT_SECONDS; i > 0; i--) {
        if (!running.load() || sink->cancelled())
            return;
        brls::Logger::info("PS5 waking up... {} seconds remaining", i);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    brls::Logger::info("Wait complete, transitioning to StreamView...");

    running.store(false);
    sink->succeeded(host, {});
}

} // namespace akira::views
