#include "views/setup_account_view.hpp"

#include "views/pair_view.hpp"
#include "core/settings_manager.hpp"
#include "ui/theme.hpp"

using namespace brls::literals;

SetupAccountView::SetupAccountView() {
    this->inflateFromXMLRes("xml/views/setup_account.xml");

    if (glyph) {
        glyph->setText("\xEE\xA1\x93");
        glyph->setTextColor(akira::ui::active().accent);
    }

    if (startBtn) {
        startBtn->registerClickAction([](brls::View*) {
            brls::Application::pushActivity(new brls::Activity(new PairView(false)));
            return true;
        });
    }

    this->registerAction("akira/common/back"_i18n, brls::ControllerButton::BUTTON_B,
        [](brls::View*) {
            brls::Application::popActivity();
            return true;
        });
}

void SetupAccountView::willAppear(bool resetState) {
    Box::willAppear(resetState);
    if (!SettingsManager::getInstance()->getProfiles().empty())
        brls::Application::popActivity();
}

brls::View* SetupAccountView::getDefaultFocus() {
    return startBtn ? startBtn->getDefaultFocus() : brls::Box::getDefaultFocus();
}

brls::View* SetupAccountView::create() {
    return new SetupAccountView();
}
