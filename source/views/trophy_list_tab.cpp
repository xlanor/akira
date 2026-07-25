#include "views/trophy_list_tab.hpp"

#include <algorithm>
#include <format>

using namespace brls::literals;

TrophyListTab* TrophyListTab::currentInstance = nullptr;
std::unordered_set<TrophyCardCell*> TrophyCardCell::liveCells;

static constexpr float CARD_COVER_HEIGHT = 128;
static constexpr float CARD_ROW_HEIGHT = 216;

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

std::string formatTrophyCounts(const TrophyCounts& counts)
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
    this->setPadding(10, 10, 10, 10);
    this->setFocusable(true);

    cover = new brls::Image();
    cover->setHeight(CARD_COVER_HEIGHT);
    cover->setScalingType(brls::ImageScalingType::FILL);
    cover->setCornerRadius(6);
    cover->setBackgroundColor(brls::Application::getTheme().getColor("color/grey_3"));
    this->addView(cover);

    nameLabel = new brls::Label();
    nameLabel->setFontSize(16);
    nameLabel->setSingleLine(true);
    nameLabel->setMarginTop(8);
    this->addView(nameLabel);

    detailLabel = new brls::Label();
    detailLabel->setFontSize(13);
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

void TrophyCardCell::bindTitle(const TrophyTitle& title)
{
    nameLabel->setText(title.trophyTitleName.empty() ? title.npCommunicationId : title.trophyTitleName);

    detailLabel->setText(std::format("{}  ·  {}%  ·  {}/{}",
        formatTrophyPlatforms(title.trophyTitlePlatform),
        title.progress,
        title.earnedTrophies.total(),
        title.definedTrophies.total()));

    float ratio = std::clamp(title.progress / 100.0f, 0.0f, 1.0f);
    progressFill->setWidthPercentage(ratio * 100.0f);

    iconUrl = title.trophyTitleIconUrl;
    cover->clear();

    if (iconUrl.empty())
        return;

    TrophyManager::getInstance()->fetchIcon(iconUrl,
        [](const std::string& url, const std::vector<uint8_t>& bytes) {
            for (TrophyCardCell* cell : liveCells)
            {
                if (cell->iconUrl == url)
                    cell->cover->setImageFromMem(bytes.data(), static_cast<int>(bytes.size()));
            }
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

TrophyGridDataSource::TrophyGridDataSource(std::vector<TrophyTitle> titles)
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

    this->registerAction("akira/trophies/refresh"_i18n, brls::ControllerButton::BUTTON_X,
        [this](brls::View* view) {
            load(true);
            return true;
        });

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

    if (!loadRequested)
        load(false);
}

void TrophyListTab::applySummary(const TrophySummary& summary)
{
    summaryTitleLabel->setText(brls::getStr("akira/trophies/summary_level",
        summary.trophyLevel, summary.tier, summary.progress));

    summaryDetailLabel->setText(brls::getStr("akira/trophies/summary_counts",
        summary.earnedTrophies.total(), formatTrophyCounts(summary.earnedTrophies)));
}

void TrophyListTab::applyTitles(const std::vector<TrophyTitle>& titles)
{
    this->titles.clear();
    this->titles.reserve(titles.size());

    for (const TrophyTitle& title : titles)
    {
        if (title.hiddenFlag)
            continue;

        this->titles.push_back(title);
    }

    std::stable_sort(this->titles.begin(), this->titles.end(),
        [](const TrophyTitle& a, const TrophyTitle& b) {
            return a.lastUpdatedDateTime > b.lastUpdatedDateTime;
        });

    size_t hidden = titles.size() - this->titles.size();
    if (hidden > 0)
        brls::Logger::info("Trophy grid: {} hidden title(s) filtered out", hidden);

    if (this->titles.empty())
    {
        grid->setEmpty("akira/trophies/empty"_i18n);
        return;
    }

    // setDataSource takes ownership and deletes the previous source, so hand it a
    // fresh one built from our own copy rather than holding a pointer into it.
    grid->setDataSource(new TrophyGridDataSource(this->titles));
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
        [](const TrophySummary& summary) {
            if (currentInstance)
                currentInstance->applySummary(summary);
        },
        [](TrophyStatus status, const std::string& message) {
            brls::Logger::warning("Trophy grid: summary unavailable [{}] {}",
                trophyStatusName(status), message);
            if (currentInstance)
                currentInstance->summaryDetailLabel->setText("akira/trophies/summary_unavailable"_i18n);
        });

    trophies->fetchLibrary(forceRefresh,
        [](const std::vector<TrophyTitle>& titles) {
            if (!currentInstance)
                return;

            currentInstance->loading = false;
            currentInstance->applyTitles(titles);
        },
        [](TrophyStatus status, const std::string& message) {
            if (!currentInstance)
                return;

            currentInstance->loading = false;

            const char* key = "akira/trophies/error_generic";
            switch (status)
            {
                case TrophyStatus::NotLinked: key = "akira/trophies/error_not_linked"; break;
                case TrophyStatus::SessionExpired: key = "akira/trophies/error_session_expired"; break;
                case TrophyStatus::Offline: key = "akira/trophies/error_offline"; break;
                case TrophyStatus::RateLimited: key = "akira/trophies/error_rate_limited"; break;
                default: break;
            }

            brls::Logger::error("Trophy grid: load failed [{}] {}", trophyStatusName(status), message);

            if (currentInstance->titles.empty())
                currentInstance->grid->setError(brls::getStr(key));
            else
                brls::Application::notify(brls::getStr(key));
        });
}
