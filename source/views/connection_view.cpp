#include "views/connection_view.hpp"
#include "views/stream_view.hpp"
#include "core/discovery_manager.hpp"
#include <chiaki/remote/holepunch.h>

ConnectionView::ConnectionView(Host* host)
    : host(host)
{
    this->io = IO::GetInstance();
    this->inflateFromXMLRes("xml/views/connection_view.xml");

    logContainer = (brls::Box*)this->getView("connection/logs");
    scrollFrame = (brls::ScrollingFrame*)this->getView("connection/scroll");

    auto* titleLabel = (brls::Label*)this->getView("connection/title");
    titleLabel->setText("Connecting to " + host->getHostName());

    auto* cancelBtn = (brls::Button*)this->getView("connection/cancel");
    cancelBtn->registerClickAction([this](brls::View* view) {
        brls::Logger::info("Connection cancelled by user");
        if (connectionRunning) {
            this->host->cancelHolepunch();
        }
        brls::sync([]() {
            brls::Application::popActivity();
        });
        return true;
    });

    setFocusable(true);

    logSubscription = brls::Logger::getLogEvent()->subscribe(
        [this](brls::Logger::TimePoint time, brls::LogLevel level, std::string msg) {
            addLogLine(msg, level);
        });

    addLogLine("Starting connection...", brls::LogLevel::LOG_INFO);

    startConnection();
}

ConnectionView::~ConnectionView()
{
    brls::Logger::getLogEvent()->unsubscribe(logSubscription);
    if (connectionRunning) {
        host->cancelHolepunch();
    }
    if (threadStarted) {
        chiaki_thread_join(&connectionThread, nullptr);
    }
}

brls::View* ConnectionView::create()
{
    return nullptr;
}

void ConnectionView::addLogLine(const std::string& line, brls::LogLevel level)
{
    std::lock_guard<std::mutex> lock(logMutex);

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm time_tm = *std::localtime(&tt);

    char timeStr[32];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d.%03d",
             time_tm.tm_hour, time_tm.tm_min, time_tm.tm_sec, (int)ms);

    std::string fullLine = std::string(timeStr) + " " + line;

    logLines.push_back(fullLine);

    while (logLines.size() > MAX_LOG_LINES) {
        logLines.pop_front();
    }

    logsNeedUpdate = true;
}

void ConnectionView::updateLogDisplay()
{
    if (!logsNeedUpdate) return;

    std::lock_guard<std::mutex> lock(logMutex);

    std::string combinedText;
    for (size_t i = 0; i < logLines.size(); i++) {
        if (i > 0) combinedText += "\n";
        combinedText += logLines[i];
    }

    if (logContainer->getChildren().empty()) {
        auto* label = new brls::Label();
        label->setFontSize(18);
        label->setTextColor(nvgRGBA(200, 200, 200, 255));
        logContainer->addView(label);
    }

    auto* label = dynamic_cast<brls::Label*>(logContainer->getChildren().front());
    if (label) {
        label->setText(combinedText);
    }

    needsScrollToBottom = true;
    logsNeedUpdate = false;
}

void ConnectionView::startConnection()
{
    connectionRunning = true;
    connectionFinished = false;
    connectionSuccess = false;
    threadStarted = false;

    ChiakiErrorCode err = chiaki_thread_create(&connectionThread, connectionThreadFunc, this);
    if (err != CHIAKI_ERR_SUCCESS) {
        connectionRunning = false;
        connectionFinished = true;
        connectionError = "Failed to create connection thread";
        brls::Logger::error("{}", connectionError);
    } else {
        threadStarted = true;
    }
}

void* ConnectionView::connectionThreadFunc(void* user)
{
    ConnectionView* view = (ConnectionView*)user;
    Host* host = view->host;

    brls::Logger::info("Connection thread started");

    if (host->isRemote()) {
        auto* dm = DiscoveryManager::getInstance();
        if (!dm->isPsnTokenValid()) {
            view->connectionError = "PSN token expired. Please refresh in settings.";
            brls::Logger::error("{}", view->connectionError);
            view->connectionSuccess = false;
            view->connectionFinished = true;
            view->connectionRunning = false;
            return nullptr;
        }

        brls::Logger::info("Initiating holepunch connection...");

        ChiakiErrorCode err = host->connectHolepunch();
        if (err != CHIAKI_ERR_SUCCESS) {
            view->connectionError = std::string("Holepunch failed: ") + chiaki_error_string(err);
            brls::Logger::error("{}", view->connectionError);
            view->connectionSuccess = false;
            view->connectionFinished = true;
            view->connectionRunning = false;
            return nullptr;
        }

        brls::Logger::info("CTRL holepunch successful! Transitioning to StreamView...");

        view->connectionSuccess = true;
        view->connectionFinished = true;
        view->connectionRunning = false;
    } else {
        view->connectionSuccess = true;
        view->connectionFinished = true;
        view->connectionRunning = false;
        brls::Logger::info("Local connection - ready to stream");
    }

    return nullptr;
}

void ConnectionView::onConnectionComplete()
{
    if (connectionSuccess) {
        brls::Logger::info("Transitioning to stream view...");

        auto* streamView = new StreamView(host);

        brls::sync([this, streamView]() {
            brls::Application::popActivity();
            brls::Application::pushActivity(new brls::Activity(streamView));
            streamView->startStream();
        });
    } else {
        std::string error = connectionError;
        brls::sync([error]() {
            auto* dialog = new brls::Dialog("Connection Failed\n\n" + error);
            dialog->addButton("OK", [dialog]() {
                dialog->close();
                brls::sync([]() {
                    brls::Application::popActivity();
                });
            });
            dialog->open();
        });
    }
}

void ConnectionView::draw(NVGcontext* vg, float x, float y, float width, float height,
                          brls::Style style, brls::FrameContext* ctx)
{
    updateLogDisplay();

    if (connectionFinished && !transitionPending) {
        transitionPending = true;
        onConnectionComplete();
    }

    Box::draw(vg, x, y, width, height, style, ctx);

    if (needsScrollToBottom && scrollFrame && logContainer) {
        float contentHeight = logContainer->getHeight();
        float frameHeight = scrollFrame->getHeight();
        if (contentHeight > frameHeight) {
            scrollFrame->setContentOffsetY(-(contentHeight - frameHeight), false);
        }
        needsScrollToBottom = false;
    }
}
