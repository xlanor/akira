#ifndef AKIRA_TROPHY_LIST_TAB_HPP
#define AKIRA_TROPHY_LIST_TAB_HPP

#include <borealis.hpp>

#include <string>
#include <unordered_set>
#include <vector>

#include "core/trophy_manager.hpp"
#include "views/psn_action_button.hpp"
#include "views/psn_gated_box.hpp"
#include "views/progress_ring.hpp"
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

    brls::Animatable focusAnim{0.0f};

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
    BRLS_BIND(brls::Box, profileBox, "trophies/profile");
    BRLS_BIND(RecyclingGrid, grid, "trophies/grid");
    BRLS_BIND(PsnGatedBox, gate, "trophies/gate");
    BRLS_BIND(brls::Button, sortBtn, "trophies/sortBtn");
    BRLS_BIND(brls::Button, filterBtn, "trophies/filterBtn");
    brls::Button* forceRefreshBtn = nullptr;

    void load(bool forceRefresh);
    void buildProfileCard();
    void applySummary(const psn::TrophySummary& summary);
    void applyTitles(const std::vector<psn::TrophyTitle>& titles);
    void rebuildGrid();
    void showSortPicker();
    void showFilterPicker();
    void refreshControlLabels();
    void refreshStatusLine();

    brls::Label* levelValue = nullptr;
    ProgressRing* levelRing = nullptr;
    brls::Label* statusLabel = nullptr;
    brls::Image* tierImages[4] = {};
    brls::Label* tierCounts[4] = {};

    brls::Animatable summaryFillAnim{0.0f};
    brls::Animatable tierCountAnim[4] = {};
    brls::Animatable entryAnim{1.0f};
    int summaryPct = -1;
    int tierPrev[4] = {-1, -1, -1, -1};

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
