#include "views/players_panel_view.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "core/host.hpp"
#include "stream/input_manager.hpp"
#include "input/pad_path.hpp"

#include <chiaki/common.h>

using akira::input::PadDescription;

namespace {

constexpr int kSecondarySlots = CHIAKI_COUCH_MAX_PADS - 1;

NVGcolor SlotLed(int player)
{
    switch (player) {
        case 1:  return nvgRGB(0x4a, 0x9e, 0xff);
        case 2:  return nvgRGB(0xff, 0x5a, 0x5a);
        case 3:  return nvgRGB(0x5e, 0xe6, 0xa0);
        case 4:  return nvgRGB(0xf0, 0x72, 0xd0);
        default: return nvgRGB(0x66, 0x66, 0x66);
    }
}

brls::Box* MakeRowShell(bool empty)
{
    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setWidth(424);
    row->setHeight(70);
    row->setPaddingLeft(14);
    row->setPaddingRight(14);
    row->setCornerRadius(14);
    row->setMarginBottom(9);
    row->setBackgroundColor(nvgRGB(0x1f, 0x1f, 0x24));
    row->setBorderThickness(1.0f);
    row->setBorderColor(empty ? nvgRGB(0x3a, 0x3a, 0x44) : nvgRGB(0x31, 0x31, 0x39));
    return row;
}

brls::Box* MakeLed(int player, bool empty)
{
    auto* led = new brls::Box();
    led->setAxis(brls::Axis::COLUMN);
    led->setJustifyContent(brls::JustifyContent::CENTER);
    led->setAlignItems(brls::AlignItems::CENTER);
    led->setWidth(38);
    led->setHeight(38);
    led->setCornerRadius(11);
    led->setMarginRight(13);
    if (empty) {
        led->setBackgroundColor(nvgRGB(0x2a, 0x2a, 0x31));
        led->setBorderThickness(1.0f);
        led->setBorderColor(nvgRGB(0x3a, 0x3a, 0x44));
    } else {
        led->setBackgroundColor(SlotLed(player));
    }

    auto* n = new brls::Label();
    n->setText(std::to_string(player));
    n->setFontSize(17);
    n->setTextColor(empty ? nvgRGB(0x6c, 0x6c, 0x76) : nvgRGB(0x0b, 0x0d, 0x12));
    led->addView(n);
    return led;
}

brls::Box* MakeBody(const std::string& dev, const std::string& meta, bool dim, brls::Label** outMeta = nullptr)
{
    auto* body = new brls::Box();
    body->setAxis(brls::Axis::COLUMN);
    body->setJustifyContent(brls::JustifyContent::CENTER);
    body->setGrow(1.0f);

    auto* devLabel = new brls::Label();
    devLabel->setText(dev);
    devLabel->setFontSize(19);
    devLabel->setTextColor(dim ? nvgRGB(0x6c, 0x6c, 0x76) : nvgRGB(0xf3, 0xf3, 0xf6));
    body->addView(devLabel);

    auto* metaLabel = new brls::Label();
    metaLabel->setText(meta);
    metaLabel->setFontSize(14);
    metaLabel->setTextColor(nvgRGB(0x9a, 0x9a, 0xa4));
    metaLabel->setMarginTop(3);
    body->addView(metaLabel);

    if (outMeta)
        *outMeta = metaLabel;

    return body;
}

brls::Button* MakeAction(const std::string& text, NVGcolor color)
{
    auto* btn = new brls::Button();
    btn->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
    btn->setText(text);
    btn->setTextColor(color);
    btn->setHeight(44);
    btn->setCornerRadius(9);
    return btn;
}

NVGcolor MetaColor(CouchPadState state)
{
    switch (state) {
        case CouchPadState::Confirmed: return nvgRGB(0x8a, 0xd6, 0xa8);
        case CouchPadState::Announced: return nvgRGB(0xf2, 0xd0, 0x7a);
        case CouchPadState::NeedsProfile: return nvgRGB(0xff, 0xa8, 0xa8);
        case CouchPadState::Failed:    return nvgRGB(0xff, 0xa8, 0xa8);
        case CouchPadState::Passcode:  return nvgRGB(0xf2, 0xd0, 0x7a);
        default:                       return nvgRGB(0x9a, 0x9a, 0xa4);
    }
}

std::string FailureText(uint8_t status)
{
    switch (status) {
        case kCouchJoinTimedOut:
            return "No answer from the console";
        case kCouchPadDropped:
            return "The console released this pad";
        case 1:
            return "That account is not signed in on the console";
        default:
            return "Console refused the join (status " + std::to_string((int)status) + ")";
    }
}

std::string MetaText(int player, CouchPadState state, uint8_t failStatus)
{
    switch (state) {
        case CouchPadState::Confirmed:
            return "Player " + std::to_string(player) + " - ready";
        case CouchPadState::Announced:
            return "Pick a user for this pad on the TV";
        case CouchPadState::NeedsProfile:
            return "Account is not registered on this PS5 - choose another profile";
        case CouchPadState::Passcode:
            return "Waiting for the console login passcode";
        case CouchPadState::Failed:
            return FailureText(failStatus);
        case CouchPadState::Joining:
        default:
            return "Joining...";
    }
}

std::string WithAccount(const std::string& text, const Host* host, uint8_t slot)
{
    if (!host || !host->isPS5())
        return text;
    const std::string& account = host->couchAccountLabel(slot);
    if (account.empty())
        return text + " - no PSN account configured";
    return text + " - PSN: " + account;
}

} // namespace

