#include "cloud/library_view.hpp"

#include "cloud/cloud_connection_view.hpp"

#include "core/settings_manager.hpp"
#include "core/trophy_manager.hpp"
#include "ui/motion.hpp"
#include "ui/theme.hpp"
#include "util/shared_view_holder.hpp"
#include "views/pair_view.hpp"
#include "views/stream_view.hpp"

#include <borealis/core/i18n.hpp>

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <vector>

using namespace brls::literals;

namespace cloud {

static constexpr size_t kCloudPerRow = 4;
static constexpr float kCloudRowHeight = 258.0f;

static brls::Box* makePill(const std::string& text, NVGcolor color)
{
    auto* pill = new brls::Box();
    pill->setAxis(brls::Axis::ROW);
    pill->setCornerRadius(9);
    pill->setPaddingLeft(9);
    pill->setPaddingRight(9);
    pill->setPaddingTop(1);
    pill->setPaddingBottom(2);
    pill->setMarginRight(6);
    pill->setBackgroundColor(akira::ui::withAlpha(color, 0x2e));

    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(14);
    label->setTextColor(color);
    pill->addView(label);
    return pill;
}

class CloudGameCard;
static std::vector<CloudGameCard*> g_liveCloudCards;

class CloudGameCard : public brls::Box {
public:
    CloudGameCard()
    {
        this->setAxis(brls::Axis::COLUMN);
        this->setFocusable(false);
        this->setCornerRadius(14);
        this->setPadding(12, 12, 12, 12);
        this->setBackgroundColor(akira::ui::active().surface);

        cover = new brls::Image();
        cover->setHeight(178);
        cover->setWidthPercentage(100.0f);
        cover->setCornerRadius(10);
        cover->setScalingType(brls::ImageScalingType::FILL);
        cover->setBackgroundColor(akira::ui::active().surfaceElevated);
        this->addView(cover);

        name = new brls::Label();
        name->setFontSize(20);
        name->setMarginTop(10);
        name->setWidthPercentage(100.0f);
        name->setSingleLine(true);
        this->addView(name);

        pillRow = new brls::Box();
        pillRow->setAxis(brls::Axis::ROW);
        pillRow->setMarginTop(8);
        this->addView(pillRow);

        this->registerClickAction([this](brls::View*) {
            if (boundGame.launchable() && launchCb)
                launchCb(boundGame);
            return true;
        });

        this->registerAction("akira/cloud/favorite"_i18n, brls::ControllerButton::BUTTON_Y,
            [this](brls::View*) {
                if (boundGame.productId.empty())
                    return true;
                favorited = !favorited;
                applyFavName();
                if (favCb)
                    favCb(boundGame.productId, favorited);
                return true;
            });

        this->registerAction("akira/cloud/pin_shortcut"_i18n, brls::ControllerButton::BUTTON_X,
            [this](brls::View*) {
                if (boundGame.launchable() && pinCb)
                    pinCb(boundGame);
                return true;
            });

        akira::ui::motion::liftOnFocus(this, focusAnim);
        g_liveCloudCards.push_back(this);
    }

    ~CloudGameCard() override
    {
        g_liveCloudCards.erase(
            std::remove(g_liveCloudCards.begin(), g_liveCloudCards.end(), this),
            g_liveCloudCards.end());
    }

    void applyFavName()
    {
        name->setText((favorited ? std::string("\xE2\x98\x85 ") : std::string()) + boundGame.name);
    }

