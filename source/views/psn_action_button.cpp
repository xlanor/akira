#include "views/psn_action_button.hpp"

static const NVGcolor PSN_ACTION_DISABLED_BACKGROUND = nvgRGBA(72, 76, 84, 255);
static const NVGcolor PSN_ACTION_DISABLED_TEXT = nvgRGBA(176, 180, 188, 255);
static constexpr brls::Time PSN_ACTION_TICK_MS = 250;

void PsnActionButton::attach(brls::Button* button, NVGcolor readyColor, std::string readyLabel,
                             std::string busyLabel, std::string waitLabelKey, StatusProvider provider)
{
    this->button = button;
    this->readyColor = readyColor;
    this->readyLabel = std::move(readyLabel);
    this->busyLabel = std::move(busyLabel);
    this->waitLabelKey = std::move(waitLabelKey);
    this->provider = std::move(provider);

    timer.setCallback([this]() { apply(); });
    apply();
}

bool PsnActionButton::isReady() const
{
    return provider && provider().state == PsnActionState::Ready;
}

void PsnActionButton::start()
{
    apply();
    timer.start(PSN_ACTION_TICK_MS);
}

void PsnActionButton::stop()
{
    timer.stop();
}

void PsnActionButton::apply()
{
    if (!button || !provider)
        return;

    PsnActionStatus status = provider();

    if (applied && status.state == appliedState && status.secondsRemaining == appliedSeconds)
        return;

    applied = true;
    appliedState = status.state;
    appliedSeconds = status.secondsRemaining;

    switch (status.state)
    {
        case PsnActionState::Ready:
            button->setText(readyLabel);
            button->setState(brls::ButtonState::ENABLED);
            button->setTextColor(brls::Application::getTheme()["brls/button/primary_enabled_text"]);
            button->setBackgroundColor(readyColor);
            break;

        case PsnActionState::Busy:
            button->setText(busyLabel);
            button->setState(brls::ButtonState::DISABLED);
            button->setTextColor(PSN_ACTION_DISABLED_TEXT);
            button->setBackgroundColor(PSN_ACTION_DISABLED_BACKGROUND);
            break;

        case PsnActionState::CoolingDown:
            button->setText(brls::getStr(waitLabelKey, status.secondsRemaining));
            button->setState(brls::ButtonState::DISABLED);
            button->setTextColor(PSN_ACTION_DISABLED_TEXT);
            button->setBackgroundColor(PSN_ACTION_DISABLED_BACKGROUND);
            break;
    }
}
