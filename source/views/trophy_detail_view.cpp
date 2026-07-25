#include "views/trophy_detail_view.hpp"
#include "views/trophy_list_tab.hpp"

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

TrophyDetailView* TrophyDetailView::currentInstance = nullptr;
std::unordered_set<TrophyRowCell*> TrophyRowCell::liveCells;
std::unordered_set<std::string> TrophyRowCell::retriedIcons;

static constexpr float ROW_ICON_SIZE = 64;
static constexpr float ROW_HEIGHT = 92;

static const NVGcolor EARNED_COLOR = nvgRGB(74, 222, 128);
static const NVGcolor PROGRESS_COLOR = nvgRGB(92, 157, 255);
static const NVGcolor MUTED_COLOR = nvgRGB(150, 150, 150);

std::string formatTrophyRarity(const psn::Trophy& trophy)
{
    const char* key = nullptr;

    switch (psn::rarityOf(trophy.trophyRare))
    {
        case psn::TrophyRarity::UltraRare: key = "akira/trophies/rarity_ultra_rare"; break;
        case psn::TrophyRarity::VeryRare: key = "akira/trophies/rarity_very_rare"; break;
        case psn::TrophyRarity::Rare: key = "akira/trophies/rarity_rare"; break;
        case psn::TrophyRarity::Common: key = "akira/trophies/rarity_common"; break;
    }

    if (trophy.trophyEarnedRate <= 0.0)
        return brls::getStr(key);

    return std::format("{}  ·  {:.1f}%", brls::getStr(key), trophy.trophyEarnedRate);
}

static std::string trophyTypeLabel(const std::string& type)
{
    if (type == "platinum")
        return "akira/trophies/type_platinum"_i18n;
    if (type == "gold")
        return "akira/trophies/type_gold"_i18n;
    if (type == "silver")
        return "akira/trophies/type_silver"_i18n;
    if (type == "bronze")
        return "akira/trophies/type_bronze"_i18n;

    return type;
}

static std::string formatEarnedDate(const std::string& iso)
{
    if (iso.size() < 10)
        return std::string();

    return iso.substr(0, 10);
}

TrophyRowCell::TrophyRowCell()
{
    this->setAxis(brls::Axis::ROW);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setCornerRadius(8);
    this->setBackgroundColor(brls::Application::getTheme().getColor("color/card"));
    this->setPadding(10, 14, 10, 14);
    this->setHeight(ROW_HEIGHT);
    this->setFocusable(true);

    icon = new brls::Image();
    icon->setWidth(ROW_ICON_SIZE);
    icon->setHeight(ROW_ICON_SIZE);
    icon->setScalingType(brls::ImageScalingType::FILL);
    icon->setCornerRadius(6);
    icon->setBackgroundColor(brls::Application::getTheme().getColor("color/grey_3"));
    this->addView(icon);

    auto* text = new brls::Box(brls::Axis::COLUMN);
    text->setGrow(1.0f);
    text->setMarginLeft(14);

    nameLabel = new brls::Label();
    nameLabel->setFontSize(17);
    nameLabel->setSingleLine(true);
    text->addView(nameLabel);

    detailLabel = new brls::Label();
    detailLabel->setFontSize(13);
    detailLabel->setTextColor(MUTED_COLOR);
    detailLabel->setSingleLine(true);
    detailLabel->setMarginTop(2);
    text->addView(detailLabel);

    progressTrack = new brls::Box(brls::Axis::ROW);
    progressTrack->setHeight(4);
    progressTrack->setCornerRadius(2);
    progressTrack->setBackgroundColor(brls::Application::getTheme().getColor("color/grey_3"));
    progressTrack->setMarginTop(7);
    progressTrack->setWidthPercentage(60);

    progressFill = new brls::Rectangle();
    progressFill->setHeight(4);
    progressFill->setCornerRadius(2);
    progressFill->setColor(PROGRESS_COLOR);
    progressTrack->addView(progressFill);

    text->addView(progressTrack);
    this->addView(text);

    auto* badges = new brls::Box(brls::Axis::COLUMN);
    badges->setAlignItems(brls::AlignItems::FLEX_END);
    badges->setMarginLeft(12);

    stateLabel = new brls::Label();
    stateLabel->setFontSize(14);
    stateLabel->setSingleLine(true);
    badges->addView(stateLabel);

    rarityLabel = new brls::Label();
    rarityLabel->setFontSize(12);
    rarityLabel->setTextColor(MUTED_COLOR);
    rarityLabel->setSingleLine(true);
    rarityLabel->setMarginTop(4);
    badges->addView(rarityLabel);

    this->addView(badges);

    liveCells.insert(this);
}

TrophyRowCell::~TrophyRowCell()
{
    liveCells.erase(this);
}

