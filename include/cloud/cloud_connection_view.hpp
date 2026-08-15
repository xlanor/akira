#ifndef AKIRA_CLOUD_CONNECTION_VIEW_HPP
#define AKIRA_CLOUD_CONNECTION_VIEW_HPP

#include <borealis.hpp>

#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include "cloud/models.hpp"

class Host;

namespace cloud {

class CloudConnectionView : public brls::Box, public std::enable_shared_from_this<CloudConnectionView> {
public:
    CloudConnectionView(const Game& game, bool skipAttr);
    ~CloudConnectionView() override;

    void setupAndStart();

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    brls::View* getDefaultFocus() override { return this; }

private:
    Game game;
    bool skipAttr = false;
    bool settled = false;

    std::string stageText;
    int stageIndex = 0;
    int stageTotal = 0;

    void startProvision();
    void onProvisionSuccess(std::shared_ptr<Host> host);
    void onProvisionError(const std::string& error);
    void onProvisionProgress(const std::string& stage);

    void addLogLine(const std::string& line);
    void renderLogs(NVGcontext* vg, float x, float y, float width, float height);

    std::deque<std::string> logLines;
    std::mutex logMutex;
    static constexpr size_t MAX_LOG_LINES = 100;
    brls::Event<brls::Logger::TimePoint, brls::LogLevel, std::string>::Subscription logSubscription;
};

} // namespace cloud

#endif // AKIRA_CLOUD_CONNECTION_VIEW_HPP
