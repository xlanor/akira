#include "views/trophy_list_tab.hpp"
#include "views/trophy_detail_view.hpp"

#include <algorithm>
#include <format>

using namespace brls::literals;

static const brls::ButtonStyle BUTTONSTYLE_BLUE = {
    .shadowType              = brls::ShadowType::GENERIC,
    .hideHighlightBackground = true,
    .highlightPadding = "",
    .borderThickness  = "",
    .enabledBackgroundColor = "",
    .enabledLabelColor      = "brls/button/primary_enabled_text",
    .enabledBorderColor     = "",
    .disabledBackgroundColor = "",
    .disabledLabelColor      = "brls/button/primary_disabled_text",
    .disabledBorderColor     = "",
};

TrophyListTab* TrophyListTab::currentInstance = nullptr;
TitleSort TrophyListTab::sortMode = TitleSort::Recent;
TitleFilter TrophyListTab::filterMode = TitleFilter::All;
std::unordered_set<TrophyCardCell*> TrophyCardCell::liveCells;
std::unordered_set<std::string> TrophyCardCell::retriedIcons;

static constexpr float CARD_COVER_HEIGHT = 196;
static constexpr float CARD_ROW_HEIGHT = 330;
static const NVGcolor PLAY_COLOR = nvgRGB(92, 157, 255);

std::string formatPlayDuration(int64_t seconds)
{
    if (seconds <= 0)
        return std::string();

    int64_t hours = seconds / 3600;
    if (hours >= 1)
        return brls::getStr("akira/trophies/play_hours", static_cast<int>(hours));

    return brls::getStr("akira/trophies/play_minutes", static_cast<int>((seconds + 59) / 60));
}

const char* titleSortLabelKey(TitleSort sort)
{
    switch (sort)
    {
        case TitleSort::Recent: return "akira/trophies/tsort_recent";
        case TitleSort::Progress: return "akira/trophies/tsort_progress";
        case TitleSort::Name: return "akira/trophies/tsort_name";
        case TitleSort::Earned: return "akira/trophies/tsort_earned";
    }
    return "akira/trophies/tsort_recent";
}

const char* titleFilterLabelKey(TitleFilter filter)
{
    switch (filter)
    {
        case TitleFilter::All: return "akira/trophies/tfilter_all";
        case TitleFilter::InProgress: return "akira/trophies/tfilter_in_progress";
        case TitleFilter::Completed: return "akira/trophies/tfilter_completed";
        case TitleFilter::Unstarted: return "akira/trophies/tfilter_unstarted";
        case TitleFilter::Hidden: return "akira/trophies/tfilter_hidden";
    }
    return "akira/trophies/tfilter_all";
}

static bool titleMatchesFilter(const psn::TrophyTitle& title, TitleFilter filter)
{
    int earned = title.earnedTrophies.total();
    int defined = title.definedTrophies.total();

    switch (filter)
    {
        case TitleFilter::All: return !title.hiddenFlag;
        case TitleFilter::InProgress: return !title.hiddenFlag && earned > 0 && earned < defined;
        case TitleFilter::Completed: return !title.hiddenFlag && defined > 0 && earned >= defined;
        case TitleFilter::Unstarted: return !title.hiddenFlag && earned == 0;
        case TitleFilter::Hidden: return title.hiddenFlag;
    }
    return true;
}

std::string formatRelativeAge(int64_t savedAt)
{
    if (savedAt <= 0)
        return std::string();

    int64_t now = static_cast<int64_t>(std::time(nullptr));
    int64_t age = now - savedAt;

    if (age < 0)
        return brls::getStr("akira/trophies/updated_just_now");

    if (age < 90)
        return brls::getStr("akira/trophies/updated_just_now");

    if (age < 3600)
        return brls::getStr("akira/trophies/updated_minutes", static_cast<int>(age / 60));

    if (age < 86400)
        return brls::getStr("akira/trophies/updated_hours", static_cast<int>(age / 3600));

    return brls::getStr("akira/trophies/updated_days", static_cast<int>(age / 86400));
}

std::string formatTrophyPlatforms(const std::string& raw)
{
    std::string out;
    size_t begin = 0;

    while (begin <= raw.size())
    {
        size_t comma = raw.find(',', begin);
        std::string part = raw.substr(begin, comma == std::string::npos ? std::string::npos : comma - begin);

        size_t first = part.find_first_not_of(" \t");
        if (first != std::string::npos)
        {
            size_t last = part.find_last_not_of(" \t");
            if (!out.empty())
                out += ", ";
            out += part.substr(first, last - first + 1);
        }

        if (comma == std::string::npos)
            break;
        begin = comma + 1;
    }

    return out;
}

