#ifndef AKIRA_CLOUD_CONNECT_TASK_HPP
#define AKIRA_CLOUD_CONNECT_TASK_HPP

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "cloud/models.hpp"
#include "views/connect_task.hpp"

namespace cloud {

/*
 * A cloud session, which has to be provisioned before there is any console to
 * connect to. The service reports its own progress as text, so the stages are
 * read out of that rather than inferred from the log.
 */
class CloudConnectTask : public akira::views::ConnectTask {
public:
    CloudConnectTask(const Game& game, bool skipAttr);
    ~CloudConnectTask() override;

    std::string title() const override;
    const char* logKind() const override { return "cloud"; }

    void start(akira::views::ConnectSink& sink) override;
    void cancel() override;

    bool presentFailure(const std::string& error,
                        akira::views::ConnectSink& sink) override;

private:
    struct CallbackState {
        std::mutex mutex;
        akira::views::ConnectSink* sink = nullptr;
        std::atomic<bool> cancelled{false};
    };

    Game game;
    bool skipAttr = false;
    std::shared_ptr<CallbackState> callbackState = std::make_shared<CallbackState>();

    void provision();
};

} // namespace cloud

#endif // AKIRA_CLOUD_CONNECT_TASK_HPP