    void bind(const Game& game, const std::function<void(const Game&)>& onLaunch,
        bool fav, const std::function<void(const std::string&, bool)>& onFav,
        const std::function<void(const Game&)>& onPin)
    {
        const auto& pal = akira::ui::active();
        bool streamable = game.streamableNow();

        boundGame = game;
        launchCb = onLaunch;
        favorited = fav;
        favCb = onFav;
        pinCb = onPin;

        this->setVisibility(brls::Visibility::VISIBLE);
        this->setFocusable(game.launchable());

        applyFavName();
        name->setTextColor(streamable ? pal.text : pal.textMuted);
        cover->setAlpha(streamable ? 1.0f : 0.5f);
        cover->clear();

        pillRow->clearViews();
        std::string platform = platformBadge(game);
        if (!platform.empty())
        {
            NVGcolor pc = game.platform == "ps5" ? pal.accentStrong
                        : game.platform == "ps4" ? pal.textMuted
                                                  : pal.media;
            pillRow->addView(makePill(platform, pc));
        }
        if (game.plusCatalog)
            pillRow->addView(makePill("akira/cloud/badge_plus"_i18n, pal.gold));
        std::string category = categoryBadge(game);
        if (!category.empty())
        {
            NVGcolor cc = game.isOwned ? pal.success
                        : game.category == "streamable" ? pal.media
                                                         : pal.warning;
            pillRow->addView(makePill(category, cc));
        }

        iconUrl = game.artworkUrl();
        if (iconUrl.empty())
            return;

        TrophyManager::getInstance()->fetchIcon(iconUrl,
            [](const std::string& url, const std::vector<uint8_t>& bytes) {
                if (bytes.empty())
                    return;
                for (CloudGameCard* card : g_liveCloudCards)
                    if (card->iconUrl == url)
                        card->cover->setImageFromMem(bytes.data(), static_cast<int>(bytes.size()));
            });
    }

    void reset()
    {
        this->setVisibility(brls::Visibility::INVISIBLE);
        this->setFocusable(false);
        launchCb = nullptr;
        iconUrl.clear();
        cover->clear();
        pillRow->clearViews();
    }

    std::string iconUrl;
    brls::Image* cover = nullptr;
    brls::Label* name = nullptr;
    brls::Box* pillRow = nullptr;

private:
    Game boundGame;
    std::function<void(const Game&)> launchCb;
    bool favorited = false;
    std::function<void(const std::string&, bool)> favCb;
    std::function<void(const Game&)> pinCb;
    brls::Animatable focusAnim{0.0f};
};

class CloudGameRowCell : public RecyclingGridItem {
public:
    CloudGameRowCell()
    {
        this->setAxis(brls::Axis::ROW);
        this->setWidthPercentage(100.0f);
        this->setFocusable(false);

        for (size_t i = 0; i < kCloudPerRow; i++)
        {
            cards[i] = new CloudGameCard();
            cards[i]->setGrow(1.0f);
            if (i > 0)
                cards[i]->setMarginLeft(16);
            this->addView(cards[i]);
        }
    }

    static RecyclingGridItem* create()
    {
        return new CloudGameRowCell();
    }

    void bindRow(const std::vector<Game>& games, size_t startIndex,
        const std::function<void(const Game&)>& onLaunch,
        const std::function<bool(const std::string&)>& isFav,
        const std::function<void(const std::string&, bool)>& onFav,
        const std::function<void(const Game&)>& onPin)
    {
        for (size_t i = 0; i < kCloudPerRow; i++)
        {
            size_t idx = startIndex + i;
            if (idx < games.size())
                cards[i]->bind(games[idx], onLaunch,
                    isFav ? isFav(games[idx].productId) : false, onFav, onPin);
            else
                cards[i]->reset();
        }
    }

    void prepareForReuse() override
    {
        for (size_t i = 0; i < kCloudPerRow; i++)
            if (cards[i])
                cards[i]->reset();
    }

private:
    CloudGameCard* cards[kCloudPerRow] = {};
};

class CloudCatalogDataSource : public RecyclingGridDataSource {
public:
    CloudCatalogDataSource(std::vector<Game> games, std::function<void(const Game&)> onLaunch,
        std::function<bool(const std::string&)> isFav,
        std::function<void(const std::string&, bool)> onFav,
        std::function<void(const Game&)> onPin)
        : games(std::move(games)), onLaunch(std::move(onLaunch)),
          isFav(std::move(isFav)), onFav(std::move(onFav)), onPin(std::move(onPin))
    {
    }

    size_t getItemCount() override
    {
        return (games.size() + kCloudPerRow - 1) / kCloudPerRow;
    }

    float heightForRow(brls::View* recycler, size_t index) override
    {
        return kCloudRowHeight;
    }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override
    {
        auto* cell = dynamic_cast<CloudGameRowCell*>(recycler->dequeueReusableCell("CloudRow"));
        if (!cell)
            return nullptr;
        cell->bindRow(games, index * kCloudPerRow, onLaunch, isFav, onFav, onPin);
        return cell;
    }