std::string formatTrophyCounts(const psn::TrophyCounts& counts)
{
    std::string out;

    auto append = [&out](int value, const char* key) {
        if (value <= 0)
            return;
        if (!out.empty())
            out += ", ";
        out += brls::getStr(key, value);
    };

    append(counts.platinum, "akira/trophies/count_platinum");
    append(counts.gold, "akira/trophies/count_gold");
    append(counts.silver, "akira/trophies/count_silver");
    append(counts.bronze, "akira/trophies/count_bronze");

    return out;
}

TrophyCardCell::TrophyCardCell()
{
    this->setAxis(brls::Axis::COLUMN);
    this->setCornerRadius(8);
    this->setBackgroundColor(brls::Application::getTheme().getColor("color/card"));
    this->setPadding(12, 12, 12, 12);
    this->setFocusable(true);

    cover = new brls::Image();
    cover->setHeight(CARD_COVER_HEIGHT);
    cover->setScalingType(brls::ImageScalingType::FILL);
    cover->setCornerRadius(6);
    cover->setBackgroundColor(brls::Application::getTheme().getColor("color/grey_3"));
    this->addView(cover);

    nameLabel = new brls::Label();
    nameLabel->setFontSize(19);
    nameLabel->setSingleLine(true);
    nameLabel->setMarginTop(8);
    this->addView(nameLabel);

    playLabel = new brls::Label();
    playLabel->setFontSize(14);
    playLabel->setTextColor(PLAY_COLOR);
    playLabel->setSingleLine(true);
    playLabel->setMarginTop(4);
    playLabel->setVisibility(brls::Visibility::GONE);
    this->addView(playLabel);

    detailLabel = new brls::Label();
    detailLabel->setFontSize(14);
    detailLabel->setTextColor(nvgRGB(150, 150, 150));
    detailLabel->setSingleLine(true);
    detailLabel->setMarginTop(3);
    this->addView(detailLabel);

    progressTrack = new brls::Box(brls::Axis::ROW);
    progressTrack->setHeight(4);
    progressTrack->setCornerRadius(2);
    progressTrack->setBackgroundColor(brls::Application::getTheme().getColor("color/grey_3"));
    progressTrack->setMarginTop(7);

    progressFill = new brls::Rectangle();
    progressFill->setHeight(4);
    progressFill->setCornerRadius(2);
    progressFill->setColor(nvgRGB(92, 157, 255));
    progressTrack->addView(progressFill);

    this->addView(progressTrack);

    liveCells.insert(this);
}

TrophyCardCell::~TrophyCardCell()
{
    liveCells.erase(this);
}

void TrophyCardCell::bindTitle(const psn::TrophyTitle& title)
{
    nameLabel->setText(title.trophyTitleName.empty() ? title.npCommunicationId : title.trophyTitleName);

    detailLabel->setText(std::format("{}  ·  {}%  ·  {}/{}",
        formatTrophyPlatforms(title.trophyTitlePlatform),
        title.progress,
        title.earnedTrophies.total(),
        title.definedTrophies.total()));

    TrophyManager::GameProgress play = TrophyManager::getInstance()->gameProgressFor(title.npCommunicationId);

    if (play.valid && play.playDurationSeconds > 0)
    {
        std::string age = formatRelativeAge(psn::parseIso8601Timestamp(play.lastPlayedDateTime));

        playLabel->setText(age.empty()
            ? formatPlayDuration(play.playDurationSeconds)
            : std::format("{}  ·  {}", formatPlayDuration(play.playDurationSeconds), age));

        playLabel->setVisibility(brls::Visibility::VISIBLE);
    }
    else
    {
        playLabel->setVisibility(brls::Visibility::GONE);
    }

    float ratio = std::clamp(title.progress / 100.0f, 0.0f, 1.0f);
    progressFill->setWidthPercentage(ratio * 100.0f);

    iconUrl = title.trophyTitleIconUrl;
    cover->clear();

    if (iconUrl.empty())
        return;

    TrophyManager::getInstance()->fetchIcon(iconUrl,
        [](const std::string& url, const std::vector<uint8_t>& bytes) {
            bool decodeFailed = false;

            for (TrophyCardCell* cell : liveCells)
            {
                if (cell->iconUrl != url)
                    continue;

                cell->cover->setImageFromMem(bytes.data(), static_cast<int>(bytes.size()));

                if (cell->cover->getTexture() <= 0)
                    decodeFailed = true;
            }

            if (!decodeFailed)
                return;

            brls::Logger::warning("Trophy cover failed to decode ({} bytes) {}", bytes.size(), url);

            if (retriedIcons.insert(url).second)
                TrophyManager::getInstance()->discardIcon(url);
        });
}

