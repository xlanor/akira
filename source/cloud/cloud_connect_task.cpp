#include "cloud/cloud_connect_task.hpp"

#include <borealis.hpp>
#include <borealis/core/i18n.hpp>

#include "cloud/service.hpp"
#include "core/host.hpp"
#include "core/settings_manager.hpp"

using namespace brls::literals;

namespace cloud {

CloudConnectTask::CloudConnectTask(const Game& game, bool skipAttr)
    : game(game), skipAttr(skipAttr)
{
}

std::string CloudConnectTask::title() const
{
    return brls::getStr("akira/connection/connecting_to", game.name);
}

void CloudConnectTask::start(akira::views::ConnectSink& s)
{
    sink = &s;
    provision();
}

void CloudConnectTask::provision()
{
    auto* s = sink;

    Service::instance().launchGame(
        game,
        [s](std::shared_ptr<Host> host) { s->succeeded(host.get(), host); },
        [s](const std::string& error) { s->failed(error); },
        [s](const std::string& stage) {
            /*
             * The service narrates in prose. Its lines land in the log like
             * everything else, and the connecting view reads its stage from
             * there - so nothing has to translate "Step 3 of 5" into a stage
             * twice.
             */
            if (!stage.empty())
                brls::Logger::info("{}", stage);
        },
        skipAttr);
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
