#ifndef AKIRA_CLOUD_CONNECT_TASK_HPP
#define AKIRA_CLOUD_CONNECT_TASK_HPP

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

    std::string title() const override;
    const char* logKind() const override { return "cloud"; }

    void start(akira::views::ConnectSink& sink) override;

    bool presentFailure(const std::string& error,
                        akira::views::ConnectSink& sink) override;

private:
    Game game;
    bool skipAttr = false;
    akira::views::ConnectSink* sink = nullptr;

    void provision();
};

} // namespace cloud

#endif // AKIRA_CLOUD_CONNECT_TASK_HPP
