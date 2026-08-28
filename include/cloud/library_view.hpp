#ifndef AKIRA_CLOUD_LIBRARY_VIEW_HPP
#define AKIRA_CLOUD_LIBRARY_VIEW_HPP

#include <borealis.hpp>

#include <memory>
#include <set>
#include <string>

#include "cloud/service.hpp"
#include "views/vendored/switchfin/recycling_grid.hpp"

namespace cloud {

class LibraryView : public brls::Box {
public:
    LibraryView();
    ~LibraryView() override;

    void willAppear(bool resetState) override;

private:
    void refresh(bool force);
    void renderSnapshot(const Snapshot& snapshot);
    void showState(const Snapshot& snapshot);
    void showCatalog(const Catalog& catalog);
    void applyFilter();
    void openPairing();
    void launchGame(const Game& game, bool forceSkipAttr = false);

    enum class Filter { Streamable, Owned, Favorites, All };
    Filter filterMode = Filter::Streamable;

    std::set<std::string> favoriteIds;
    bool isFavorite(const std::string& productId) const;
    void toggleFavorite(const std::string& productId);
    void toggleShortcut(const Game& game);
    void loadFavorites();
    void saveFavorites();
    std::string searchQuery;
    std::vector<Game> allGames;
    brls::Button* searchButton = nullptr;
    brls::Button* filterButton = nullptr;
    brls::Button* serverButton = nullptr;
    brls::Button* sortButton = nullptr;
    brls::Button* overflowButton = nullptr;
    int sortState = 0;

    void openServerPicker();
    void openFilterPicker();
    void openSortPicker();
    void openOverflowMenu();
    void updateServerButton();
    void updateSortButton();
    void showAddGameDialog(const Game& game);

    brls::Box* statusChip = nullptr;
    brls::Label* statusChipLabel = nullptr;
    brls::Box* noticeBox = nullptr;
    brls::Label* noticeLabel = nullptr;
    bool canPairState = false;
    bool checkingState = false;
    RecyclingGrid* grid = nullptr;
    brls::Box* stateBox = nullptr;
    RecyclingGridDataSource* dataSource = nullptr;

    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    int generation = 0;
    bool launching = false;
};

} // namespace cloud

#endif // AKIRA_CLOUD_LIBRARY_VIEW_HPP
