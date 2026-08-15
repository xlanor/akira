#include "views/profile_card_view.hpp"
#include "cloud/service.hpp"
#include "ui/theme.hpp"
#include "core/trophy_manager.hpp"

#include <algorithm>
#include <format>

using namespace brls::literals;

namespace {

std::string cloudSummary(const cloud::Status& status)
{
    switch (status.availability)
    {
        case cloud::Availability::Ready:
            return "akira/cloud/chip_ready"_i18n;
        case cloud::Availability::Warning:
            return "akira/cloud/chip_warning"_i18n;
        case cloud::Availability::Empty:
            return "akira/cloud/chip_empty"_i18n;
        case cloud::Availability::NeedsPairing:
            return "akira/cloud/chip_pair"_i18n;
        case cloud::Availability::Checking:
            return "akira/cloud/chip_checking"_i18n;
        case cloud::Availability::LaunchBlocked:
        case cloud::Availability::Error:
            return "akira/cloud/chip_error"_i18n;
        case cloud::Availability::NoProfile:
        default:
            return "";
    }
}

} // namespace

ProfileCardView::ProfileCardView() {
    this->inflateFromXMLRes("xml/profile_card.xml");
    settings = SettingsManager::getInstance();

    this->setAlignItems(brls::AlignItems::CENTER);
    this->setBackgroundColor(akira::ui::active().surface);

    avatar->setScalingType(brls::ImageScalingType::FILL);
    avatar->setBackgroundColor(akira::ui::active().surfaceElevated);
    avatar->setCornerRadius(50);

    track->setBackgroundColor(akira::ui::active().surfaceLine);
    progress->setColor(akira::ui::active().accentStrong);
    progress->setCornerRadius(3);

    plusLabel->setTextColor(akira::ui::active().warning);
    statusLabel->setTextColor(akira::ui::active().textMuted);
    levelLabel->setTextColor(akira::ui::active().textMuted);
    platLabel->setTextColor(akira::ui::active().text);
    goldLabel->setTextColor(akira::ui::active().text);
    silverLabel->setTextColor(akira::ui::active().text);
    bronzeLabel->setTextColor(akira::ui::active().text);

    platImg->setImageFromRes("img/trophy/platinum.png");
    goldImg->setImageFromRes("img/trophy/gold.png");
    silverImg->setImageFromRes("img/trophy/silver.png");
    bronzeImg->setImageFromRes("img/trophy/bronze.png");
    platImg->setScalingType(brls::ImageScalingType::FIT);
    goldImg->setScalingType(brls::ImageScalingType::FIT);
    silverImg->setScalingType(brls::ImageScalingType::FIT);
    bronzeImg->setScalingType(brls::ImageScalingType::FIT);

    refresh();
}

ProfileCardView::~ProfileCardView() {
    *alive = false;
}

void ProfileCardView::refresh() {
    const Profile* p = settings->getActiveProfile();

    std::string display;
    if (!p)
        display = "akira/settings/no_active_profile"_i18n;
    else if (p->label().empty())
        display = "akira/settings/unnamed_profile"_i18n;
    else
        display = p->label();
    onlineIdLabel->setText(display);

    std::string status = p && p->isRemote()
        ? "akira/settings/profile_remote"_i18n
        : "akira/settings/profile_local"_i18n;
    if (p)
    {
        std::string cloud = cloudSummary(cloud::Service::instance().snapshotForActiveProfile().status);
        if (!cloud.empty())
            status += "  ·  " + cloud;
    }
    statusLabel->setText(status);
    plusLabel->setVisibility(brls::Visibility::GONE);
    trophyCol->setVisibility(brls::Visibility::GONE);
    levelLabel->setText("");
    progress->setWidthPercentage(0.0f);
    platLabel->setText("");
    goldLabel->setText("");
    silverLabel->setText("");
    bronzeLabel->setText("");

    if (!p)
        return;

    auto guard = alive;

    TrophyManager::getInstance()->fetchProfile(false,
        [this, guard](const psn::PsnProfile& prof) {
            if (!*guard)
                return;
            if (!prof.onlineId.empty())
                onlineIdLabel->setText(prof.onlineId);
            plusLabel->setVisibility(prof.isPlus ? brls::Visibility::VISIBLE : brls::Visibility::GONE);

            std::string url = prof.avatarUrl();
            if (url.empty())
                return;

            auto inner = guard;
            TrophyManager::getInstance()->fetchIcon(url,
                [this, inner](const std::string&, const std::vector<uint8_t>& bytes) {
                    if (!*inner)
                        return;
                    if (!bytes.empty())
                        avatar->setImageFromMem(bytes.data(), static_cast<int>(bytes.size()));
                });
        },
        [](psn::Status, const std::string&) {});

    TrophyManager::getInstance()->fetchSummary(false,
        [this, guard](const psn::TrophySummary& summary) {
            if (!*guard)
                return;
            trophyCol->setVisibility(brls::Visibility::VISIBLE);
            levelLabel->setText(std::format("{} {} · {}%",
                "akira/settings/trophy_level"_i18n, summary.trophyLevel, summary.progress));

            int pct = std::max(0, std::min(100, summary.progress));
            progress->setWidthPercentage(static_cast<float>(pct));

            platLabel->setText(std::to_string(summary.earnedTrophies.platinum));
            goldLabel->setText(std::to_string(summary.earnedTrophies.gold));
            silverLabel->setText(std::to_string(summary.earnedTrophies.silver));
            bronzeLabel->setText(std::to_string(summary.earnedTrophies.bronze));

            platImg->setAlpha(summary.earnedTrophies.platinum > 0 ? 1.0f : 0.4f);
            goldImg->setAlpha(summary.earnedTrophies.gold > 0 ? 1.0f : 0.4f);
            silverImg->setAlpha(summary.earnedTrophies.silver > 0 ? 1.0f : 0.4f);
            bronzeImg->setAlpha(summary.earnedTrophies.bronze > 0 ? 1.0f : 0.4f);
        },
        [](psn::Status, const std::string&) {});
}
