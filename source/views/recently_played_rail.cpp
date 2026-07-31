#include "views/recently_played_rail.hpp"
#include "ui/theme.hpp"
#include "ui/motion.hpp"
#include "core/trophy_manager.hpp"

#include <algorithm>

using namespace brls::literals;

RecentlyPlayedRail::RecentlyPlayedRail() {
    this->setAxis(brls::Axis::COLUMN);
    this->setMarginTop(24);
    this->setVisibility(brls::Visibility::GONE);

    titleLabel = new brls::Label();
    titleLabel->setText("akira/hosts/recently_played"_i18n);
    titleLabel->setFontSize(20);
    titleLabel->setMarginBottom(12);
    this->addView(titleLabel);

    railRow = new brls::Box();
    railRow->setAxis(brls::Axis::ROW);
    this->addView(railRow);

    refresh();
}

RecentlyPlayedRail::~RecentlyPlayedRail() {
    *alive = false;
}

void RecentlyPlayedRail::refresh() {
    auto guard = alive;
    int gen = ++generation;

    TrophyManager::getInstance()->fetchPlayedGames(false,
        [this, guard, gen](const std::vector<psn::PlayedGame>& games) {
            if (!*guard || gen != generation)
                return;

            railRow->clearViews();

            if (games.empty()) {
                this->setVisibility(brls::Visibility::GONE);
                railShown = false;
                return;
            }
            this->setVisibility(brls::Visibility::VISIBLE);
            if (!railShown) {
                railShown = true;
                akira::ui::motion::fadeIn(this, entranceAnim, 0, 260);
            }

            size_t count = std::min(static_cast<size_t>(5), games.size());
            for (size_t i = 0; i < count; i++) {
                const psn::PlayedGame& game = games[i];

                auto* tile = new akira::ui::motion::LiftBox();
                tile->setAxis(brls::Axis::COLUMN);
                tile->setWidth(132);
                tile->setMarginRight(14);
                tile->setFocusable(true);
                tile->setCornerRadius(10);
                std::string toast = game.name;
                tile->registerClickAction([toast](brls::View*) {
                    brls::Application::notify(toast);
                    return true;
                });

                auto* cover = new brls::Image();
                cover->setWidth(132);
                cover->setHeight(132);
                cover->setCornerRadius(10);
                cover->setScalingType(brls::ImageScalingType::FILL);
                cover->setBackgroundColor(akira::ui::active().surfaceElevated);
                tile->addView(cover);

                auto* name = new brls::Label();
                name->setText(game.name);
                name->setFontSize(13);
                name->setMarginTop(7);
                name->setWidth(132);
                tile->addView(name);

                railRow->addView(tile);

                if (!game.imageUrl.empty()) {
                    auto inner = guard;
                    TrophyManager::getInstance()->fetchIcon(game.imageUrl,
                        [this, inner, gen, cover](const std::string&, const std::vector<uint8_t>& bytes) {
                            if (!*inner || gen != generation)
                                return;
                            if (!bytes.empty())
                                cover->setImageFromMem(bytes.data(), static_cast<int>(bytes.size()));
                        });
                }
            }
        },
        [this, guard, gen](psn::Status, const std::string&) {
            if (!*guard || gen != generation)
                return;
            this->setVisibility(brls::Visibility::GONE);
        });
}
