#include "views/recently_played_rail.hpp"
#include "ui/theme.hpp"
#include "ui/motion.hpp"
#include "core/trophy_manager.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

using namespace brls::literals;

namespace {
std::string platformLabel(const std::string& category) {
    if (category.find("ps5") != std::string::npos)
        return "PS5";
    if (category.find("ps4") != std::string::npos)
        return "PS4";
    return "";
}

std::string playtimeLabel(int64_t seconds) {
    if (seconds <= 0)
        return "";
    int64_t hours = seconds / 3600;
    if (hours >= 1)
        return std::to_string(hours) + "h";
    int64_t mins = seconds / 60;
    if (mins >= 1)
        return std::to_string(mins) + "m";
    return "";
}

int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = static_cast<unsigned>(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

std::string relativeTime(const std::string& iso) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 3)
        return "";
    if (y <= 0 || mo <= 0 || d <= 0)
        return "";

    int64_t when = daysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400
                   + h * 3600 + mi * 60 + s;
    int64_t now = static_cast<int64_t>(std::time(nullptr));
    int64_t delta = now - when;
    if (delta < 0)
        return "";
    if (delta < 60)
        return "just now";
    if (delta < 3600)
        return std::to_string(delta / 60) + "m ago";
    if (delta < 86400)
        return std::to_string(delta / 3600) + "h ago";
    if (delta < 604800)
        return std::to_string(delta / 86400) + "d ago";
    if (delta < 2629800)
        return std::to_string(delta / 604800) + "w ago";
    if (delta < 31557600)
        return std::to_string(delta / 2629800) + "mo ago";
    return std::to_string(delta / 31557600) + "y ago";
}

std::string metaLine(const psn::PlayedGame& game) {
    std::vector<std::string> parts;
    std::string platform = platformLabel(game.category);
    std::string recent = relativeTime(game.lastPlayedDateTime);
    std::string playtime = playtimeLabel(game.playDurationSeconds);
    if (!platform.empty())
        parts.push_back(platform);
    if (!recent.empty())
        parts.push_back(recent);
    if (!playtime.empty())
        parts.push_back(playtime);

    std::string out;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0)
            out += "  ·  ";
        out += parts[i];
    }
    return out;
}
}

RecentlyPlayedRail::RecentlyPlayedRail() {
    this->setAxis(brls::Axis::COLUMN);
    this->setMarginTop(24);
    this->setVisibility(brls::Visibility::GONE);

    titleLabel = new brls::Label();
    titleLabel->setText("akira/hosts/recently_played"_i18n);
    titleLabel->setFontSize(24);
    titleLabel->setMarginBottom(22);
    this->addView(titleLabel);

    railRow = new brls::Box();
    railRow->setAxis(brls::Axis::ROW);
    railRow->setAlignItems(brls::AlignItems::FLEX_START);
    railRow->setWidthPercentage(100);
    this->addView(railRow);

    refresh();
}

RecentlyPlayedRail::~RecentlyPlayedRail() {
    *alive = false;
}

void RecentlyPlayedRail::setResumeHandler(std::function<void(const psn::PlayedGame&)> handler) {
    resumeHandler = std::move(handler);
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

            auto fetchInto = [this, guard, gen](brls::Image* cover, const std::string& url) {
                if (url.empty())
                    return;
                auto inner = guard;
                TrophyManager::getInstance()->fetchIcon(url,
                    [this, inner, gen, cover](const std::string&, const std::vector<uint8_t>& bytes) {
                        if (!*inner || gen != generation)
                            return;
                        if (!bytes.empty())
                            cover->setImageFromMem(bytes.data(), static_cast<int>(bytes.size()));
                    });
            };

            size_t count = std::min(static_cast<size_t>(5), games.size());

            for (size_t i = 0; i < count; i++) {
                const psn::PlayedGame& game = games[i];

                auto* card = new akira::ui::motion::LiftBox();
                card->setAxis(brls::Axis::COLUMN);
                card->setGrow(1.0f);
                if (i + 1 < count)
                    card->setMarginRight(16);
                card->setCornerRadius(14);
                card->setBackgroundColor(akira::ui::active().surface);
                card->setPadding(12.0f, 12.0f, 12.0f, 12.0f);
                card->setFocusable(true);
                card->setCustomNavigationRoute(brls::FocusDirection::UP, "host/container");
                psn::PlayedGame gameCopy = game;
                card->registerClickAction([this, gameCopy](brls::View*) {
                    if (resumeHandler)
                        resumeHandler(gameCopy);
                    return true;
                });

                auto* cover = new brls::Image();
                cover->setWidthPercentage(100.0f);
                cover->setHeight(206.0f);
                cover->setCornerRadius(10);
                cover->setScalingType(brls::ImageScalingType::FILL);
                cover->setBackgroundColor(akira::ui::active().surfaceElevated);
                card->addView(cover);

                auto* name = new brls::Label();
                name->setText(game.name);
                name->setFontSize(22);
                name->setMarginTop(10);
                name->setWidthPercentage(100.0f);
                name->setSingleLine(true);
                name->setAutoAnimate(true);
                name->setTextColor(akira::ui::active().text);
                card->addView(name);

                std::string meta = metaLine(game);
                if (!meta.empty()) {
                    auto* metaLabel = new brls::Label();
                    metaLabel->setText(meta);
                    metaLabel->setFontSize(17);
                    metaLabel->setMarginTop(3);
                    metaLabel->setWidthPercentage(100.0f);
                    metaLabel->setSingleLine(true);
                    metaLabel->setTextColor(akira::ui::active().textDim);
                    card->addView(metaLabel);
                }

                railRow->addView(card);
                fetchInto(cover, game.imageUrl);
            }
        },
        [this, guard, gen](psn::Status, const std::string&) {
            if (!*guard || gen != generation)
                return;
            this->setVisibility(brls::Visibility::GONE);
        });
}
