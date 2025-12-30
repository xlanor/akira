#ifndef AKIRA_BENCHMARK_VIEW_HPP
#define AKIRA_BENCHMARK_VIEW_HPP

#include <borealis.hpp>
#include <chiaki/thread.h>
#include <deque>
#include <mutex>
#include <atomic>

class BenchmarkView : public brls::Box {
public:
    BenchmarkView();
    ~BenchmarkView() override;

    void startBenchmark();

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    brls::View* getDefaultFocus() override { return this; }

    static brls::View* create();

private:
    brls::Box* logContainer = nullptr;
    brls::ScrollingFrame* scrollFrame = nullptr;
    brls::Button* closeBtn = nullptr;
    std::deque<std::string> logLines;
    std::mutex logMutex;
    static constexpr size_t MAX_LOG_LINES = 200;

    ChiakiThread benchmarkThread;
    std::atomic<bool> threadStarted{false};
    std::atomic<bool> benchmarkRunning{false};
    std::atomic<bool> benchmarkFinished{false};

    bool logsNeedUpdate = false;
    bool needsScrollToBottom = false;

    void addLogLine(const std::string& line);
    void updateLogDisplay();

    static void* benchmarkThreadFunc(void* user);
};

#endif // AKIRA_BENCHMARK_VIEW_HPP