PlayersPanelView::PlayersPanelView(Host* host, InputManager* input, bool claimImmediately)
    : m_host(host)
    , m_input(input)
{
    setAxis(brls::Axis::COLUMN);
    setJustifyContent(brls::JustifyContent::CENTER);
    setAlignItems(brls::AlignItems::CENTER);
    setGrow(1.0f);
    setWidthPercentage(100.0f);
    setHeightPercentage(100.0f);
    setBackgroundColor(nvgRGBA(6, 7, 10, 190));

    auto* panel = new brls::Box();
    panel->setAxis(brls::Axis::COLUMN);
    panel->setWidth(460);
    panel->setPaddingTop(18);
    panel->setPaddingBottom(16);
    panel->setPaddingLeft(18);
    panel->setPaddingRight(18);
    panel->setCornerRadius(16);
    panel->setBackgroundColor(nvgRGB(0x26, 0x26, 0x2b));
    panel->setBorderThickness(1.0f);
    panel->setBorderColor(nvgRGB(0x3a, 0x3a, 0x42));
    addView(panel);

    auto* header = new brls::Box();
    header->setAxis(brls::Axis::ROW);
    header->setAlignItems(brls::AlignItems::CENTER);
    header->setMarginBottom(2);
    panel->addView(header);

    auto* title = new brls::Label();
    title->setText("Players");
    title->setFontSize(24);
    title->setTextColor(nvgRGB(0xf3, 0xf3, 0xf6));
    title->setMarginRight(10);
    header->addView(title);

    m_count = new brls::Label();
    m_count->setFontSize(15);
    m_count->setTextColor(nvgRGB(0x16, 0xbf, 0xe0));
    header->addView(m_count);

    m_sub = new brls::Label();
    m_sub->setFontSize(14);
    m_sub->setTextColor(nvgRGB(0x9a, 0x9a, 0xa4));
    m_sub->setMarginBottom(14);
    panel->addView(m_sub);

    m_slots = new brls::Box();
    m_slots->setAxis(brls::Axis::COLUMN);
    panel->addView(m_slots);

    m_foot = new brls::Label();
    m_foot->setFontSize(13);
    m_foot->setTextColor(nvgRGB(0x6c, 0x6c, 0x76));
    m_foot->setMarginTop(6);
    panel->addView(m_foot);

    if (m_input) {
        m_input->setOnCouchJoined([this](HidNpadIdType npad, uint8_t slot) {
            onJoined(npad, slot);
        });
    }

    registerAction("Back", brls::ControllerButton::BUTTON_B,
        [this](brls::View*) {
            if (m_claiming) {
                stopClaim();
                rebuild(true);
                return true;
            }
            if (m_selectingProfile) {
                m_selectingProfile = false;
                m_profileRetry = false;
                m_profileRejected = false;
                m_profileSlot = 0;
                m_pendingProfileId = 0;
                rebuild(true);
                return true;
            }
            if (m_input)
                m_input->cancelCouchJoin();
            brls::Application::popActivity(brls::TransitionAnimation::NONE);
            return true;
        });

    rebuild(false);

    if (claimImmediately)
        startAddPlayer();
}

