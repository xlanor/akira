#include "views/psn_gated_box.hpp"

using namespace brls::literals;

std::unordered_set<PsnGatedBox*> PsnGatedBox::liveBoxes;
bool PsnGatedBox::observerInstalled = false;

PsnGatedBox::PsnGatedBox()
{
    this->setAxis(brls::Axis::COLUMN);

    placeholder = new brls::Label();
    placeholder->setFontSize(18);
    placeholder->setTextColor(nvgRGB(150, 150, 150));
    placeholder->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    placeholder->setVerticalAlign(brls::VerticalAlign::CENTER);
    placeholder->setGrow(1.0f);
    placeholder->setVisibility(brls::Visibility::GONE);
    this->addView(placeholder);

    liveBoxes.insert(this);

    if (!observerInstalled)
    {
        observerInstalled = true;
        psn::Auth::instance().setStateObserver([]() {
            brls::sync([]() { PsnGatedBox::evaluateAll(); });
        });
    }
}

PsnGatedBox::~PsnGatedBox()
{
    liveBoxes.erase(this);
}

brls::View* PsnGatedBox::create()
{
    return new PsnGatedBox();
}

void PsnGatedBox::evaluateAll()
{
    for (PsnGatedBox* box : liveBoxes)
        box->evaluate();
}

bool PsnGatedBox::evaluate()
{
    applyState(psn::Auth::instance().state());
    return gateOpen;
}

void PsnGatedBox::applyState(psn::SessionState state)
{
    if (applied && state == appliedState)
        return;

    applied = true;
    appliedState = state;

    bool open = state != psn::SessionState::NotLinked;
    gateOpen = open;

    for (brls::View* child : this->getChildren())
    {
        if (child == placeholder)
            continue;

        child->setVisibility(open ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }

    if (open)
    {
        placeholder->setVisibility(brls::Visibility::GONE);
        return;
    }

    placeholder->setText("akira/trophies/no_psn_token"_i18n);
    placeholder->setVisibility(brls::Visibility::VISIBLE);

    brls::Logger::info("PSN gate closed: no account linked");
}