void TrophyRowCell::bindTrophy(const psn::Trophy& trophy)
{
    bool concealed = trophy.trophyHidden && !trophy.earned;

    nameLabel->setText(concealed ? "akira/trophies/hidden_name"_i18n : trophy.trophyName);
    detailLabel->setText(concealed ? "akira/trophies/hidden_detail"_i18n : trophy.trophyDetail);

    if (trophy.earned)
    {
        std::string date = formatEarnedDate(trophy.earnedDateTime);
        stateLabel->setText(date.empty()
            ? std::format("{}  {}", trophyTypeLabel(trophy.trophyType), "akira/trophies/earned"_i18n)
            : std::format("{}  {}", trophyTypeLabel(trophy.trophyType), date));
        stateLabel->setTextColor(EARNED_COLOR);
    }
    else
    {
        stateLabel->setText(trophyTypeLabel(trophy.trophyType));
        stateLabel->setTextColor(MUTED_COLOR);
    }

    rarityLabel->setText(formatTrophyRarity(trophy));

    bool showProgress = trophy.hasProgress && trophy.progressTarget > 0 && !trophy.earned;
    progressTrack->setVisibility(showProgress ? brls::Visibility::VISIBLE : brls::Visibility::GONE);

    if (showProgress)
    {
        float ratio = static_cast<float>(trophy.progress) / static_cast<float>(trophy.progressTarget);
        progressFill->setWidthPercentage(std::clamp(ratio, 0.0f, 1.0f) * 100.0f);

        detailLabel->setText(std::format("{}  ·  {}/{}",
            concealed ? "akira/trophies/hidden_detail"_i18n : trophy.trophyDetail,
            trophy.progress, trophy.progressTarget));
    }

    iconUrl = trophy.trophyIconUrl;
    icon->clear();

    if (iconUrl.empty())
        return;

    TrophyManager::getInstance()->fetchIcon(iconUrl,
        [](const std::string& url, const std::vector<uint8_t>& bytes) {
            bool decodeFailed = false;

            for (TrophyRowCell* cell : liveCells)
            {
                if (cell->iconUrl != url)
                    continue;

                cell->icon->setImageFromMem(bytes.data(), static_cast<int>(bytes.size()));

                if (cell->icon->getTexture() <= 0)
                    decodeFailed = true;
            }

            if (!decodeFailed)
                return;

            brls::Logger::warning("Trophy icon failed to decode ({} bytes) {}", bytes.size(), url);

            if (retriedIcons.insert(url).second)
                TrophyManager::getInstance()->discardIcon(url);
        });
}

void TrophyRowCell::prepareForReuse()
{
    iconUrl.clear();
    icon->clear();
}

void TrophyRowCell::cacheForReuse()
{
    iconUrl.clear();
    icon->clear();
}

RecyclingGridItem* TrophyRowCell::create()
{
    return new TrophyRowCell();
}

TrophyRowDataSource::TrophyRowDataSource(std::vector<psn::Trophy> trophies)
    : trophies(std::move(trophies))
{
}

size_t TrophyRowDataSource::getItemCount()
{
    return trophies.size();
}

RecyclingGridItem* TrophyRowDataSource::cellForRow(RecyclingView* recycler, size_t index)
{
    auto* cell = dynamic_cast<TrophyRowCell*>(recycler->dequeueReusableCell("TrophyRow"));
    if (!cell)
        return nullptr;

    if (index < trophies.size())
        cell->bindTrophy(trophies[index]);

    return cell;
}

void TrophyRowDataSource::clearData()
{
    trophies.clear();
}

TrophyDetailView::TrophyDetailView(const psn::TrophyTitle& title)
    : title(title)
{
    this->inflateFromXMLRes("xml/views/trophy_detail.xml");

    currentInstance = this;

    list->registerCell("TrophyRow", TrophyRowCell::create);
    list->estimatedRowHeight = ROW_HEIGHT;

    titleLabel->setText(title.trophyTitleName.empty() ? title.npCommunicationId : title.trophyTitleName);
    subtitleLabel->setText("akira/trophies/loading"_i18n);
    countsLabel->setText("");

    groupBtn->setStyle(&BUTTONSTYLE_BLUE);
    groupBtn->setBackgroundColor(nvgRGBA(72, 76, 84, 255));
    groupBtn->setVisibility(brls::Visibility::GONE);
    groupBtn->registerClickAction([this](brls::View* view) {
        showGroupPicker();
        return true;
    });

    refreshBtn->setStyle(&BUTTONSTYLE_BLUE);

    refreshGate.attach(
        refreshBtn,
        nvgRGBA(92, 157, 255, 255),
        "akira/trophies/force_refresh_btn"_i18n,
        "akira/trophies/force_refresh_busy"_i18n,
        "akira/trophies/force_refresh_wait",
        [this]() {
            if (loading)
                return psn::ActionStatus{psn::ActionState::Busy, 0};

            return TrophyManager::getInstance()->forceRefreshStatus(this->title.npCommunicationId);
        }
    );

    refreshBtn->registerClickAction([this](brls::View* view) {
        if (!refreshGate.isReady())
            return true;

        brls::Logger::info("User triggered force refresh of {}", this->title.npCommunicationId);

        TrophyManager* manager = TrophyManager::getInstance();
        PersistedRateLimiter::Status budget = manager->budgetStatus();
        brls::Application::notify(brls::getStr("akira/trophies/force_refresh",
            budget.used, budget.limit));

        manager->recordForcedRefresh(this->title.npCommunicationId);
        load(true);
        refreshGate.apply();

        return true;
    });

    this->registerAction("akira/common/back"_i18n, brls::ControllerButton::BUTTON_B,
        [](brls::View* view) {
            brls::Application::popActivity();
            return true;
        });
}

