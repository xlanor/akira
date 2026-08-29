#include "views/connecting_view.hpp"

#include <chrono>
#include <ctime>
#include <format>

#include <borealis/core/i18n.hpp>

#include "core/host.hpp"
#include "core/settings_manager.hpp"
#include "ui/theme.hpp"
#include "util/shared_view_holder.hpp"
#include "views/stream_view.hpp"

using namespace brls::literals;

namespace akira::views {

ConnectingView::ConnectingView(std::unique_ptr<ConnectTask> task)
    : task(std::move(task))
{
    this->inflateFromXMLRes("xml/views/connection_view.xml");

    auto* titleLabel = (brls::Label*)this->getView("connection/title");
    titleLabel->setText(this->task->title());
    titleLabel->setMarginLeft(14);

    auto* titleRow = (brls::Box*)this->getView("connection/titleRow");
    auto* spinner = new brls::ProgressSpinner();
    spinner->setWidth(28);
    spinner->setHeight(28);
    titleRow->addView(spinner, 0);

    setFocusable(true);

    /*
     * Swallowed, not left to fall through.
     *
     * L and R open the trophy and settings tabs from the home screen, and this
     * screen sits above it with a stream half-built underneath. Registering
     * them here means they stop at the top of the stack whatever else is on it.
     */
    registerAction("", brls::ControllerButton::BUTTON_LB, [](brls::View*) { return true; }, false);
    registerAction("", brls::ControllerButton::BUTTON_RB, [](brls::View*) { return true; }, false);

    registerAction("akira/common/cancel"_i18n, brls::ControllerButton::BUTTON_B,
                   [this](brls::View*) {
                       cancelFromUser();
                       return true;
                   }, false);
}

ConnectingView::~ConnectingView()
{
    brls::Logger::unsubscribeFromLog(logSubscription);

    if (!settled.load())
        task->cancel();

    task.reset();

    restoreMainLog();
    SharedViewHolder::release(this);
}

void ConnectingView::setupAndStart()
{
    auto weak = weak_from_this();

    auto* cancelBtn = (brls::Button*)this->getView("connection/cancel");
    if (cancelBtn) {
        cancelBtn->registerClickAction([weak](brls::View*) {
            if (auto self = weak.lock())
                self->cancelFromUser();
            return true;
        });
    }

    logSubscription = brls::Logger::subscribeToLog(
        [weak](brls::Logger::TimePoint, brls::LogLevel, std::string msg) {
            if (auto self = weak.lock())
                self->addLogLine(msg);
        });

    switchToConnectionLog();

    brls::Logger::info("========================================");
    brls::Logger::info("CONNECTION ATTEMPT: {} to {}", task->logKind(), task->title());
    brls::Logger::info("========================================");

    addLogLine("akira/connection/starting"_i18n);

    task->start(*this);
}

void ConnectingView::cancelFromUser()
{
    if (settled.exchange(true))
        return;

    brls::Logger::info("Connection cancelled by user");
    wasCancelled.store(true);
    task->cancel();

    brls::sync([]() { brls::Application::popActivity(); });
}

void ConnectingView::addLogLine(const std::string& line)
{
    {
        std::lock_guard<std::mutex> lock(logMutex);

        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()).count() % 1000;
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm time_tm = *std::localtime(&tt);

        logLines.push_back(std::format("{:02}:{:02}:{:02}.{:03} {}",
                                       time_tm.tm_hour, time_tm.tm_min, time_tm.tm_sec,
                                       static_cast<int>(ms), line));

        while (logLines.size() > MAX_LOG_LINES)
            logLines.pop_front();
    }

    /*
     * Read out of the log rather than reported by hand. A route that says
     * nothing about its stages still gets a ring, because the lines it writes
     * on the way through name the stages anyway.
     */
    currentStage.store(static_cast<int>(
        matchConnectionStage(line, static_cast<ConnectionStage>(currentStage.load()))));
}

void ConnectingView::switchToConnectionLog()
{
    if (!SettingsManager::getInstance()->getEnableFileLogging())
        return;

    const std::string logPath = SettingsManager::getConnectionLogFilePath(task->logKind());

    FILE* newLogFile = fopen(logPath.c_str(), "w");
    if (newLogFile) {
        prevLogOutput = brls::Logger::getLogOutput();
        connectionLogFile = newLogFile;
        brls::Logger::setLogOutput(newLogFile);
        brls::Logger::info("Switched to connection log: {}", logPath);
    }
}

void ConnectingView::restoreMainLog()
{
    if (!connectionLogFile)
        return;

    brls::Logger::flushAsyncLogs();
    brls::Logger::setLogOutput(prevLogOutput ? prevLogOutput : stdout);
    brls::Logger::flushAsyncLogs();
    fclose(connectionLogFile);
    connectionLogFile = nullptr;
    prevLogOutput = nullptr;
}

