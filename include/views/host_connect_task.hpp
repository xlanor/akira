#ifndef AKIRA_HOST_CONNECT_TASK_HPP
#define AKIRA_HOST_CONNECT_TASK_HPP

#include <atomic>
#include <string>

#include <chiaki/thread.h>

#include "views/connect_task.hpp"

class Host;

namespace akira::views {

/*
 * A console akira already knows about, on the same network or over the
 * internet.
 *
 * Both are one task because the difference between them is one branch: a local
 * console is ready the moment it is asked, a remote one needs a PSN token, a
 * holepunch, and time to wake up. Splitting them would duplicate the parts that
 * are the same and hide how little actually differs.
 */
class HostConnectTask : public ConnectTask {
public:
    explicit HostConnectTask(Host* host);
    ~HostConnectTask() override;

    std::string title() const override;
    const char* logKind() const override;

    void start(ConnectSink& sink) override;
    void cancel() override;

private:
    static constexpr int WAKEUP_WAIT_SECONDS = 20;

    Host* host = nullptr;
    ConnectSink* sink = nullptr;

    ChiakiThread thread{};
    std::atomic<bool> threadStarted{false};
    std::atomic<bool> running{false};

    static void* threadFunc(void* user);
    void run();
};

} // namespace akira::views

#endif // AKIRA_HOST_CONNECT_TASK_HPP
