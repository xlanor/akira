#ifndef AKIRA_PAIR_ADVERTISER_HPP
#define AKIRA_PAIR_ADVERTISER_HPP

#include <atomic>
#include <thread>

namespace akira::pair {

class PairAdvertiser {
public:
    PairAdvertiser() = default;
    ~PairAdvertiser();

    void start(int port);
    void stop();

private:
    void run(int port);

    std::thread thread_;
    std::atomic<bool> running_{false};
};

}

#endif