    void clearData() override
    {
        games.clear();
    }

private:
    std::vector<Game> games;
    std::function<void(const Game&)> onLaunch;
    std::function<bool(const std::string&)> isFav;
    std::function<void(const std::string&, bool)> onFav;
    std::function<void(const Game&)> onPin;
};

LibraryView::LibraryView()
{
    this->setAxis(brls::Axis::COLUMN);
    this->setPadding(22, 26, 22, 26);

    auto* header = new brls::Box();
    header->setAxis(brls::Axis::ROW);
    header->setAlignItems(brls::AlignItems::CENTER);
    header->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    header->setWidthPercentage(100.0f);
    header->setMarginBottom(18);
    this->addView(header);

    auto* headerText = new brls::Box();
    headerText->setAxis(brls::Axis::ROW);
    headerText->setAlignItems(brls::AlignItems::CENTER);
    headerText->setGrow(1.0f);
    header->addView(headerText);

    auto* actions = new brls::Box();
    actions->setAxis(brls::Axis::ROW);
    actions->setAlignItems(brls::AlignItems::CENTER);
    header->addView(actions);

    statusChip = new brls::Box();
    statusChip->setAxis(brls::Axis::ROW);
    statusChip->setCornerRadius(11);
    statusChip->setPaddingLeft(12);
    statusChip->setPaddingRight(12);
    statusChip->setPaddingTop(4);
    statusChip->setPaddingBottom(5);

    statusChipLabel = new brls::Label();
    statusChipLabel->setFontSize(16);
    statusChip->addView(statusChipLabel);
    headerText->addView(statusChip);

    searchButton = new brls::Button();
    searchButton->setText("akira/cloud/search"_i18n);
    searchButton->setMarginRight(8);
    searchButton->registerClickAction([this](brls::View*) {
        brls::Application::getImeManager()->openForText(
            [this](std::string text) { searchQuery = text; applyFilter(); },
            "akira/cloud/search"_i18n, "", 64, searchQuery);
        return true;
    });
    actions->addView(searchButton);

    sortState = SettingsManager::getInstance()->getCloudSortState();
    loadFavorites();

    filterButton = new brls::Button();
    filterButton->setText("akira/cloud/filter_streamable"_i18n);
    filterButton->setMarginRight(8);
    filterButton->registerClickAction([this](brls::View*) {
        openFilterPicker();
        return true;
    });
    actions->addView(filterButton);

    sortButton = new brls::Button();
    sortButton->setMarginRight(8);
    sortButton->registerClickAction([this](brls::View*) {
        openSortPicker();
        return true;
    });
    actions->addView(sortButton);
    updateSortButton();

    serverButton = new brls::Button();
    serverButton->setMarginRight(8);
    serverButton->registerClickAction([this](brls::View*) {
        openServerPicker();
        return true;
    });
    actions->addView(serverButton);
    updateServerButton();

    overflowButton = new brls::Button();
    overflowButton->setText("\xEE\x97\x93");
    overflowButton->registerClickAction([this](brls::View*) {
        openOverflowMenu();
        return true;
    });
    actions->addView(overflowButton);

    grid = new RecyclingGrid();
    grid->setGrow(1.0f);
    grid->registerCell("CloudRow", CloudGameRowCell::create);
    grid->estimatedRowHeight = kCloudRowHeight;
    grid->spanCount = 1;
    grid->isFlowMode = true;
    this->addView(grid);

    stateBox = new brls::Box();
    stateBox->setAxis(brls::Axis::COLUMN);
    stateBox->setGrow(1.0f);
    stateBox->setWidthPercentage(100.0f);
    stateBox->setVisibility(brls::Visibility::GONE);
    this->addView(stateBox);
}

LibraryView::~LibraryView()
{
    *alive = false;
}

void LibraryView::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);
    refresh(false);
}

void LibraryView::refresh(bool force)
{
    int gen = ++generation;
    auto guard = alive;

    brls::Logger::info("CloudLib: refresh(force={}) gen={}", force, gen);
    renderSnapshot(Service::instance().snapshotForActiveProfile());
    Service::instance().refreshActiveProfile(force,
        [this, guard, gen](const Snapshot& snapshot) {
            brls::Logger::info("CloudLib: refresh cb gen={} cur={} alive={} avail={} hasCatalog={} games={}",
                gen, generation, *guard, static_cast<int>(snapshot.status.availability),
                snapshot.hasCatalog, snapshot.catalog.games.size());
            if (!*guard || gen != generation)
                return;
            renderSnapshot(snapshot);
        });
}

