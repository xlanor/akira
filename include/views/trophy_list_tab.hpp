#ifndef AKIRA_TROPHY_LIST_TAB_HPP
#define AKIRA_TROPHY_LIST_TAB_HPP

#include <borealis.hpp>

#include <string>
#include <unordered_set>
#include <vector>

#include "core/trophy_manager.hpp"
#include "views/psn_action_button.hpp"
#include "views/psn_gated_box.hpp"
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
    brls::Label* playLabel = nullptr;
    brls::Label* detailLabel = nullptr;
    brls::Box* progressTrack = nullptr;
    brls::Rectangle* progressFill = nullptr;

    std::string iconUrl;

    static std::unordered_set<TrophyCardCell*> liveCells;
    static std::unordered_set<std::string> retriedIcons;
};

class TrophyGridDataSource : public RecyclingGridDataSource {
public:
    explicit TrophyGridDataSource(std::vector<psn::TrophyTitle> titles);

    std::vector<psn::TrophyTitle> titles;

    size_t getItemCount() override;
    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override;
    void onItemSelected(brls::Box* recycler, size_t index) override;
    void clearData() override;
};

enum class TitleSort {
    Recent,
    Progress,
    Name,
    Earned
};

enum class TitleFilter {
    All,
    InProgress,
    Completed,
    Unstarted,
    Hidden
};

const char* titleSortLabelKey(TitleSort sort);
const char* titleFilterLabelKey(TitleFilter filter);
std::string formatRelativeAge(int64_t savedAt);

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
    BRLS_BIND(PsnGatedBox, gate, "trophies/gate");
    BRLS_BIND(brls::Button, sortBtn, "trophies/sortBtn");
    BRLS_BIND(brls::Button, filterBtn, "trophies/filterBtn");
    BRLS_BIND(brls::Label, statusLabel, "trophies/status");
    BRLS_BIND(brls::Button, forceRefreshBtn, "trophies/forceRefreshBtn");

    void load(bool forceRefresh);
    void applySummary(const psn::TrophySummary& summary);
    void applyTitles(const std::vector<psn::TrophyTitle>& titles);
    void rebuildGrid();
    void showSortPicker();
    void showFilterPicker();
    void refreshControlLabels();
    void refreshStatusLine();

    PsnActionButton forceRefreshGate;

    std::vector<psn::TrophyTitle> titles;
    brls::RepeatingTimer statusTimer;
    static TitleSort sortMode;
    static TitleFilter filterMode;
    bool loadRequested = false;
    bool loading = false;
};

std::string formatTrophyPlatforms(const std::string& raw);
std::string formatTrophyCounts(const psn::TrophyCounts& counts);

#endif // AKIRA_TROPHY_LIST_TAB_HPP
