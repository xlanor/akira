#include "views/settings_controller_view.hpp"
#include "views/controller_remap_view.hpp"
#include "input/extended_input_manager.hpp"
#include "input/pad_path.hpp"
#include "views/settings_controllers_view.hpp"

#include <borealis/core/i18n.hpp>
#include <algorithm>

using namespace brls::literals;

SettingsControllerView::SettingsControllerView() {
    this->inflateFromXMLRes("xml/settings/controller.xml");

    settings = SettingsManager::getInstance();

    initSleepOnExitToggle();
    initConnectedControllers();
    initButtonMappingCell();

}

void SettingsControllerView::initSleepOnExitToggle() {
    bool currentValue = settings->getSleepOnExit();

    sleepOnExitToggle->init(
        "akira/settings/sleep_on_exit"_i18n,
        currentValue,
        [this](bool isOn) {
            settings->setSleepOnExit(isOn);
            settings->writeFile();
        }
    );
}

/*
 * A readout, not a control.
 *
 * There is no toggle because installing the sysmodule is the opt-in and
 * Ultrahand can stop it - a config flag would only make Akira decline to
 * subscribe, which is the weaker of the two. What is still worth showing is
 * the answer to "is this working?", which is otherwise invisible until a
 * stream starts.
 */
void SettingsControllerView::initConnectedControllers()
{
    connectedControllersCell->setText("akira/settings/connected_controllers"_i18n);

    /* Counted rather than named, because naming one of three is a choice this
     * row has no basis for making - the list behind it can name them all. */
    const size_t count = akira::input::DescribePads().size();
    connectedControllersCell->setDetailText(
        count == 0 ? "akira/settings/connected_controllers_none"_i18n
                   : std::to_string(count));

    connectedControllersCell->registerClickAction([](brls::View*) {
        brls::Application::pushActivity(new brls::Activity(new SettingsControllersView()),
                                        brls::TransitionAnimation::NONE);
        return true;
    });
}

void SettingsControllerView::initButtonMappingCell() {
    buttonMappingCell->setText("akira/settings/button_mapping"_i18n);
    buttonMappingCell->setDetailText("akira/common/configure"_i18n);

    buttonMappingCell->registerClickAction([](brls::View*) {
        auto* remapView = new ControllerRemapView();
        brls::Application::pushActivity(new brls::Activity(remapView), brls::TransitionAnimation::NONE);
        return true;
    });
}
