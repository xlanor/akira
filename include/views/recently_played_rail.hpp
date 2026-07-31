#ifndef AKIRA_RECENTLY_PLAYED_RAIL_HPP
#define AKIRA_RECENTLY_PLAYED_RAIL_HPP

#include <borealis.hpp>
#include <memory>

class RecentlyPlayedRail : public brls::Box {
public:
    RecentlyPlayedRail();
    ~RecentlyPlayedRail() override;

    void refresh();

private:
    brls::Label* titleLabel = nullptr;
    brls::Box* railRow = nullptr;

    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    int generation = 0;

    brls::Animatable entranceAnim{1.0f};
    bool railShown = false;
};

#endif // AKIRA_RECENTLY_PLAYED_RAIL_HPP