void LibraryView::renderSnapshot(const Snapshot& snapshot)
{
    const auto& pal = akira::ui::active();
    const Status& status = snapshot.status;

    canPairState = status.canPair;
    checkingState = status.availability == Availability::Checking;

    std::string chipText;
    NVGcolor chipColor = pal.textDim;
    bool chipShown = true;
    switch (status.availability)
    {
        case Availability::Ready:
            chipText = "akira/cloud/chip_ready"_i18n;
            chipColor = pal.success;
            break;
        case Availability::Warning:
        case Availability::LaunchBlocked:
            chipText = "akira/cloud/chip_warning"_i18n;
            chipColor = pal.warning;
            break;
        case Availability::Empty:
            chipText = "akira/cloud/chip_empty"_i18n;
            chipColor = pal.textDim;
            break;
        case Availability::NeedsPairing:
            chipText = "akira/cloud/chip_pair"_i18n;
            chipColor = pal.accent;
            break;
        case Availability::Checking:
            chipText = "akira/cloud/chip_checking"_i18n;
            chipColor = pal.accent;
            break;
        case Availability::Error:
            chipText = "akira/cloud/chip_error"_i18n;
            chipColor = pal.danger;
            break;
        default:
            chipShown = false;
            break;
    }

    statusChip->setVisibility(chipShown ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    if (chipShown)
    {
        statusChipLabel->setText(chipText);
        statusChipLabel->setTextColor(chipColor);
        statusChip->setBackgroundColor(akira::ui::withAlpha(chipColor, 0x2e));
    }

    (void)pal;

    if (!snapshot.hasCatalog || snapshot.catalog.games.empty())
    {
        brls::Logger::info("CloudLib: renderSnapshot -> STATE (avail={} hasCatalog={} games={})",
            static_cast<int>(status.availability), snapshot.hasCatalog, snapshot.catalog.games.size());
        grid->setVisibility(brls::Visibility::GONE);
        grid->clearData();
        showState(snapshot);
        return;
    }

    brls::Logger::info("CloudLib: renderSnapshot -> CATALOG ({} games)", snapshot.catalog.games.size());

    bool refocusGrid = false;
    for (brls::View* f = brls::Application::getCurrentFocus(); f; f = f->getParent())
    {
        if (f == stateBox)
        {
            refocusGrid = true;
            break;
        }
    }

    stateBox->clearViews();
    stateBox->setVisibility(brls::Visibility::GONE);
    grid->setVisibility(brls::Visibility::VISIBLE);
    showCatalog(snapshot.catalog);

    if (refocusGrid)
        brls::Application::giveFocus(grid);
}

void LibraryView::showState(const Snapshot& snapshot)
{
    const auto& pal = akira::ui::active();
    const Status& st = snapshot.status;

    bool refocus = false;
    for (brls::View* f = brls::Application::getCurrentFocus(); f; f = f->getParent())
    {
        if (f == stateBox)
        {
            refocus = true;
            break;
        }
    }

    stateBox->clearViews();
    stateBox->setVisibility(brls::Visibility::VISIBLE);

    std::string glyph = "\xEE\x8A\xBD";
    NVGcolor glyphColor = pal.accent;
    std::string title = st.title;
    std::string detail = st.detail;
    enum StateAction { ActNone, ActPair, ActRefresh } action = ActNone;

    switch (st.availability)
    {
        case Availability::NeedsPairing:
            glyph = "\xEE\x85\x97";
            action = ActPair;
            break;
        case Availability::Empty:
            glyphColor = pal.textDim;
            title = "akira/cloud/empty_none_title"_i18n;
            detail = "akira/cloud/empty_none_detail"_i18n;
            action = ActRefresh;
            break;
        case Availability::Error:
        case Availability::LaunchBlocked:
            glyph = "\xEE\x8B\x81";
            glyphColor = pal.danger;
            action = ActRefresh;
            break;
        case Availability::Checking:
            glyphColor = pal.accent;
            break;
        default:
            glyphColor = pal.textDim;
            break;
    }

    auto* panel = new brls::Box();
    panel->setAxis(brls::Axis::COLUMN);
    panel->setAlignItems(brls::AlignItems::CENTER);
    panel->setJustifyContent(brls::JustifyContent::CENTER);
    panel->setGrow(1.0f);
    panel->setWidthPercentage(100.0f);

    auto* g = new brls::Label();
    g->setText(glyph);
    g->setFontSize(60);
    g->setTextColor(glyphColor);
    g->setMarginBottom(16);
    panel->addView(g);

    auto* t = new brls::Label();
    t->setText(title);
    t->setFontSize(24);
    t->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    t->setMarginBottom(8);
    panel->addView(t);

    if (!detail.empty())
    {
        auto* d = new brls::Label();
        d->setText(detail);
        d->setFontSize(16);
        d->setTextColor(pal.textDim);
        d->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        d->setWidth(560);
        d->setMarginBottom(18);
        panel->addView(d);
    }

    brls::Button* actionBtn = nullptr;
    if (action == ActPair)
    {
        actionBtn = new brls::Button();
        actionBtn->setText("akira/cloud/pair"_i18n);
        actionBtn->registerClickAction([this](brls::View*) { openPairing(); return true; });
        panel->addView(actionBtn);
    }
    else if (action == ActRefresh)
    {
        actionBtn = new brls::Button();
        actionBtn->setText("akira/cloud/check_again"_i18n);
        actionBtn->registerClickAction([this](brls::View*) { refresh(true); return true; });
        panel->addView(actionBtn);
    }

    stateBox->addView(panel);

    if (refocus)
        brls::Application::giveFocus(actionBtn ? static_cast<brls::View*>(actionBtn)
                                               : static_cast<brls::View*>(searchButton));
}

void LibraryView::showCatalog(const Catalog& catalog)
{
    allGames = catalog.games;
    applyFilter();
}

void LibraryView::applyFilter()
{
    std::string q = searchQuery;
    std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c) { return std::tolower(c); });

    std::vector<Game> filtered;
    filtered.reserve(allGames.size());
    for (const Game& game : allGames)
    {
        bool pass = filterMode == Filter::All ? true
                  : filterMode == Filter::Owned ? game.isOwned
                  : filterMode == Filter::Favorites ? isFavorite(game.productId)
                                                    : game.streamableNow();
        if (!pass)
            continue;
        if (!q.empty())
        {
            std::string name = game.name;
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
            if (name.find(q) == std::string::npos)
                continue;
        }
        filtered.push_back(game);
    }

    auto ciless = [](const std::string& a, const std::string& b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
            [](unsigned char c1, unsigned char c2) { return std::tolower(c1) < std::tolower(c2); });
    };
    if (sortState == 1)
        std::sort(filtered.begin(), filtered.end(),
            [&ciless](const Game& a, const Game& b) { return ciless(a.name, b.name); });
    else if (sortState == 2)
        std::sort(filtered.begin(), filtered.end(),
            [&ciless](const Game& a, const Game& b) { return ciless(b.name, a.name); });
    else
        std::stable_sort(filtered.begin(), filtered.end(),
            [](const Game& a, const Game& b) { return a.streamableNow() && !b.streamableNow(); });

    std::string filterLabel = filterMode == Filter::All ? "akira/cloud/filter_all"_i18n
                            : filterMode == Filter::Owned ? "akira/cloud/filter_owned"_i18n
                            : filterMode == Filter::Favorites ? "akira/cloud/filter_favorites"_i18n
                                                              : "akira/cloud/filter_streamable"_i18n;
    if (filterButton)
        filterButton->setText(filterLabel);

    brls::Logger::info("CloudLib: applyFilter mode={} q='{}' -> {}/{} games",
        static_cast<int>(filterMode), searchQuery, filtered.size(), allGames.size());

    auto guard = alive;
    std::function<void(const Game&)> onLaunch = [this, guard](const Game& game) {
        if (!*guard)
            return;
        if (game.streamableNow())
            launchGame(game);
        else
            showAddGameDialog(game);
    };
    std::function<bool(const std::string&)> isFav = [this, guard](const std::string& id) {
        return *guard && isFavorite(id);
    };
    std::function<void(const std::string&, bool)> onFav = [this, guard](const std::string& id, bool) {
        if (*guard)
            toggleFavorite(id);
    };
    std::function<void(const Game&)> onPin = [this, guard](const Game& game) {
        if (*guard)
            toggleShortcut(game);
    };

    auto* source = new CloudCatalogDataSource(std::move(filtered), std::move(onLaunch),
        std::move(isFav), std::move(onFav), std::move(onPin));
    dataSource = source;
    grid->setDataSource(source);
}