void TrophyCardCell::prepareForReuse()
{
    iconUrl.clear();
    cover->clear();
}

void TrophyCardCell::cacheForReuse()
{
    iconUrl.clear();
    cover->clear();
}

RecyclingGridItem* TrophyCardCell::create()
{
    return new TrophyCardCell();
}

TrophyGridDataSource::TrophyGridDataSource(std::vector<psn::TrophyTitle> titles)
    : titles(std::move(titles))
{
}

size_t TrophyGridDataSource::getItemCount()
{
    return titles.size();
}

RecyclingGridItem* TrophyGridDataSource::cellForRow(RecyclingView* recycler, size_t index)
{
    auto* cell = dynamic_cast<TrophyCardCell*>(recycler->dequeueReusableCell("TrophyCard"));
    if (!cell)
        return nullptr;

    if (index < titles.size())
        cell->bindTitle(titles[index]);

    return cell;
}

void TrophyGridDataSource::onItemSelected(brls::Box* recycler, size_t index)
{
    if (index >= titles.size())
        return;

    brls::Application::pushActivity(new brls::Activity(new TrophyDetailView(titles[index])));
}

void TrophyGridDataSource::clearData()
{
    titles.clear();
}

TrophyListTab::TrophyListTab()
{
    this->inflateFromXMLRes("xml/tabs/trophies.xml");

    currentInstance = this;

    grid->registerCell("TrophyCard", TrophyCardCell::create);
    grid->estimatedRowHeight = CARD_ROW_HEIGHT;

    for (brls::Button* button : {forceRefreshBtn.getView(), sortBtn.getView(), filterBtn.getView()})
        button->setStyle(&BUTTONSTYLE_BLUE);

    sortBtn->setBackgroundColor(nvgRGBA(72, 76, 84, 255));
    filterBtn->setBackgroundColor(nvgRGBA(72, 76, 84, 255));

    sortBtn->registerClickAction([this](brls::View* view) {
        showSortPicker();
        return true;
    });

    filterBtn->registerClickAction([this](brls::View* view) {
        showFilterPicker();
        return true;
    });

    refreshControlLabels();

    forceRefreshGate.attach(
        forceRefreshBtn,
        nvgRGBA(92, 157, 255, 255),
        "akira/trophies/force_refresh_btn"_i18n,
        "akira/trophies/force_refresh_busy"_i18n,
        "akira/trophies/force_refresh_wait",
        [this]() {
            if (loading)
                return psn::ActionStatus{psn::ActionState::Busy, 0};

            return TrophyManager::getInstance()->forceRefreshStatus();
        }
    );

    forceRefreshBtn->registerClickAction([this](brls::View* view) {
        if (!forceRefreshGate.isReady())
            return true;

        brls::Logger::info("User triggered force refresh");

        TrophyManager* manager = TrophyManager::getInstance();
        PersistedRateLimiter::Status budget = manager->budgetStatus();
        brls::Application::notify(brls::getStr("akira/trophies/force_refresh",
            budget.used, budget.limit));

        manager->recordForcedRefresh();
        load(true);
        forceRefreshGate.apply();

        return true;
    });

    TrophyManager* trophies = TrophyManager::getInstance();

    trophies->setSummaryObserver([](const psn::TrophySummary& summary) {
        if (currentInstance)
            currentInstance->applySummary(summary);
    });

    trophies->setLibraryObserver([](const std::vector<psn::TrophyTitle>& titles) {
        if (currentInstance)
            currentInstance->applyTitles(titles);
    });

    trophies->startAutoRefresh();

    summaryTitleLabel->setText("akira/trophies/title"_i18n);
    summaryDetailLabel->setText("akira/trophies/loading"_i18n);
}