PlayersPanelView::~PlayersPanelView()
{
    if (m_input) {
        m_input->cancelCouchJoin();
        m_input->setOnCouchJoined(nullptr);
    }

    if (m_on_closed)
        m_on_closed();
}

int PlayersPanelView::rosterCount() const
{
    return m_input ? (int)m_input->couchRoster().size() : 0;
}

const char* PlayersPanelView::deviceLabelFor(HidNpadIdType npad) const
{
    if (!m_input)
        return "Controller";
    for (const auto& d : m_input->describePads()) {
        if (d.npad == npad)
            return d.label;
    }
    return "Controller";
}

void PlayersPanelView::populateRoster()
{
    const char* primaryLabel = "Player 1";
    if (m_input) {
        for (const auto& d : m_input->describePads()) {
            if (m_input->CouchPadIndexForController(d.npad) == 0) {
                primaryLabel = d.label;
                break;
            }
        }
    }

    auto* primary = MakeRowShell(false);
    primary->setBorderColor(nvgRGB(0x35, 0x5a, 0x8a));
    primary->addView(MakeLed(1, false));
    primary->addView(MakeBody(primaryLabel, WithAccount("Player 1 - ready", m_host, 0), false));
    m_slots->addView(primary);

    std::vector<std::pair<HidNpadIdType, uint8_t>> ordered;
    if (m_input) {
        for (const auto& entry : m_input->couchRoster())
            ordered.emplace_back(entry.first, entry.second);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    for (const auto& entry : ordered) {
        HidNpadIdType npad = entry.first;
        uint8_t slot = entry.second;
        const int player = slot + 1;
        const CouchPadState state = m_host ? m_host->couchPadState(slot) : CouchPadState::Idle;
        const uint8_t failStatus = m_host ? m_host->couchPadFailure(slot) : 0;

        auto* row = MakeRowShell(false);
        if (state == CouchPadState::Failed || state == CouchPadState::NeedsProfile)
            row->setBorderColor(nvgRGB(0x7a, 0x3a, 0x3a));
        row->addView(MakeLed(player, false));

        brls::Label* meta = nullptr;
        row->addView(MakeBody(deviceLabelFor(npad),
                              WithAccount(MetaText(player, state, failStatus), m_host, slot),
                              false, &meta));
        if (meta)
            meta->setTextColor(MetaColor(state));

        brls::View* action = nullptr;
        if (state == CouchPadState::NeedsProfile) {
            auto* choose = MakeAction("Choose profile", nvgRGB(0x16, 0xbf, 0xe0));
            choose->registerClickAction([this, slot](brls::View*) {
                startProfileSelection(slot, true);
                return true;
            });
            row->addView(choose);
            action = choose;
        } else if (state == CouchPadState::Failed) {
            auto* retry = MakeAction("Retry", nvgRGB(0x16, 0xbf, 0xe0));
            retry->registerClickAction([this, slot](brls::View*) {
                if (m_host)
                    m_host->couchRetryPlayer(slot);
                return true;
            });
            row->addView(retry);
            action = retry;
        }

        auto* leave = MakeAction("Leave", nvgRGB(0xff, 0xb4, 0xb4));
        leave->registerClickAction([this, npad](brls::View*) {
            leaveSlot(npad);
            return true;
        });
        row->addView(leave);
        if (!action)
            action = leave;

        if (!m_first)
            m_first = action;

        m_rows.push_back({ slot, row, meta, action });
        m_slots->addView(row);
    }

    uint8_t lowestOpen = CHIAKI_COUCH_MAX_PADS;
    for (uint8_t slot = 1; slot < CHIAKI_COUCH_MAX_PADS; slot++) {
        const bool used = std::any_of(ordered.begin(), ordered.end(),
            [slot](const auto& entry) { return entry.second == slot; });
        if (!used) {
            lowestOpen = slot;
            break;
        }
    }

    for (uint8_t slot = 1; slot < CHIAKI_COUCH_MAX_PADS; slot++) {
        const bool used = std::any_of(ordered.begin(), ordered.end(),
            [slot](const auto& entry) { return entry.second == slot; });
        if (used)
            continue;
        const int player = slot + 1;
        auto* row = MakeRowShell(true);
        row->addView(MakeLed(player, true));
        const bool hasProfiles = !m_host || !m_host->isPS5() || !m_host->couchProfileChoices(slot).empty();
        const std::string emptyMeta = hasProfiles
            ? (slot == lowestOpen ? "Invite a player to this slot" : "Waiting for the previous slot")
            : "No other Akira profile has a PSN account ID";
        row->addView(MakeBody("Open slot", emptyMeta, true));

        if (hasProfiles && slot == lowestOpen) {
            auto* add = MakeAction("Add player", nvgRGB(0x16, 0xbf, 0xe0));
            add->registerClickAction([this](brls::View*) {
                startAddPlayer();
                return true;
            });
            if (!m_first)
                m_first = add;
            if (!m_addAction)
                m_addAction = add;
            row->addView(add);
        }

        m_slots->addView(row);
    }
}

void PlayersPanelView::populateProfilePicker()
{
    const auto choices = m_host ? m_host->couchProfileChoices(m_profileSlot)
                                : std::vector<Host::CouchProfileChoice>{};
    if (choices.empty()) {
        auto* row = MakeRowShell(true);
        row->addView(MakeBody("No eligible profiles",
            "Add another PSN profile to Akira first", true));
        m_slots->addView(row);
        return;
    }

    for (const auto& choice : choices) {
        auto* row = MakeRowShell(false);
        row->addView(MakeBody(choice.label, "Akira profile", false));
        auto* use = MakeAction("Use", nvgRGB(0x16, 0xbf, 0xe0));
        use->registerClickAction([this, profileId = choice.profileId](brls::View*) {
            selectProfile(profileId);
            return true;
        });
        row->addView(use);
        if (!m_first)
            m_first = use;
        m_slots->addView(row);
    }
}

void PlayersPanelView::populatePicker()
{
    std::vector<PadDescription> candidates;
    if (m_input) {
        const auto pads = m_input->describePads();
        std::string roster;
        for (const auto& entry : m_input->couchRoster())
            roster += " " + std::to_string((int)entry.first) + "->" + std::to_string((int)entry.second);
        brls::Logger::info("Couch picker: {} pad(s) listed, bound npad {}, roster:{}",
                           pads.size(), (int)m_input->boundNpad(),
                           roster.empty() ? " (empty)" : roster);

        for (const auto& d : pads) {
            const bool claimable = m_input->isClaimableNpad(d.npad);
            const bool inRoster = m_input->couchRoster().find(d.npad) != m_input->couchRoster().end();
            brls::Logger::info("Couch picker: npad {} '{}' claimable={} inRoster={}",
                               (int)d.npad, d.label, claimable, inRoster);
            if (!claimable || inRoster)
                continue;
            candidates.push_back(d);
        }
    }

    if (candidates.empty()) {
        auto* row = MakeRowShell(true);
        row->addView(MakeBody("No controllers waiting", "Connect a pad, then hold ZL + ZR", true));
        m_slots->addView(row);
        return;
    }

    for (const auto& c : candidates) {
        auto* row = MakeRowShell(false);
        row->setBorderColor(nvgRGB(0x16, 0xbf, 0xe0));
        brls::Label* meta = nullptr;
        row->addView(MakeBody(c.label, "Hold ZL + ZR to claim", false, &meta));
        if (!m_claimMeta)
            m_claimMeta = meta;
        m_slots->addView(row);
    }
}

void PlayersPanelView::refreshStates()
{
    for (const auto& r : m_rows) {
        if (!r.meta || !m_host)
            continue;
        const CouchPadState state = m_host->couchPadState(r.slot);
        const std::string text = WithAccount(
            MetaText(r.slot + 1, state, m_host->couchPadFailure(r.slot)), m_host, r.slot);
        if (r.meta->getFullText() != text) {
            r.meta->setText(text);
            r.meta->setTextColor(MetaColor(state));
        }
    }

    if (m_claiming && m_claimMeta && m_input) {
        const float progress = m_input->couchClaimProgress();
        const std::string text = progress > 0.0f
            ? "Claiming... " + std::to_string((int)(progress * 100.0f)) + "%"
            : std::string("Hold ZL + ZR to claim");
        if (m_claimMeta->getFullText() != text)
            m_claimMeta->setText(text);
    }
}

void PlayersPanelView::rebuild(bool refocus)
{
    bool focusInside = false;
    for (brls::View* v = brls::Application::getCurrentFocus(); v; v = v->getParent()) {
        if (v == m_slots) {
            focusInside = true;
            break;
        }
    }

    if (focusInside) {
        m_focusSlot = -1;
        brls::View* focused = brls::Application::getCurrentFocus();
        for (const auto& r : m_rows) {
            for (brls::View* v = focused; v; v = v->getParent()) {
                if (v == r.row) {
                    m_focusSlot = (int)r.slot;
                    break;
                }
            }
            if (m_focusSlot >= 0)
                break;
        }
    }

    m_slots->clearViews();
    m_first = nullptr;
    m_claimMeta = nullptr;
    m_addAction = nullptr;
    m_rows.clear();

    const int total = 1 + rosterCount();
    m_count->setText(std::to_string(total) + " / " + std::to_string(CHIAKI_COUCH_MAX_PADS));

    if (m_selectingProfile) {
        m_sub->setText(m_profileRejected
            ? "That account is not registered on this PS5. Choose another Akira profile registered on the console."
            : "Choose the Akira profile for Player " + std::to_string(m_profileSlot + 1));
        m_foot->setText("The PS5 will verify whether the selected account is registered on the console.");
        populateProfilePicker();
    } else if (m_claiming) {
        m_sub->setText("Hold ZL + ZR on the controller you want to add");
        m_foot->setText("Press B to cancel. A connected pad does nothing until it claims a slot.");
        populatePicker();
    } else if (rosterCount() >= kSecondarySlots) {
        m_sub->setText("Four players seated - the session is at capacity.");
        m_foot->setText("");
        populateRoster();
    } else {
        m_sub->setText("Everyone shares this screen. Add a pad by acting on it.");
        m_foot->setText("Add player, then the new player holds ZL + ZR on their pad to claim.");
        populateRoster();
    }

    if (!refocus && !focusInside)
        return;

    brls::View* target = nullptr;
    if (m_focusAddNext) {
        m_focusAddNext = false;
        target = m_addAction;
    }
    if (!target && m_focusSlot >= 0) {
        for (const auto& r : m_rows) {
            if ((int)r.slot == m_focusSlot) {
                target = r.action;
                break;
            }
        }
    }
    if (!target)
        target = m_first;
    if (target)
        brls::Application::giveFocus(target);
}

void PlayersPanelView::startAddPlayer()
{
    if (!m_input || rosterCount() >= kSecondarySlots)
        return;
    if (!m_host || !m_host->isPS5()) {
        startClaim();
        return;
    }

    uint8_t slot = 1;
    for (; slot < CHIAKI_COUCH_MAX_PADS; slot++) {
        const bool used = std::any_of(m_input->couchRoster().begin(), m_input->couchRoster().end(),
            [slot](const auto& entry) { return entry.second == slot; });
        if (!used)
            break;
    }
    if (slot < CHIAKI_COUCH_MAX_PADS)
        startProfileSelection(slot, false);
}

void PlayersPanelView::startClaim()
{
    if (m_claiming || !m_input)
        return;
    if (rosterCount() >= kSecondarySlots)
        return;
    m_claiming = true;
    m_input->beginCouchJoin();
    rebuild(true);
}

void PlayersPanelView::stopClaim()
{
    m_claiming = false;
    if (m_input)
        m_input->cancelCouchJoin();
}

void PlayersPanelView::startProfileSelection(uint8_t slot, bool retry)
{
    if (!m_host || !m_host->isPS5() || slot == 0 || slot >= CHIAKI_COUCH_MAX_PADS)
        return;
    stopClaim();
    m_selectingProfile = true;
    m_profileRetry = retry;
    m_profileRejected = retry && m_host->couchPadState(slot) == CouchPadState::NeedsProfile;
    m_profileSlot = slot;
    m_pendingProfileId = 0;
    rebuild(true);
}

void PlayersPanelView::selectProfile(int64_t profileId)
{
    if (!m_selectingProfile || !m_host)
        return;

    if (m_profileRetry) {
        if (!m_host->couchSelectProfile(m_profileSlot, profileId))
            return;
        m_selectingProfile = false;
        m_profileRetry = false;
        m_profileRejected = false;
        m_host->couchRetryPlayer(m_profileSlot);
        rebuild(true);
        return;
    }

    m_pendingProfileId = profileId;
    m_selectingProfile = false;
    m_profileRejected = false;
    startClaim();
}

void PlayersPanelView::onJoined(HidNpadIdType npad, uint8_t slot)
{
    m_claiming = false;
    m_focusSlot = -1;
    m_focusAddNext = true;
    if (m_host && m_host->isPS5() && !m_host->couchSelectProfile(slot, m_pendingProfileId)) {
        if (m_input)
            m_input->couchLeave(npad);
        startProfileSelection(slot, false);
        return;
    }
    m_pendingProfileId = 0;
    if (m_host)
        m_host->couchAddPlayer(slot);
    rebuild(true);
}

void PlayersPanelView::leaveSlot(HidNpadIdType npad)
{
    if (!m_input)
        return;

    const uint8_t slot = m_input->CouchPadIndexForController(npad);

    if (slot != InputManager::kNoCouchSlot && slot != 0) {
        uint8_t addr[6];
        if (m_input->couchSlotOutputTarget(slot, addr, nullptr, nullptr))
            m_input->extendedInput().unregisterCouchOutput(addr);
    }

    m_input->couchLeave(npad);
    if (m_host && slot != InputManager::kNoCouchSlot && slot != 0)
        m_host->couchRemovePlayer(slot);

    if (m_host && rosterCount() == 0)
        m_host->setCouchMode(false);

    rebuild(true);
}

std::string PlayersPanelView::structureSignature() const
{
    std::string sig = m_selectingProfile ? "p" : (m_claiming ? "c" : "r");
    if (!m_input)
        return sig;

    std::vector<std::pair<HidNpadIdType, uint8_t>> ordered;
    for (const auto& entry : m_input->couchRoster())
        ordered.emplace_back(entry.first, entry.second);
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    for (const auto& entry : ordered) {
        sig += ":" + std::to_string((int)entry.first) + "-" + std::to_string(entry.second);
        if (m_host) {
            if (m_host->couchPadState(entry.second) == CouchPadState::Failed)
                sig += "F";
            else if (m_host->couchPadState(entry.second) == CouchPadState::NeedsProfile)
                sig += "N";
        }
    }

    if (m_claiming) {
        for (const auto& d : m_input->describePads()) {
            if (!m_input->isClaimableNpad(d.npad))
                continue;
            if (m_input->couchRoster().find(d.npad) != m_input->couchRoster().end())
                continue;
            sig += "/" + std::to_string((int)d.npad);
        }
    }
    return sig;
}

void PlayersPanelView::draw(NVGcontext* vg, float x, float y, float width, float height,
                            brls::Style style, brls::FrameContext* ctx)
{
    if (m_claiming && m_input)
        m_input->tickCouchClaim();

    if (m_host)
        m_host->couchTickJoinTimeouts();

    const std::string sig = structureSignature();
    if (sig != m_signature) {
        m_signature = sig;
        rebuild(false);
    }

    refreshStates();

    Box::draw(vg, x, y, width, height, style, ctx);
}

brls::View* PlayersPanelView::getDefaultFocus()
{
    return m_first ? m_first : this;
}
