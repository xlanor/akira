#include "cloud/cloud_connect_task.hpp"

#include <borealis.hpp>
#include <borealis/core/i18n.hpp>

#include "cloud/service.hpp"
#include "core/host.hpp"
#include "core/settings_manager.hpp"

#include <cstdio>
#include <string>

using namespace brls::literals;

namespace cloud {

namespace {

bool parseProvisionStep(const std::string& stage, int& index, int& total, std::string& label)
{
    static const std::string marker = " - Step ";
    const std::size_t at = stage.rfind(marker);
    if (at == std::string::npos)
        return false;

    int parsedIndex = 0;
    int parsedTotal = 0;
    if (std::sscanf(stage.c_str() + at + marker.size(), "%d of %d",
                    &parsedIndex, &parsedTotal) != 2)
        return false;
    if (parsedIndex <= 0 || parsedTotal <= 0)
        return false;

    index = parsedIndex;
    total = parsedTotal;
    label = stage.substr(0, at);
    return true;
}

} // namespace

CloudConnectTask::CloudConnectTask(const Game& game, bool skipAttr)
    : game(game), skipAttr(skipAttr)
{
}

CloudConnectTask::~CloudConnectTask()
{
    cancel();
}

std::string CloudConnectTask::title() const
{
    return brls::getStr("akira/connection/connecting_to", game.name);
}

void CloudConnectTask::start(akira::views::ConnectSink& s)
{
    {
        std::lock_guard<std::mutex> lock(callbackState->mutex);
        callbackState->sink = &s;
        callbackState->cancelled.store(false);
    }
    s.progressStep(0, 0, "");
    provision();
}

void CloudConnectTask::cancel()
{
    callbackState->cancelled.store(true);
    std::lock_guard<std::mutex> lock(callbackState->mutex);
    callbackState->sink = nullptr;
}

void CloudConnectTask::provision()
{
    auto state = callbackState;

    Service::instance().launchGame(
        game,
        [state](std::shared_ptr<Host> host) {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->cancelled.load() && state->sink)
                state->sink->succeeded(host.get(), host);
        },
        [state](const std::string& error) {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->cancelled.load() && state->sink)
                state->sink->failed(error);
        },
        [state](const std::string& stage) {
            if (stage.empty())
                return;

            brls::Logger::info("{}", stage);

            int index = 0;
            int total = 0;
            std::string label;
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->cancelled.load() || !state->sink)
                return;
            if (parseProvisionStep(stage, index, total, label))
                state->sink->progressStep(index, total, label);
            else
                state->sink->progressStep(0, 0, stage);
        },
        skipAttr,
        [state]() { return state->cancelled.load(); });
}

bool CloudConnectTask::presentFailure(const std::string& error,
                                      akira::views::ConnectSink& s)
{
    /*
     * Only the privacy check is worth offering a way past - it is a choice the
     * account holder is allowed to make, and the service will honour it on a
     * second attempt. Every other failure is the shared dialog's business.
     */
    const std::string privacyIntro = "akira/cloud/launch_privacy"_i18n;
    if (error.rfind(privacyIntro, 0) != 0)
        return false;

    auto* dialog = new brls::Dialog(error);

    dialog->addButton("akira/cloud/try_anyway"_i18n, [this, dialog]() {
        dialog->close();
        SettingsManager::getInstance()->setCloudAttrPassed(true);
        SettingsManager::getInstance()->writeFile();
        skipAttr = true;
        provision();
    });

    dialog->addButton("akira/common/ok"_i18n, [dialog]() {
        dialog->close();
        brls::Application::popActivity();
    });

    dialog->open();
    (void)s;
    return true;
}

} // namespace cloud