void ConnectingView::progress(ConnectionStage stage)
{
    currentStage.store(static_cast<int>(stage));
}

bool ConnectingView::cancelled() const
{
    return wasCancelled.load();
}

void ConnectingView::succeeded(Host* host, std::shared_ptr<Host> owner)
{
    if (settled.exchange(true))
        return;

    brls::Logger::info("Transitioning to stream view...");

    auto streamView = SharedViewHolder::holdNew<StreamView>(host, owner);
    streamView->setupCallbacks();

    /*
     * Popped and pushed in the same callback, and this is the moment the home
     * screen used to take focus back: popping makes it briefly the top activity,
     * its onResume fires, and the callback it queues runs a frame later - by
     * which time the stream view, and often the controller picker, are above it.
     */
    brls::sync([streamView]() {
        brls::Application::popActivity();
        brls::Application::pushActivity(new brls::Activity(streamView.get()));
        streamView->startStream();
    });
}

void ConnectingView::failed(const std::string& error)
{
    if (settled.load())
        return;

    brls::Logger::error("{}", error);

    if (task->presentFailure(error, *this))
        return;

    /*
     * Named by stage when the ring is what is on screen, because "it failed
     * while linking" is worth more than the last error string the stack
     * produced - and the ring is already showing which stage that was.
     */
    if (SettingsManager::getInstance()->getConnectionShowStages()) {
        const auto stage = static_cast<ConnectionStage>(currentStage.load());
        failureText = brls::getStr(connectionFailureKeyForStage(stage));
        showFailure.store(true);
        return;
    }

    settled.store(true);

    brls::sync([error]() {
        auto* dialog = new brls::Dialog(brls::getStr("akira/connection/connection_failed", error));
        dialog->addButton("akira/common/ok"_i18n, []() {
            brls::Application::popActivity();
        });
        dialog->open();
    });
}

void ConnectingView::renderLogs(NVGcontext* vg, float x, float y, float width, float height)
{
    std::lock_guard<std::mutex> lock(logMutex);

    nvgSave(vg);
    nvgScissor(vg, x, y, width, height);

    nvgFontSize(vg, 16);
    nvgFillColor(vg, akira::ui::active().textMuted);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

    const float padding = 20;
    const float maxTextWidth = width - padding * 2;
    const float availableHeight = height - padding * 2;

    /* Fitted from the bottom up: the newest line is the one worth keeping when
     * there is not room for all of them. */
    std::size_t startIdx = logLines.size();
    float totalHeight = 0;
    while (startIdx > 0) {
        float bounds[4];
        nvgTextBoxBounds(vg, 0, 0, maxTextWidth, logLines[startIdx - 1].c_str(), nullptr, bounds);
        const float lineH = bounds[3] - bounds[1];
        if (totalHeight + lineH > availableHeight)
            break;
        totalHeight += lineH;
        startIdx--;
    }

    float currentY = y + padding;
    for (std::size_t i = startIdx; i < logLines.size(); i++) {
        nvgTextBox(vg, x + padding, currentY, maxTextWidth, logLines[i].c_str(), nullptr);

        float bounds[4];
        nvgTextBoxBounds(vg, x + padding, currentY, maxTextWidth, logLines[i].c_str(), nullptr, bounds);
        currentY += (bounds[3] - bounds[1]);
    }

    nvgRestore(vg);
}

void ConnectingView::draw(NVGcontext* vg, float x, float y, float width, float height,
                          brls::Style style, brls::FrameContext* ctx)
{
    Box::draw(vg, x, y, width, height, style, ctx);

    auto* logArea = this->getView("connection/logArea");
    if (!logArea)
        return;

    const float centerX = logArea->getX() + logArea->getWidth() / 2.0f;
    const float centerY = logArea->getY() + logArea->getHeight() / 2.0f;

    if (showFailure.load()) {
        drawConnectionFailure(vg, centerX, centerY, failureText);
        return;
    }

    if (SettingsManager::getInstance()->getConnectionShowStages()) {
        const auto stage = static_cast<ConnectionStage>(currentStage.load());
        const int raw = static_cast<int>(stage);
        const int idx = raw < 1 ? 1 : (raw > 6 ? 6 : raw);
        drawConnectionRing(vg, centerX, centerY,
                           brls::getStr(connectionStageLabelKey(stage)), idx, 6);
        return;
    }

    renderLogs(vg, logArea->getX(), logArea->getY(),
               logArea->getWidth(), logArea->getHeight());
}

void startConnecting(std::unique_ptr<ConnectTask> task)
{
    auto view = SharedViewHolder::holdNew<ConnectingView>(std::move(task));
    brls::Application::pushActivity(new brls::Activity(view.get()));
    view->setupAndStart();
}

} // namespace akira::views
