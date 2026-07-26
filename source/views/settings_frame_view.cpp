#include "views/settings_frame_view.hpp"
#include "views/settings_general_view.hpp"
#include "views/settings_picture_view.hpp"
#include "views/settings_controller_view.hpp"
#include "views/settings_debug_view.hpp"
#include "views/settings_account_view.hpp"
#include "views/settings_poweruser_view.hpp"
#include "core/settings_manager.hpp"
#include "core/settings_descriptions.hpp"

#include <borealis/core/i18n.hpp>

using namespace brls::literals;

SettingsFrameView::SettingsFrameView() {
    buildMenus();

    this->setAxis(brls::Axis::COLUMN);
    this->setWidthPercentage(100.0f);
    this->setHeightPercentage(100.0f);
    this->setBackgroundColor(brls::Application::getTheme()["brls/background"]);

    menuBar = new brls::Box();
    menuBar->setAxis(brls::Axis::ROW);
    menuBar->setPadding(18.0f, 40.0f, 12.0f, 40.0f);
    this->addView(menuBar);

    subBar = new brls::Box();
    subBar->setAxis(brls::Axis::ROW);
    subBar->setPaddingLeft(40.0f);
    subBar->setPaddingRight(40.0f);
    subBar->setPaddingBottom(10.0f);
    this->addView(subBar);

    auto* split = new brls::Box();
    split->setAxis(brls::Axis::ROW);
    split->setGrow(1.0f);
    split->setWidthPercentage(100.0f);

    contentHolder = new brls::Box();
    contentHolder->setAxis(brls::Axis::COLUMN);
    contentHolder->setWidthPercentage(70.0f);
    contentHolder->setHeightPercentage(100.0f);
    split->addView(contentHolder);

    auto* descPanel = new brls::Box();
    descPanel->setAxis(brls::Axis::COLUMN);
    descPanel->setWidthPercentage(30.0f);
    descPanel->setHeightPercentage(100.0f);
    descPanel->setPadding(34.0f, 34.0f, 34.0f, 34.0f);
    descPanel->setBackgroundColor(brls::Application::getTheme()["color/card"]);

    descTitle = new brls::Label();
    descTitle->setFontSize(20.0f);
    descTitle->setMarginBottom(12.0f);
    descTitle->setWidthPercentage(100.0f);
    descPanel->addView(descTitle);

    descBody = new brls::Label();
    descBody->setFontSize(15.0f);
    descBody->setTextColor(nvgRGBA(160, 170, 185, 255));
    descBody->setWidthPercentage(100.0f);
    descPanel->addView(descBody);

    descImage = new brls::Image();
    descImage->setWidthPercentage(100.0f);
    descImage->setMarginTop(18.0f);
    descImage->setVisibility(brls::Visibility::GONE);
    descPanel->addView(descImage);

    split->addView(descPanel);
    this->addView(split);

    renderMenuBar();
    renderSubBar();
    loadContent(false);

    focusSub = brls::Application::getGlobalFocusChangeEvent()->subscribe([this](brls::View*) {
        this->updateDescriptionFromFocus();
    });

    this->registerAction("", brls::ControllerButton::BUTTON_LB, [this](brls::View*) { switchMenu(-1); return true; }, true);
    this->registerAction("", brls::ControllerButton::BUTTON_RB, [this](brls::View*) { switchMenu(1); return true; }, true);
    this->registerAction("", brls::ControllerButton::BUTTON_LT, [this](brls::View*) { switchSub(-1); return true; }, true);
    this->registerAction("", brls::ControllerButton::BUTTON_RT, [this](brls::View*) { switchSub(1); return true; }, true);
    this->registerAction("akira/common/back"_i18n, brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, false);
}

SettingsFrameView::~SettingsFrameView() {
    brls::Application::getGlobalFocusChangeEvent()->unsubscribe(focusSub);
}

void SettingsFrameView::buildMenus() {
    menus.push_back({"General", {{"", []() -> brls::Box* { return new SettingsGeneralView(); }}}});
    menus.push_back({"Quality", {{"", []() -> brls::Box* { return new SettingsPictureView(); }}}});
    menus.push_back({"Controls", {{"", []() -> brls::Box* { return new SettingsControllerView(); }}}});
    menus.push_back({"Debug", {{"", []() -> brls::Box* { return new SettingsDebugView(); }}}});
    menus.push_back({"Account", {{"", []() -> brls::Box* { return new SettingsAccountView(); }}}});

    if (SettingsManager::getInstance()->getPowerUserMenuUnlocked()) {
        menus.push_back({"Power user", {{"", []() -> brls::Box* { return new SettingsPowerUserView(); }}}});
    }
}

void SettingsFrameView::renderMenuBar() {
    menuBar->clearViews();
    NVGcolor accent = brls::Application::getTheme()["brls/highlight/color1"];
    for (size_t i = 0; i < menus.size(); i++) {
        auto* lbl = new brls::Label();
        lbl->setText(menus[i].label);
        lbl->setFontSize(18.0f);
        lbl->setMarginRight(28.0f);
        lbl->setTextColor(static_cast<int>(i) == activeMenu ? accent : nvgRGBA(130, 140, 155, 255));
        menuBar->addView(lbl);
    }
}

void SettingsFrameView::renderSubBar() {
    subBar->clearViews();
    auto& subs = menus[activeMenu].subs;
    bool hasNamed = subs.size() > 1;
    subBar->setVisibility(hasNamed ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    if (!hasNamed)
        return;

    NVGcolor accent = brls::Application::getTheme()["brls/highlight/color1"];
    for (size_t i = 0; i < subs.size(); i++) {
        auto* lbl = new brls::Label();
        lbl->setText(subs[i].name);
        lbl->setFontSize(15.0f);
        lbl->setMarginRight(22.0f);
        lbl->setTextColor(static_cast<int>(i) == activeSub ? accent : nvgRGBA(130, 140, 155, 255));
        subBar->addView(lbl);
    }
}

void SettingsFrameView::loadContent(bool focus) {
    contentHolder->clearViews();
    auto* v = menus[activeMenu].subs[activeSub].make();
    contentHolder->addView(v);
    if (focus) {
        brls::View* def = v->getDefaultFocus();
        if (def)
            brls::Application::giveFocus(def);
    }
}

void SettingsFrameView::switchMenu(int delta) {
    int n = static_cast<int>(menus.size());
    activeMenu = ((activeMenu + delta) % n + n) % n;
    activeSub = 0;
    renderMenuBar();
    renderSubBar();
    loadContent(true);
}

void SettingsFrameView::switchSub(int delta) {
    int s = static_cast<int>(menus[activeMenu].subs.size());
    if (s <= 1)
        return;
    activeSub = ((activeSub + delta) % s + s) % s;
    renderSubBar();
    loadContent(true);
}

void SettingsFrameView::updateDescriptionFromFocus() {
    brls::View* f = brls::Application::getCurrentFocus();
    while (f) {
        akira::SettingDescription d;
        if (akira::lookupSettingDescription(f->getId(), d)) {
            descTitle->setText(d.title);
            descBody->setText(d.body);
            if (!d.image.empty()) {
                descImage->setImageFromRes(d.image);
                descImage->setVisibility(brls::Visibility::VISIBLE);
            } else {
                descImage->setVisibility(brls::Visibility::GONE);
            }
            return;
        }
        f = f->getParent();
    }
}