TrophyListTab::~TrophyListTab()
{
    if (currentInstance == this)
        currentInstance = nullptr;
}

brls::View* TrophyListTab::create()
{
    return new TrophyListTab();
}

void TrophyListTab::willAppear(bool resetState)
{
    Box::willAppear(resetState);

    forceRefreshGate.start();
    statusTimer.setCallback([this]() { refreshStatusLine(); });
    statusTimer.start(30 * 1000);
    refreshStatusLine();

    if (!gate->evaluate())
    {
        loadRequested = false;
        return;
    }

    if (!loadRequested)
        load(false);
}

void TrophyListTab::willDisappear(bool resetState)
{
    forceRefreshGate.stop();
    statusTimer.stop();

    Box::willDisappear(resetState);
}

void TrophyListTab::applySummary(const psn::TrophySummary& summary)
{
    summaryTitleLabel->setText(brls::getStr("akira/trophies/summary_level",
        summary.trophyLevel, summary.tier, summary.progress));

    summaryDetailLabel->setText(brls::getStr("akira/trophies/summary_counts",
        summary.earnedTrophies.total(), formatTrophyCounts(summary.earnedTrophies)));
}

void TrophyListTab::applyTitles(const std::vector<psn::TrophyTitle>& titles)
{
    this->titles = titles;
    rebuildGrid();
}

void TrophyListTab::rebuildGrid()
{
    std::vector<psn::TrophyTitle> rows;
    rows.reserve(titles.size());

    for (const psn::TrophyTitle& title : titles)
    {
        if (titleMatchesFilter(title, filterMode))
            rows.push_back(title);
    }

    switch (sortMode)
    {
        case TitleSort::Recent:
            std::stable_sort(rows.begin(), rows.end(),
                [](const psn::TrophyTitle& a, const psn::TrophyTitle& b) {
                    return a.lastUpdatedDateTime > b.lastUpdatedDateTime;
                });
            break;

        case TitleSort::Progress:
            std::stable_sort(rows.begin(), rows.end(),
                [](const psn::TrophyTitle& a, const psn::TrophyTitle& b) {
                    if (a.progress != b.progress)
                        return a.progress > b.progress;
                    return a.lastUpdatedDateTime > b.lastUpdatedDateTime;
                });
            break;

        case TitleSort::Name:
            std::stable_sort(rows.begin(), rows.end(),
                [](const psn::TrophyTitle& a, const psn::TrophyTitle& b) {
                    return a.trophyTitleName < b.trophyTitleName;
                });
            break;

        case TitleSort::Earned:
            std::stable_sort(rows.begin(), rows.end(),
                [](const psn::TrophyTitle& a, const psn::TrophyTitle& b) {
                    int aEarned = a.earnedTrophies.total();
                    int bEarned = b.earnedTrophies.total();

                    if (aEarned != bEarned)
                        return aEarned > bEarned;

                    return a.lastUpdatedDateTime > b.lastUpdatedDateTime;
                });
            break;
    }

    refreshControlLabels();
    refreshStatusLine();

    if (rows.empty())
    {
        grid->setEmpty(filterMode == TitleFilter::All
            ? "akira/trophies/empty"_i18n
            : "akira/trophies/empty_filter"_i18n);
        return;
    }

    grid->setDataSource(new TrophyGridDataSource(std::move(rows)));

    TrophyManager::getInstance()->resolveGameProgress(titles, []() {
        if (currentInstance)
            currentInstance->rebuildGrid();
    });
}

void TrophyListTab::refreshControlLabels()
{
    sortBtn->setText(brls::getStr("akira/trophies/sort_btn_value",
        brls::getStr(titleSortLabelKey(sortMode))));

    filterBtn->setText(brls::getStr("akira/trophies/filter_btn_value",
        brls::getStr(titleFilterLabelKey(filterMode))));
}

