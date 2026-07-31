#include "views/profile_chip_view.hpp"
#include "ui/theme.hpp"
#include "core/trophy_manager.hpp"

using namespace brls::literals;

ProfileChipView::ProfileChipView() {
    this->inflateFromXMLRes("xml/profile_chip.xml");
    settings = SettingsManager::getInstance();

    this->setAlignItems(brls::AlignItems::CENTER);
    this->setBackgroundColor(brls::Application::getTheme()["color/card"]);

    avatar->setScalingType(brls::ImageScalingType::FILL);
    avatar->setBackgroundColor(akira::ui::active().surfaceElevated);
    avatar->setCornerRadius(18);

    subLabel->setTextColor(akira::ui::active().textMuted);
    plusLabel->setTextColor(akira::ui::active().warning);

    refresh();
}

ProfileChipView::~ProfileChipView() {
    *alive = false;
}

void ProfileChipView::refresh() {
    const Profile* p = settings->getActiveProfile();

    std::string name;
    if (!p)
        name = "akira/settings/no_active_profile"_i18n;
    else if (p->label().empty())
        name = "akira/settings/unnamed_profile"_i18n;
    else
        name = p->label();
    nameLabel->setText(name);

    subLabel->setText(p && p->isRemote()
        ? "akira/settings/profile_remote"_i18n
        : "akira/settings/profile_local"_i18n);
    plusLabel->setVisibility(brls::Visibility::GONE);

    if (!p)
        return;

    auto guard = alive;

    TrophyManager::getInstance()->fetchProfile(false,
        [this, guard](const psn::PsnProfile& prof) {
            if (!*guard)
                return;
            if (!prof.onlineId.empty())
                nameLabel->setText(prof.onlineId);
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
}
