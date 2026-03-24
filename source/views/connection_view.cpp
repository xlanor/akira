#include "views/connection_view.hpp"
#include "views/stream_view.hpp"
#include "core/discovery_manager.hpp"
#include "core/settings_manager.hpp"
#include "core/thread_affinity.h"
#include "util/shared_view_holder.hpp"
#include <chiaki/remote/holepunch.h>
#include <chiaki/thread.h>

ConnectionView::ConnectionView(Host* host)
    : host(host)
{
    this->io = IO::GetInstance();
    this->inflateFromXMLRes("xml/views/connection_view.xml");

    auto* titleLabel = (brls::Label*)this->getView("connection/title");
    titleLabel->setText("Connecting to " + host->getHostName());

    setFocusable(true);
}

void ConnectionView::setupAndStart()
{
    auto weak = weak_from_this();

    auto* cancelBtn = (brls::Button*)this->getView("connection/cancel");
    cancelBtn->registerClickAction([weak](brls::View* view) {
        if (auto self = weak.lock()) {
            brls::Logger::info("Connection cancelled by user");
            if (self->connectionRunning) {
                self->host->cancelHolepunch();
            }
            brls::sync([]() {
                brls::Application::popActivity();
            });
        }
        return true;
    });

    logSubscription = brls::Logger::getLogEvent()->subscribe(
        [weak](brls::Logger::TimePoint time, brls::LogLevel level, std::string msg) {
            if (auto self = weak.lock()) {
                self->addLogLine(msg, level);
            }
        });

    switchToConnectionLog();

    std::string connType = host->isRemote() ? "remote" : "local";
    brls::Logger::info("========================================");
    brls::Logger::info("CONNECTION ATTEMPT: {} to {}", connType, host->getHostName());
    brls::Logger::info("========================================");

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

    restoreMainLog();
    SharedViewHolder::release(this);
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

}

void ConnectionView::switchToConnectionLog()
{
    if (!SettingsManager::getInstance()->getEnableFileLogging())
        return;

    std::string connType = host->isRemote() ? "remote" : "local";
    std::string logPath = SettingsManager::getConnectionLogFilePath(connType);

    FILE* newLogFile = fopen(logPath.c_str(), "w");
    if (newLogFile) {
        m_prevLogOutput = brls::Logger::getLogOutput();
        m_connectionLogFile = newLogFile;
        brls::Logger::setLogOutput(newLogFile);
        brls::Logger::info("Switched to connection log: {}", logPath);
    }
}

void ConnectionView::restoreMainLog()
{
    if (m_connectionLogFile) {
        brls::Logger::setLogOutput(m_prevLogOutput);
        fclose(m_connectionLogFile);
        m_connectionLogFile = nullptr;
        m_prevLogOutput = nullptr;
    }
}

void ConnectionView::renderLogs(NVGcontext* vg, float x, float y, float width, float height)
{
    std::lock_guard<std::mutex> lock(logMutex);

    nvgSave(vg);
    nvgScissor(vg, x, y, width, height);

    nvgFontSize(vg, 16);
    nvgFillColor(vg, nvgRGBA(200, 200, 200, 255));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

    float padding = 20;
    float maxTextWidth = width - padding * 2;
    float availableHeight = height - padding * 2;

    // Calculate line heights from bottom up to find which lines fit
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

// Helper struct to pass weak_ptr through C thread API
struct ConnectionThreadArgs {
    std::weak_ptr<ConnectionView> weak;
    Host* host;  // Host pointer is safe - it outlives the connection
};

void ConnectionView::startConnection()
{
    connectionRunning = true;
    connectionFinished = false;
    connectionSuccess = false;
    threadStarted = false;

    auto args = std::make_unique<ConnectionThreadArgs>(ConnectionThreadArgs{weak_from_this(), host});

    ChiakiErrorCode err = chiaki_thread_create(&connectionThread, connectionThreadFunc, args.get());
    if (err != CHIAKI_ERR_SUCCESS) {
        connectionRunning = false;
        connectionFinished = true;
        connectionError = "Failed to create connection thread";
        brls::Logger::error("{}", connectionError);
    } else {
        args.release();
        threadStarted = true;
    }
}

void* ConnectionView::connectionThreadFunc(void* user)
{
    akira_thread_set_affinity(AKIRA_THREAD_NAME_CONNECTION);
    auto* args = static_cast<ConnectionThreadArgs*>(user);
    auto weak = args->weak;
    Host* host = args->host;
    delete args;

    brls::Logger::info("Connection thread started");

    if (host->isRemote()) {
        auto* dm = DiscoveryManager::getInstance();
        if (!dm->isPsnTokenValid()) {
            if (auto view = weak.lock()) {
                view->connectionError = "PSN token expired. Please refresh in settings.";
                brls::Logger::error("{}", view->connectionError);
                view->connectionSuccess = false;
                view->connectionFinished = true;
                view->connectionRunning = false;
            }
            return nullptr;
        }

        brls::Logger::info("Initiating holepunch connection...");

        ChiakiErrorCode err = host->connectHolepunch();
        if (err != CHIAKI_ERR_SUCCESS) {
            if (auto view = weak.lock()) {
                view->connectionError = std::string("Holepunch failed: ") + chiaki_error_string(err);
                brls::Logger::error("{}", view->connectionError);
                view->connectionSuccess = false;
                view->connectionFinished = true;
                view->connectionRunning = false;
            }
            return nullptr;
        }

        brls::Logger::info("CTRL holepunch successful! Transitioning to StreamView...");

        if (auto view = weak.lock()) {
            view->connectionSuccess = true;
            view->connectionFinished = true;
            view->connectionRunning = false;
        }
    } else {
        if (auto view = weak.lock()) {
            view->connectionSuccess = true;
            view->connectionFinished = true;
            view->connectionRunning = false;
        }
        brls::Logger::info("Local connection - ready to stream");
    }

    return nullptr;
}

void ConnectionView::onConnectionComplete()
{
    if (connectionSuccess) {
        brls::Logger::info("Transitioning to stream view...");

        auto streamView = SharedViewHolder::holdNew<StreamView>(host);
        streamView->setupCallbacks();

        brls::sync([streamView]() {
            brls::Application::popActivity();
            brls::Application::pushActivity(new brls::Activity(streamView.get()));
            streamView->startStream();
        });
    } else {
        std::string error = connectionError;
        brls::sync([error]() {
            auto* dialog = new brls::Dialog("Connection Failed\n\n" + error);
            dialog->addButton("OK", []() {
                brls::Application::popActivity();
            });
            dialog->open();
        });
    }
}

void ConnectionView::draw(NVGcontext* vg, float x, float y, float width, float height,
                          brls::Style style, brls::FrameContext* ctx)
{
    if (connectionFinished && !transitionPending) {
        transitionPending = true;
        onConnectionComplete();
    }

    Box::draw(vg, x, y, width, height, style, ctx);

    // Render logs directly with NVG into the logArea space
    auto* logArea = this->getView("connection/logArea");
    if (logArea) {
        renderLogs(vg, logArea->getX(), logArea->getY(),
                   logArea->getWidth(), logArea->getHeight());
    }
}
