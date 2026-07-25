#ifndef AKIRA_TROPHY_LIST_TAB_HPP
#define AKIRA_TROPHY_LIST_TAB_HPP

#include <borealis.hpp>

#include <string>
#include <unordered_set>
#include <vector>

#include "core/trophy_manager.hpp"
#include "views/psn_action_button.hpp"
#include "views/vendored/switchfin/recycling_grid.hpp"

class TrophyCardCell : public RecyclingGridItem {
public:
    TrophyCardCell();
    ~TrophyCardCell() override;

    void bindTitle(const psn::TrophyTitle& title);
    void prepareForReuse() override;
    void cacheForReuse() override;

    static RecyclingGridItem* create();

private:
    brls::Image* cover = nullptr;
    brls::Label* nameLabel = nullptr;
    brls::Label* detailLabel = nullptr;
    brls::Box* progressTrack = nullptr;
    brls::Rectangle* progressFill = nullptr;

    std::string iconUrl;

    // Cells are destroyed on recycle, on scroll, and wholesale when the data source is
    // swapped. An icon callback must never dereference one that has gone away, so results
    // are routed through this registry instead of a captured this.
    static std::unordered_set<TrophyCardCell*> liveCells;
};

class TrophyGridDataSource : public RecyclingGridDataSource {
public:
    explicit TrophyGridDataSource(std::vector<psn::TrophyTitle> titles);

    std::vector<psn::TrophyTitle> titles;

    size_t getItemCount() override;
    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override;
    void clearData() override;
};

class TrophyListTab : public brls::Box {
public:
    TrophyListTab();
    ~TrophyListTab() override;

    static brls::View* create();

    void willAppear(bool resetState) override;
    void willDisappear(bool resetState) override;

    static TrophyListTab* currentInstance;

private:
    BRLS_BIND(brls::Label, summaryTitleLabel, "trophies/summaryTitle");
    BRLS_BIND(brls::Label, summaryDetailLabel, "trophies/summaryDetail");
    BRLS_BIND(RecyclingGrid, grid, "trophies/grid");
    BRLS_BIND(brls::Button, forceRefreshBtn, "trophies/forceRefreshBtn");

    void load(bool forceRefresh);
    void applySummary(const psn::TrophySummary& summary);
    void applyTitles(const std::vector<psn::TrophyTitle>& titles);

    PsnActionButton forceRefreshGate;

    std::vector<psn::TrophyTitle> titles;
    bool loadRequested = false;
    bool loading = false;
};

std::string formatTrophyPlatforms(const std::string& raw);
std::string formatTrophyCounts(const psn::TrophyCounts& counts);

#endif // AKIRA_TROPHY_LIST_TAB_HPP