void TrophyListTab::refreshStatusLine()
{
    TrophyManager* manager = TrophyManager::getInstance();
    PersistedRateLimiter::Status budget = manager->budgetStatus();

    int64_t now = static_cast<int64_t>(std::time(nullptr));

    if (budget.breakerOpen(now))
    {
        statusLabel->setText(brls::getStr("akira/trophies/status_breaker",
            static_cast<int>((budget.breakerUntil - now + 59) / 60)));
        statusLabel->setTextColor(nvgRGB(248, 113, 113));
        return;
    }

    if (budget.remaining() <= 0)
    {
        statusLabel->setText(brls::getStr("akira/trophies/status_budget_spent",
            static_cast<int>((budget.bucketResetsAt - now + 59) / 60)));
        statusLabel->setTextColor(nvgRGB(248, 113, 113));
        return;
    }

    std::string age = formatRelativeAge(manager->librarySavedAtSeconds());

    statusLabel->setText(age.empty()
        ? brls::getStr("akira/trophies/status_budget", budget.used, budget.limit)
        : brls::getStr("akira/trophies/status_line", age, budget.used, budget.limit));

    statusLabel->setTextColor(budget.remaining() <= budget.limit / 5
        ? nvgRGB(251, 191, 36)
        : nvgRGB(122, 127, 136));
}

void TrophyListTab::showSortPicker()
{
    static const TitleSort options[] = {
        TitleSort::Recent,
        TitleSort::Progress,
        TitleSort::Name,
        TitleSort::Earned
    };

    std::vector<std::string> labels;
    int selected = 0;

    for (size_t i = 0; i < std::size(options); i++)
    {
        labels.push_back(brls::getStr(titleSortLabelKey(options[i])));
        if (options[i] == sortMode)
            selected = static_cast<int>(i);
    }

    auto* dropdown = new brls::Dropdown(
        "akira/trophies/sort_picker"_i18n,
        labels,
        [](int chosen) {
            if (!currentInstance || chosen < 0 || chosen >= static_cast<int>(std::size(options)))
                return;

            sortMode = options[chosen];
            currentInstance->rebuildGrid();
        },
        selected);

    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void TrophyListTab::showFilterPicker()
{
    static const TitleFilter options[] = {
        TitleFilter::All,
        TitleFilter::InProgress,
        TitleFilter::Completed,
        TitleFilter::Unstarted,
        TitleFilter::Hidden
    };

    std::vector<std::string> labels;
    int selected = 0;

    for (size_t i = 0; i < std::size(options); i++)
    {
        labels.push_back(brls::getStr(titleFilterLabelKey(options[i])));
        if (options[i] == filterMode)
            selected = static_cast<int>(i);
    }

    auto* dropdown = new brls::Dropdown(
        "akira/trophies/filter_picker"_i18n,
        labels,
        [](int chosen) {
            if (!currentInstance || chosen < 0 || chosen >= static_cast<int>(std::size(options)))
                return;

            filterMode = options[chosen];
            currentInstance->rebuildGrid();
        },
        selected);

    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void TrophyListTab::load(bool forceRefresh)
{
    if (loading)
    {
        brls::Logger::info("Trophy grid: load already in progress, ignoring request");
        return;
    }

    loading = true;
    loadRequested = true;

    if (titles.empty())
        grid->showSkeleton(12);

    TrophyManager* trophies = TrophyManager::getInstance();

    trophies->fetchSummary(forceRefresh,
        [](const psn::TrophySummary& summary) {
            if (currentInstance)
                currentInstance->applySummary(summary);
        },
        [](psn::Status status, const std::string& message) {
            brls::Logger::warning("Trophy grid: summary unavailable [{}] {}",
                psn::statusName(status), message);
            if (currentInstance)
                currentInstance->summaryDetailLabel->setText("akira/trophies/summary_unavailable"_i18n);
        });

    trophies->fetchLibrary(forceRefresh,
        [](const std::vector<psn::TrophyTitle>& titles) {
            if (!currentInstance)
                return;

            currentInstance->loading = false;
            currentInstance->applyTitles(titles);
        },
        [](psn::Status status, const std::string& message) {
            if (!currentInstance)
                return;

            currentInstance->loading = false;

            const char* key = "akira/trophies/error_generic";
            switch (status)
            {
                case psn::Status::NotLinked: key = "akira/trophies/error_not_linked"; break;
                case psn::Status::SessionExpired: key = "akira/trophies/error_session_expired"; break;
                case psn::Status::Offline: key = "akira/trophies/error_offline"; break;
                case psn::Status::RateLimited: key = "akira/trophies/error_rate_limited"; break;
                default: break;
            }

            brls::Logger::error("Trophy grid: load failed [{}] {}", psn::statusName(status), message);

            if (currentInstance->titles.empty())
                currentInstance->grid->setError(brls::getStr(key));
            else
                brls::Application::notify(brls::getStr(key));
        });
}
