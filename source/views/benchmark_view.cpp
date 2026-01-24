#include "views/benchmark_view.hpp"
#include "core/thread_affinity.h"
#include "crypto/libnx/gmac.h"
#include <chiaki/thread.h>

#include <vector>
#include <chrono>
#include <cstdio>

struct BenchmarkThreadArgs {
    BenchmarkView* view;
};

BenchmarkView::BenchmarkView()
{
    this->inflateFromXMLRes("xml/views/benchmark_view.xml");

    logContainer = (brls::Box*)this->getView("benchmark/logs");
    scrollFrame = (brls::ScrollingFrame*)this->getView("benchmark/scroll");
    closeBtn = (brls::Button*)this->getView("benchmark/close");

    closeBtn->registerClickAction([](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });

    setFocusable(true);
}

BenchmarkView::~BenchmarkView()
{
    if (threadStarted && benchmarkRunning) {
        benchmarkRunning = false;
    }
    if (threadStarted) {
        chiaki_thread_join(&benchmarkThread, nullptr);
    }
}

brls::View* BenchmarkView::create()
{
    return new BenchmarkView();
}

void BenchmarkView::startBenchmark()
{
    benchmarkRunning = true;
    benchmarkFinished = false;
    threadStarted = false;

    auto* args = new BenchmarkThreadArgs{this};

    ChiakiErrorCode err = chiaki_thread_create(&benchmarkThread, benchmarkThreadFunc, args);
    if (err != CHIAKI_ERR_SUCCESS) {
        delete args;
        benchmarkRunning = false;
        benchmarkFinished = true;
        addLogLine("ERROR: Failed to create benchmark thread");
    } else {
        threadStarted = true;
    }
}

void BenchmarkView::addLogLine(const std::string& line)
{
    std::lock_guard<std::mutex> lock(logMutex);
    logLines.push_back(line);
    while (logLines.size() > MAX_LOG_LINES) {
        logLines.pop_front();
    }
    logsNeedUpdate = true;
}

void BenchmarkView::updateLogDisplay()
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

void* BenchmarkView::benchmarkThreadFunc(void* user)
{
    akira_thread_set_affinity(AKIRA_THREAD_NAME_BENCHMARK);
    auto* args = static_cast<BenchmarkThreadArgs*>(user);
    BenchmarkView* view = args->view;
    delete args;

    view->addLogLine("========================================");
    view->addLogLine("        GHASH Benchmark Results");
    view->addLogLine("========================================");
    view->addLogLine("");

    const int iterations = 10000;
    const size_t sizes[] = {64, 256, 1024, 4096};
    const int numSizes = 4;

    view->addLogLine("Iterations per test: " + std::to_string(iterations));
    view->addLogLine("");

    uint8_t key[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                       0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
    uint8_t iv[12] = {0};

    for (int s = 0; s < numSizes && view->benchmarkRunning; s++) {
        size_t size = sizes[s];
        std::vector<uint8_t> data(size, 0xAB);
        uint8_t tag[16];

        view->addLogLine("----------------------------------------");
        view->addLogLine("Testing " + std::to_string(size) + " byte blocks...");
        view->addLogLine("");

        view->addLogLine("  [TABLE] Running " + std::to_string(iterations) + " iterations...");
        chiaki_libnx_set_ghash_mode(CHIAKI_LIBNX_GHASH_TABLE);
        ChiakiGmacContext tableCtx;
        chiaki_gmac_context_init(&tableCtx);

        auto tableStart = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations && view->benchmarkRunning; i++) {
            chiaki_gmac_compute_cached(&tableCtx, key, iv, sizeof(iv), data.data(), size, tag, sizeof(tag));
        }
        auto tableEnd = std::chrono::steady_clock::now();
        chiaki_gmac_context_fini(&tableCtx);

        double tableMs = std::chrono::duration<double, std::milli>(tableEnd - tableStart).count();
        double tableOps = (iterations * 1000.0) / tableMs;
        double tableMBs = (iterations * size * 1000.0) / (tableMs * 1024.0 * 1024.0);

        char tableBuf[128];
        snprintf(tableBuf, sizeof(tableBuf), "  [TABLE] %.0f ops/s | %.2f MB/s | %.1f ms total",
                 tableOps, tableMBs, tableMs);
        view->addLogLine(tableBuf);

#ifdef CHIAKI_LIB_ENABLE_LIBNX_EXPERIMENTAL
        view->addLogLine("  [PMULL] Running " + std::to_string(iterations) + " iterations...");
        chiaki_libnx_set_ghash_mode(CHIAKI_LIBNX_GHASH_PMULL);
        ChiakiGmacContext pmullCtx;
        chiaki_gmac_context_init(&pmullCtx);

        auto pmullStart = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations && view->benchmarkRunning; i++) {
            chiaki_gmac_compute_cached(&pmullCtx, key, iv, sizeof(iv), data.data(), size, tag, sizeof(tag));
        }
        auto pmullEnd = std::chrono::steady_clock::now();
        chiaki_gmac_context_fini(&pmullCtx);

        double pmullMs = std::chrono::duration<double, std::milli>(pmullEnd - pmullStart).count();
        double pmullOps = (iterations * 1000.0) / pmullMs;
        double pmullMBs = (iterations * size * 1000.0) / (pmullMs * 1024.0 * 1024.0);

        char pmullBuf[128];
        snprintf(pmullBuf, sizeof(pmullBuf), "  [PMULL] %.0f ops/s | %.2f MB/s | %.1f ms total",
                 pmullOps, pmullMBs, pmullMs);
        view->addLogLine(pmullBuf);

        double speedup = tableMs / pmullMs;
        char speedupBuf[64];
        if (speedup >= 1.0) {
            snprintf(speedupBuf, sizeof(speedupBuf), "  >> PMULL is %.2fx faster", speedup);
        } else {
            snprintf(speedupBuf, sizeof(speedupBuf), "  >> TABLE is %.2fx faster", 1.0 / speedup);
        }
        view->addLogLine(speedupBuf);
#else
        view->addLogLine("  [PMULL] Not compiled (CHIAKI_LIB_ENABLE_LIBNX_EXPERIMENTAL not set)");
#endif
        view->addLogLine("");
    }

    chiaki_libnx_set_ghash_mode(CHIAKI_LIBNX_GHASH_PMULL);

    view->addLogLine("========================================");
    view->addLogLine("         Benchmark Complete!");
    view->addLogLine("========================================");

    brls::sync([]() {
        auto* statusLabel = (brls::Label*)brls::Application::getCurrentFocus();
        if (statusLabel) {
            auto* parent = statusLabel->getParent();
            while (parent) {
                auto* benchView = dynamic_cast<BenchmarkView*>(parent);
                if (benchView) {
                    auto* status = (brls::Label*)benchView->getView("benchmark/status");
                    if (status) {
                        status->setText("Complete");
                    }
                    break;
                }
                parent = parent->getParent();
            }
        }
    });

    view->benchmarkFinished = true;
    view->benchmarkRunning = false;

    return nullptr;
}

void BenchmarkView::draw(NVGcontext* vg, float x, float y, float width, float height,
                          brls::Style style, brls::FrameContext* ctx)
{
    updateLogDisplay();

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
