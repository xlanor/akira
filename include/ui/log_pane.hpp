#ifndef AKIRA_UI_LOG_PANE_HPP
#define AKIRA_UI_LOG_PANE_HPP

#include <borealis.hpp>

#include <deque>
#include <mutex>
#include <string>

namespace akira::ui {

class LogPane {
public:
    ~LogPane();

    void subscribe();
    void unsubscribe();

    void addLine(const std::string& line);
    void render(NVGcontext* vg, float x, float y, float width, float height);

private:
    std::deque<std::string> lines;
    std::mutex linesMutex;
    static constexpr size_t MAX_LINES = 100;

    brls::Event<brls::Logger::TimePoint, brls::LogLevel, std::string>::Subscription subscription;
    bool subscribed = false;
};

} // namespace akira::ui

#endif // AKIRA_UI_LOG_PANE_HPP