void LibraryView::updateServerButton()
{
    if (!serverButton)
        return;
    auto* s = SettingsManager::getInstance();
    std::string dcPscloud = s->getCloudDatacenter(true);
    std::string dcPsnow = s->getCloudDatacenter(false);
    std::string label;

    if (dcPscloud.empty() && dcPsnow.empty())
    {
        std::vector<Datacenter> dcs = parseDatacenters(s->getCloudDatacentersJson(true));
        if (dcs.empty())
            dcs = parseDatacenters(s->getCloudDatacentersJson(false));
        label = "akira/cloud/server_auto"_i18n;
        if (!dcs.empty())
            label += " (" + std::to_string(dcs.front().rttMs) + "ms)";
    }
    else
    {
        bool pscloud = !dcPscloud.empty();
        std::string dc = pscloud ? dcPscloud : dcPsnow;
        int rtt = -1;
        for (const Datacenter& d : parseDatacenters(s->getCloudDatacentersJson(pscloud)))
            if (d.name == dc) { rtt = d.rttMs; break; }
        label = dc + (rtt >= 0 ? " (" + std::to_string(rtt) + "ms)" : "");
    }

    serverButton->setText("\xEE\xA0\x8B  " + label);
}

void LibraryView::openFilterPicker()
{
    std::vector<std::string> names = {
        "akira/cloud/filter_streamable"_i18n,
        "akira/cloud/filter_owned"_i18n,
        "akira/cloud/filter_favorites"_i18n,
        "akira/cloud/filter_all"_i18n,
    };
    int selected = filterMode == Filter::Streamable ? 0
                 : filterMode == Filter::Owned ? 1
                 : filterMode == Filter::Favorites ? 2
                                                   : 3;
    auto guard = alive;
    auto* dropdown = new brls::Dropdown(
        "akira/cloud/filter"_i18n, names,
        [this, guard](int sel) {
            if (!*guard)
                return;
            filterMode = sel == 0 ? Filter::Streamable
                       : sel == 1 ? Filter::Owned
                       : sel == 2 ? Filter::Favorites
                                  : Filter::All;
            applyFilter();
        },
        selected);
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void LibraryView::openSortPicker()
{
    std::vector<std::string> names = {
        "akira/cloud/sort_playable"_i18n,
        "akira/cloud/sort_az"_i18n,
        "akira/cloud/sort_za"_i18n,
    };
    auto guard = alive;
    auto* dropdown = new brls::Dropdown(
        "akira/cloud/sort"_i18n, names,
        [this, guard](int sel) {
            if (!*guard)
                return;
            sortState = sel;
            SettingsManager::getInstance()->setCloudSortState(sortState);
            SettingsManager::getInstance()->writeFile();
            updateSortButton();
            applyFilter();
        },
        sortState);
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void LibraryView::openOverflowMenu()
{
    std::vector<std::string> names;
    std::vector<int> ops;
    names.push_back(checkingState ? "akira/cloud/refresh_busy"_i18n : "akira/cloud/refresh"_i18n);
    ops.push_back(0);
    if (canPairState)
    {
        names.push_back("akira/cloud/pair"_i18n);
        ops.push_back(1);
    }

    auto guard = alive;
    auto* dropdown = new brls::Dropdown(
        "akira/cloud/options"_i18n, names,
        [this, guard, ops](int sel) {
            if (!*guard || sel < 0 || sel >= static_cast<int>(ops.size()))
                return;
            if (ops[static_cast<size_t>(sel)] == 0)
                refresh(true);
            else
                openPairing();
        },
        0);
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void LibraryView::openServerPicker()
{
    auto* settings = SettingsManager::getInstance();
    std::vector<Datacenter> psnow = parseDatacenters(settings->getCloudDatacentersJson(false));
    std::vector<Datacenter> pscloud = parseDatacenters(settings->getCloudDatacentersJson(true));
    if (psnow.empty() && pscloud.empty())
    {
        auto* dialog = new brls::Dialog("akira/cloud/server_none"_i18n);
        dialog->addButton("akira/common/ok"_i18n, [dialog]() { dialog->close(); });
        dialog->open();
        return;
    }

    struct Entry { bool pscloud; std::string name; };
    std::vector<Entry> entries;
    std::vector<std::string> names;
    names.push_back("akira/cloud/server_auto"_i18n);

    std::string curPsnow = settings->getCloudDatacenter(false);
    std::string curPscloud = settings->getCloudDatacenter(true);
    int selected = 0;
    auto addService = [&](const std::vector<Datacenter>& dcs, bool isPscloud, const std::string& cur, const char* tag) {
        for (const Datacenter& dc : dcs)
        {
            entries.push_back({isPscloud, dc.name});
            names.push_back(std::string(tag) + " · " + dc.name + " (" + std::to_string(dc.rttMs) + "ms)");
            if (dc.name == cur)
                selected = static_cast<int>(entries.size());
        }
    };
    addService(psnow, false, curPsnow, "PS Now");
    addService(pscloud, true, curPscloud, "PS5");

    auto guard = alive;
    auto* dropdown = new brls::Dropdown(
        "akira/cloud/server"_i18n, names,
        [this, guard, entries](int sel) {
            auto* s = SettingsManager::getInstance();
            if (sel <= 0 || sel > static_cast<int>(entries.size()))
            {
                s->setCloudDatacenter(false, "");
                s->setCloudDatacenter(true, "");
            }
            else
            {
                const Entry& e = entries[static_cast<size_t>(sel - 1)];
                s->setCloudDatacenter(e.pscloud, e.name);
            }
            s->writeFile();
            if (*guard)
                updateServerButton();
        },
        selected);
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void LibraryView::updateSortButton()
{
    if (!sortButton)
        return;
    std::string key = sortState == 1 ? "akira/cloud/sort_az"
                    : sortState == 2 ? "akira/cloud/sort_za"
                                     : "akira/cloud/sort_playable";
    sortButton->setText(brls::getStr(key));
}

bool LibraryView::isFavorite(const std::string& productId) const
{
    return favoriteIds.count(productId) > 0;
}

void LibraryView::toggleFavorite(const std::string& productId)
{
    if (productId.empty())
        return;
    if (favoriteIds.count(productId))
        favoriteIds.erase(productId);
    else
        favoriteIds.insert(productId);
    saveFavorites();
    if (filterMode == Filter::Favorites)
        applyFilter();
}

void LibraryView::toggleShortcut(const Game& game)
{
    if (!game.launchable())
        return;
    auto* settings = SettingsManager::getInstance();
    std::vector<Game> shortcuts = parseShortcuts(settings->getCloudShortcuts());
    auto it = std::find_if(shortcuts.begin(), shortcuts.end(),
        [&](const Game& g) { return g.productId == game.productId; });

    bool added;
    if (it != shortcuts.end())
    {
        shortcuts.erase(it);
        added = false;
    }
    else
    {
        shortcuts.push_back(game);
        added = true;
    }
    settings->setCloudShortcuts(serializeShortcuts(shortcuts));
    settings->writeFile();
    brls::Application::notify(added ? "akira/cloud/shortcut_added"_i18n
                                    : "akira/cloud/shortcut_removed"_i18n);
}

void LibraryView::loadFavorites()
{
    favoriteIds.clear();
    std::string raw = SettingsManager::getInstance()->getCloudFavorites();
    size_t start = 0;
    while (start < raw.size())
    {
        size_t nl = raw.find('\n', start);
        std::string id = raw.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (!id.empty())
            favoriteIds.insert(id);
        if (nl == std::string::npos)
            break;
        start = nl + 1;
    }
}

void LibraryView::saveFavorites()
{
    std::string joined;
    for (const std::string& id : favoriteIds)
    {
        if (!joined.empty())
            joined += "\n";
        joined += id;
    }
    SettingsManager::getInstance()->setCloudFavorites(joined);
    SettingsManager::getInstance()->writeFile();
}

void LibraryView::showAddGameDialog(const Game& game)
{
    std::string msg = brls::getStr("akira/cloud/add_game_body", game.name);
    auto* dialog = new brls::Dialog(msg);
    dialog->addButton("akira/common/ok"_i18n, [dialog]() { dialog->close(); });
    dialog->open();
}

void LibraryView::openPairing()
{
    brls::Application::pushActivity(new brls::Activity(new PairView()));
}

void LibraryView::launchGame(const Game& game, bool forceSkipAttr)
{
    if (launching)
        return;

    launching = true;

    bool skipAttr = forceSkipAttr || SettingsManager::getInstance()->getCloudAttrPassed();

    auto view = SharedViewHolder::holdNew<CloudConnectionView>(game, skipAttr);
    view->setupAndStart();
    brls::Application::pushActivity(new brls::Activity(view.get()));

    launching = false;
}

} // namespace cloud
