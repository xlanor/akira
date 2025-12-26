#include "views/stream_menu.hpp"

StreamMenu::StreamMenu()
{
    brls::Logger::info("StreamMenu: constructor");

    this->inflateFromXMLRes("xml/views/stream_menu.xml");
    brls::Logger::info("StreamMenu: XML inflated");

    this->registerAction(
        "", brls::ControllerButton::BUTTON_B,
        [this](brls::View*) {
            brls::Logger::info("StreamMenu: B pressed, calling dismiss callback");
            if (this->onDismiss)
                this->onDismiss();
            brls::Logger::info("StreamMenu: B dismiss callback done, popping activity");
            brls::Application::popActivity(brls::TransitionAnimation::NONE);
            return true;
        },
        false, false, brls::SOUND_BACK);

    brls::Logger::info("StreamMenu: setting up button actions");

    this->statsButton->registerClickAction([this](brls::View*) {
        brls::Logger::info("StreamMenu: stats button clicked, calling callback");
        bool newState = !this->statsEnabled;
        if (this->onStatsToggle)
            this->onStatsToggle(newState);
        brls::Logger::info("StreamMenu: stats callback done, popping activity (NONE)");
        brls::Application::popActivity(brls::TransitionAnimation::NONE);
        return true;
    });

    this->resetGyroButton->registerClickAction([this](brls::View*) {
        brls::Logger::info("StreamMenu: reset gyro button clicked");
        if (this->onGyroReset)
            this->onGyroReset();
        brls::Application::popActivity(brls::TransitionAnimation::NONE);
        return true;
    });

    this->disconnectButton->registerClickAction([this](brls::View*) {
        brls::Logger::info("StreamMenu: disconnect button clicked");
        if (this->onDisconnect)
            this->onDisconnect(false);  // false = don't sleep
        // Defer pops to next frame to avoid use-after-free (first pop destroys this menu)
        brls::sync([]() {
            brls::Application::popActivity(brls::TransitionAnimation::NONE);
            brls::Application::popActivity(brls::TransitionAnimation::NONE);
            // Token is already 0 after pops - don't unblock or it underflows!
        });
        return true;
    });

    this->sleepButton->registerClickAction([this](brls::View*) {
        brls::Logger::info("StreamMenu: sleep button clicked");
        if (this->onDisconnect)
            this->onDisconnect(true);  // true = sleep
        // Defer pops to next frame to avoid use-after-free (first pop destroys this menu)
        brls::sync([]() {
            brls::Application::popActivity(brls::TransitionAnimation::NONE);
            brls::Application::popActivity(brls::TransitionAnimation::NONE);
            // Token is already 0 after pops - don't unblock or it underflows!
        });
        return true;
    });

    this->setLastFocusedView(this->statsButton);

    brls::Logger::info("StreamMenu: constructor complete");
}

void StreamMenu::setOnStatsToggle(std::function<void(bool)> callback)
{
    this->onStatsToggle = callback;
}

void StreamMenu::setOnDisconnect(std::function<void(bool sleep)> callback)
{
    this->onDisconnect = callback;
}

void StreamMenu::setOnDismiss(std::function<void()> callback)
{
    this->onDismiss = callback;
}

void StreamMenu::setOnGyroReset(std::function<void()> callback)
{
    this->onGyroReset = callback;
}

void StreamMenu::setStatsEnabled(bool enabled)
{
    this->statsEnabled = enabled;
    if (this->statsButton)
    {
        this->statsButton->setText(enabled ? "Hide Stats" : "Show Stats");
    }
}
