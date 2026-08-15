#include "cloud/cloud_connection_view.hpp"

#include "cloud/service.hpp"
#include "core/host.hpp"
#include "core/settings_manager.hpp"
#include "ui/theme.hpp"
#include "util/shared_view_holder.hpp"
#include "views/connection_stage.hpp"
#include "views/stream_view.hpp"

#include <borealis/core/i18n.hpp>

#include <cstdio>

using namespace brls::literals;

namespace cloud {

CloudConnectionView::CloudConnectionView(const Game& game, bool skipAttr)
    : game(game), skipAttr(skipAttr)
{
    this->inflateFromXMLRes("xml/views/connection_view.xml");

    stageText = "akira/cloud/launch_starting"_i18n;

    auto* titleLabel = (brls::Label*)this->getView("connection/title");
    titleLabel->setText(brls::getStr("akira/connection/connecting_to", this->game.name));
    titleLabel->setMarginLeft(14);

    auto* titleRow = (brls::Box*)this->getView("connection/titleRow");
    auto* spinner = new brls::ProgressSpinner();
    spinner->setWidth(28);
    spinner->setHeight(28);
    titleRow->addView(spinner, 0);

    setFocusable(true);
}

CloudConnectionView::~CloudConnectionView()
{
    brls::Logger::unsubscribeFromLog(logSubscription);
    SharedViewHolder::release(this);
}

void CloudConnectionView::addLogLine(const std::string& line)
{
    std::lock_guard<std::mutex> lock(logMutex);
    logLines.push_back(line);
    while (logLines.size() > MAX_LOG_LINES)
        logLines.pop_front();
}

void CloudConnectionView::renderLogs(NVGcontext* vg, float x, float y, float width, float height)
{
    std::lock_guard<std::mutex> lock(logMutex);

    nvgSave(vg);
    nvgScissor(vg, x, y, width, height);
    nvgFontSize(vg, 16);
    nvgFillColor(vg, akira::ui::active().textMuted);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

    float padding = 20;
    float maxTextWidth = width - padding * 2;
    float availableHeight = height - padding * 2;

    size_t startIdx = logLines.size();
    float totalHeight = 0;
    while (startIdx > 0) {
        float bounds[4];
        nvgTextBoxBounds(vg, 0, 0, maxTextWidth, logLines[startIdx - 1].c_str(), nullptr, bounds);
        float lineH = bounds[3] - bounds[1];
        if (totalHeight + lineH > availableHeight)
            break;
        totalHeight += lineH;
        startIdx--;
    }

    float currentY = y + padding;
    for (size_t i = startIdx; i < logLines.size(); i++) {
        nvgTextBox(vg, x + padding, currentY, maxTextWidth, logLines[i].c_str(), nullptr);
        float bounds[4];
        nvgTextBoxBounds(vg, x + padding, currentY, maxTextWidth, logLines[i].c_str(), nullptr, bounds);
        currentY += (bounds[3] - bounds[1]);
    }

    nvgRestore(vg);
}

void CloudConnectionView::setupAndStart()
{
    auto weak = weak_from_this();

    auto* cancelBtn = (brls::Button*)this->getView("connection/cancel");
    cancelBtn->registerClickAction([weak](brls::View*) {
        if (auto self = weak.lock()) {
            if (self->settled)
                return true;
            self->settled = true;
            brls::sync([]() { brls::Application::popActivity(); });
        }
        return true;
    });

    logSubscription = brls::Logger::subscribeToLog(
        [weak](brls::Logger::TimePoint, brls::LogLevel, std::string msg) {
            if (auto self = weak.lock())
                self->addLogLine(msg);
        });

    startProvision();
}

void CloudConnectionView::startProvision()
{
    auto weak = weak_from_this();

    Service::instance().launchGame(game,
        [weak](std::shared_ptr<Host> host) {
            if (auto self = weak.lock())
                self->onProvisionSuccess(host);
        },
        [weak](const std::string& error) {
            if (auto self = weak.lock())
                self->onProvisionError(error);
        },
        [weak](const std::string& stage) {
            if (stage.empty())
                return;
            brls::sync([weak, stage]() {
                if (auto self = weak.lock())
                    self->onProvisionProgress(stage);
            });
        },
        skipAttr);
}

void CloudConnectionView::onProvisionProgress(const std::string& stage)
{
    if (settled)
        return;

    size_t sp = stage.find("Step ");
    if (sp != std::string::npos) {
        int idx = 0;
        int total = 0;
        if (std::sscanf(stage.c_str() + sp, "Step %d of %d", &idx, &total) == 2 && total > 0) {
            stageIndex = idx;
            stageTotal = total;
        }
        size_t dash = stage.rfind(" - ", sp);
        stageText = (dash != std::string::npos) ? stage.substr(0, dash) : stage.substr(0, sp);
    } else {
        stageText = stage;
    }
}

void CloudConnectionView::onProvisionSuccess(std::shared_ptr<Host> host)
{
    if (settled)
        return;
    settled = true;

    auto streamView = SharedViewHolder::holdNew<StreamView>(host.get(), host);
    streamView->setupCallbacks();

    brls::sync([streamView]() {
        brls::Application::popActivity();
        brls::Application::pushActivity(new brls::Activity(streamView.get()));
        streamView->startStream();
    });
}

void CloudConnectionView::onProvisionError(const std::string& error)
{
    if (settled)
        return;

    auto weak = weak_from_this();

    auto* dialog = new brls::Dialog(error);

    std::string privacyIntro = "akira/cloud/launch_privacy"_i18n;
    if (error.rfind(privacyIntro, 0) == 0) {
        dialog->addButton("akira/cloud/try_anyway"_i18n, [weak, dialog]() {
            dialog->close();
            SettingsManager::getInstance()->setCloudAttrPassed(true);
            SettingsManager::getInstance()->writeFile();
            if (auto self = weak.lock()) {
                self->stageText = "akira/cloud/launch_starting"_i18n;
                self->skipAttr = true;
                self->startProvision();
            }
        });
        dialog->addButton("akira/common/ok"_i18n, [weak, dialog]() {
            dialog->close();
            if (auto self = weak.lock()) {
                self->settled = true;
                brls::Application::popActivity();
            }
        });
    } else {
        dialog->addButton("akira/common/ok"_i18n, [weak, dialog]() {
            dialog->close();
            if (auto self = weak.lock()) {
                self->settled = true;
                brls::Application::popActivity();
            }
        });
    }

    dialog->open();
}

void CloudConnectionView::draw(NVGcontext* vg, float x, float y, float width, float height,
                               brls::Style style, brls::FrameContext* ctx)
{
    Box::draw(vg, x, y, width, height, style, ctx);

    auto* logArea = this->getView("connection/logArea");
    if (!logArea)
        return;

    if (!SettingsManager::getInstance()->getConnectionShowStages())
    {
        renderLogs(vg, logArea->getX(), logArea->getY(),
                   logArea->getWidth(), logArea->getHeight());
        return;
    }

    float cx = logArea->getX() + logArea->getWidth() / 2.0f;
    float cy = logArea->getY() + logArea->getHeight() / 2.0f;
    drawConnectionRing(vg, cx, cy, stageText, stageIndex, stageTotal);
}

} // namespace cloud