TrophyDetailView::~TrophyDetailView()
{
    if (currentInstance == this)
        currentInstance = nullptr;
}

void TrophyDetailView::willAppear(bool resetState)
{
    Box::willAppear(resetState);

    refreshGate.start();

    if (!loadRequested)
        load(false);
}

void TrophyDetailView::willDisappear(bool resetState)
{
    refreshGate.stop();

    Box::willDisappear(resetState);
}

void TrophyDetailView::load(bool forceRefresh)
{
    loading = true;
    loadRequested = true;

    subtitleLabel->setText("akira/trophies/loading"_i18n);

    TrophyManager::getInstance()->fetchTitleDetail(title, forceRefresh,
        [](const psn::TitleDetail& loaded) {
            if (currentInstance)
            {
                currentInstance->loading = false;
                currentInstance->applyDetail(loaded);
            }
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

            currentInstance->subtitleLabel->setText(brls::getStr(key));
            currentInstance->list->setError(brls::getStr(key));

            brls::Logger::error("Trophy detail: load failed [{}] {}", psn::statusName(status), message);
        });
}

void TrophyDetailView::applyDetail(const psn::TitleDetail& loaded)
{
    detail = loaded;
    selectedGroup = 0;

    groupBtn->setVisibility(detail.groups.size() > 1
        ? brls::Visibility::VISIBLE
        : brls::Visibility::GONE);

    applyGroup(0);
}

void TrophyDetailView::applyGroup(size_t index)
{
    if (detail.groups.empty())
        return;

    selectedGroup = std::min(index, detail.groups.size() - 1);
    const psn::TrophyGroup& group = detail.groups[selectedGroup];

    groupBtn->setText(group.trophyGroupName.empty()
        ? "akira/trophies/group_all"_i18n
        : group.trophyGroupName);

    subtitleLabel->setText(std::format("{}  ·  {}%  ·  {}/{}",
        formatTrophyPlatforms(title.trophyTitlePlatform),
        group.progress,
        group.earnedTrophies.total(),
        group.definedTrophies.total()));

    countsLabel->setText(formatTrophyCounts(group.earnedTrophies));

    std::vector<psn::Trophy> rows;
    rows.reserve(detail.trophies.size());

    bool singleGroup = detail.groups.size() <= 1;
    for (const psn::Trophy& trophy : detail.trophies)
    {
        if (singleGroup || trophy.trophyGroupId == group.trophyGroupId)
            rows.push_back(trophy);
    }

    std::stable_sort(rows.begin(), rows.end(),
        [](const psn::Trophy& a, const psn::Trophy& b) {
            bool aInProgress = !a.earned && a.hasProgress && a.progressRate > 0;
            bool bInProgress = !b.earned && b.hasProgress && b.progressRate > 0;

            if (aInProgress != bInProgress)
                return aInProgress;

            if (aInProgress && bInProgress)
                return a.progressRate > b.progressRate;

            if (a.earned != b.earned)
                return !a.earned;

            return a.trophyId < b.trophyId;
        });

    if (rows.empty())
    {
        list->setEmpty("akira/trophies/empty_group"_i18n);
        return;
    }

    list->setDataSource(new TrophyRowDataSource(std::move(rows)));
}

void TrophyDetailView::showGroupPicker()
{
    if (detail.groups.size() <= 1)
        return;

    std::vector<std::string> labels;
    labels.reserve(detail.groups.size());

    for (const psn::TrophyGroup& group : detail.groups)
    {
        labels.push_back(group.trophyGroupName.empty()
            ? group.trophyGroupId
            : group.trophyGroupName);
    }

    auto* dropdown = new brls::Dropdown(
        "akira/trophies/group_picker"_i18n,
        labels,
        [](int selected) {
            if (currentInstance)
                currentInstance->applyGroup(static_cast<size_t>(selected));
        },
        static_cast<int>(selectedGroup));

    brls::Application::pushActivity(new brls::Activity(dropdown));
}
