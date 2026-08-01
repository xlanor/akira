#ifndef AKIRA_RECENTLY_PLAYED_RAIL_HPP
#define AKIRA_RECENTLY_PLAYED_RAIL_HPP

#include <borealis.hpp>
#include <functional>
#include <memory>

namespace psn { struct PlayedGame; }

class RecentlyPlayedRail : public brls::Box {
public:
    RecentlyPlayedRail();
    ~RecentlyPlayedRail() override;

    void refresh();
    void setResumeHandler(std::function<void(const psn::PlayedGame&)> handler);

private:
    brls::Label* titleLabel = nullptr;
    brls::Box* railRow = nullptr;
    std::function<void(const psn::PlayedGame&)> resumeHandler;

    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    int generation = 0;

    brls::Animatable entranceAnim{1.0f};
    bool railShown = false;
};

#endif // AKIRA_RECENTLY_PLAYED_RAIL_HPP
