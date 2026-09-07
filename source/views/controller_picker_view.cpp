#include "views/controller_picker_view.hpp"

#include <string>
#include <vector>

#include "views/battery_pips.hpp"
#include "input/pad_art.hpp"

#include "ui/theme.hpp"

#include <borealis/core/i18n.hpp>

using namespace brls::literals;
using akira::input::PadDescription;
using akira::input::PadPathKind;

namespace {

/*
 * Two pads are worth choosing between when they differ in what they can do.
 * Two Joy-Con pairs are interchangeable and asking about them is noise; a
 * DualSense next to Joy-Cons is a real choice, and so is a Pro Controller next
 * to handheld Joy-Cons - that pair is the case where guessing currently sends
 * gyro and rumble to the pad sitting in the rail.
 */
bool Differ(const PadDescription& a, const PadDescription& b)
{
    if (a.kind != b.kind)
        return true;
    if (a.caps.analog_triggers != b.caps.analog_triggers)
        return true;
    if (a.caps.touchpad != b.caps.touchpad)
        return true;
    return false;
}

/*
 * One per line, not joined with a separator. Two capabilities on one line read
 * as a sentence with a full stop in the middle of it, and the list only ever
 * has two or three entries - there is room.
 */
std::vector<std::string> CapabilityLines(const PadDescription& d)
{
    std::vector<std::string> out;

    if (d.caps.analog_triggers) out.emplace_back("analog triggers");
    if (d.caps.touchpad)        out.emplace_back("touchpad");
    if (out.empty())            out.emplace_back("digital triggers");

    return out;
}

std::string SlotLine(HidNpadIdType npad)
{
    if (npad == HidNpadIdType_Handheld)
        return "Handheld";
    return "Player " + std::to_string((int)npad + 1);
}

} // namespace

bool ControllerPickerView::worthAsking(const std::vector<PadDescription>& pads)
{
    if (pads.size() < 2)
        return false;

    for (std::size_t i = 1; i < pads.size(); i++) {
        if (Differ(pads[0], pads[i]))
            return true;
    }
    return false;
}

ControllerPickerView::ControllerPickerView(std::vector<PadDescription> pads,
                                          OnChosen onChosen, Describe describe)
    : m_pads(std::move(pads))
    , m_onChosen(std::move(onChosen))
    , m_describe(std::move(describe))
{
    auto theme = brls::Application::getTheme();

    setAxis(brls::Axis::COLUMN);
    setJustifyContent(brls::JustifyContent::CENTER);
    setAlignItems(brls::AlignItems::CENTER);
    setGrow(1.0f);
    /* Fill outright. Growing alone left the app's own background
     * visible at the edges, and white line art on a light bar is
     * invisible. */
    setWidthPercentage(100.0f);
    setHeightPercentage(100.0f);
    /*
     * Painted dark whatever the theme, because the controller art is white on
     * transparent and would vanish on a light ground. The console's own
     * controller screen is dark for the same reason, so this reads as
     * deliberate rather than as a theme that failed to apply.
     */
    setBackgroundColor(nvgRGB(14, 18, 26));

    /*
     * "Press [L] + [R] on the controller you'd like to use" - the glyphs
     * inline, because that is what your thumb is looking for, not the letters.
     * Each pad's own drawing sits on its card below, so there is no separate
     * row of shapes: it would say the same thing twice.
     */
    auto* prompt = new brls::Box();
    prompt->setAxis(brls::Axis::ROW);
    prompt->setJustifyContent(brls::JustifyContent::CENTER);
    prompt->setAlignItems(brls::AlignItems::CENTER);
    prompt->setMarginBottom(34);
    addView(prompt);

    auto* before = new brls::Label();
    before->setText("Press");
    before->setFontSize(22);
    before->setTextColor(nvgRGB(232, 238, 245));
    prompt->addView(before);

    auto* lBtn = new brls::Image();
    lBtn->setImageFromRes("img/buttons/l.png");
    lBtn->setWidth(62);
    lBtn->setHeight(46);
    lBtn->setMarginLeft(12);
    prompt->addView(lBtn);

    auto* plus = new brls::Label();
    plus->setText("+");
    plus->setFontSize(20);
    plus->setTextColor(nvgRGB(140, 155, 172));
    plus->setMarginLeft(6);
    plus->setMarginRight(6);
    prompt->addView(plus);

    auto* rBtn = new brls::Image();
    rBtn->setImageFromRes("img/buttons/r.png");
    rBtn->setWidth(62);
    rBtn->setHeight(46);
    rBtn->setMarginRight(12);
    prompt->addView(rBtn);

    auto* after = new brls::Label();
    after->setText("on the controller you'd like to use");
    after->setFontSize(22);
    after->setTextColor(nvgRGB(232, 238, 245));
    prompt->addView(after);

    m_row = new brls::Box();
    m_row->setAxis(brls::Axis::ROW);
    m_row->setJustifyContent(brls::JustifyContent::CENTER);
    m_row->setAlignItems(brls::AlignItems::CENTER);
    addView(m_row);

    buildCards();

    /*
     * One PadState per candidate, so a press can be attributed to the pad that
     * made it. The merged pad everything else reads from cannot answer "which
     * one" - that is the whole problem being solved here.
     */

    /*
     * The same hints every other screen shows, rather than one label with eight
     * spaces in the middle of it - so A and B get their glyphs, and so a button
     * that is not available right now simply is not listed.
     *
     * Given a bar of its own because this screen has no frame around it to put
     * a footer in, and centred hints on a black field read as an afterthought.
     * The glyphs are borealis's own at their normal size; what makes them
     * obvious is the ground they sit on.
     */
    auto* hintBar = new brls::Box();
    hintBar->setAxis(brls::Axis::ROW);
    hintBar->setJustifyContent(brls::JustifyContent::CENTER);
    hintBar->setAlignItems(brls::AlignItems::CENTER);
    hintBar->setPaddingLeft(26);
    hintBar->setPaddingRight(26);
    hintBar->setPaddingTop(6);
    hintBar->setPaddingBottom(6);
    hintBar->setCornerRadius(12);
    hintBar->setBackgroundColor(nvgRGBA(255, 255, 255, 20));
    hintBar->setBorderColor(nvgRGBA(255, 255, 255, 38));
    hintBar->setBorderThickness(1.5f);
    hintBar->setMarginTop(34);

    /*
     * Larger than the 25.5 and 21.5 the hint XML sets, because on this screen
     * the buttons are the instruction rather than a reminder of one. Set on the
     * row, not the hints, so it survives the rebuild that happens every time A
     * becomes available.
     */
    auto* hints = new brls::Hints();
    hints->setHintFontSizes(34.0f, 28.0f);
    hintBar->addView(hints);
    addView(hintBar);

    /*
     * The way out, and deliberately on borealis input rather than the raw
     * per-pad polling above. If that polling is wrong for some pad, the screen
     * still has to be leaveable - a modal you cannot dismiss, in front of a
     * stream that has not started, means force-quitting the app.
     */
    setFocusable(true);

    registerAction("Continue", brls::ControllerButton::BUTTON_A,
        [this](brls::View*) {
            accept();
            return true;
        });

    /*
     * Hidden until one is chosen, because until then A meant "carry on with
     * whatever the default resolved to" - a second, invisible action wearing
     * the same button as "use this one". The hints row reads availability, so
     * this takes the prompt away as well as the press.
     */
    setActionAvailable(brls::ControllerButton::BUTTON_A, false);

    registerAction("Back", brls::ControllerButton::BUTTON_B,
        [this](brls::View*) {
            cancel();
            return true;
        });
}

void ControllerPickerView::accept()
{
    if (m_done)
        return;

    if (m_selected >= 0 && m_selected < (int)m_pads.size()) {
        brls::Logger::info("ControllerPicker: chose {} on npad {}",
            m_pads[m_selected].label, (int)m_pads[m_selected].npad);
        choose((std::size_t)m_selected);
        return;
    }

    brls::Logger::info("ControllerPicker: continuing on the default pad");
    m_done = true;
    if (m_onChosen)
        m_onChosen(kNoChoice);
}

void ControllerPickerView::cancel()
{
    if (m_done)
        return;

    brls::Logger::info("ControllerPicker: backed out");
    m_done = true;
    if (m_onChosen)
        m_onChosen(kCancelled);
}

void ControllerPickerView::buildCards()
{
    auto theme = brls::Application::getTheme();
    (void)theme;

    m_states.resize(m_pads.size());
    m_seen_clear.assign(m_pads.size(), false);

    /*
     * Fit the row to the canvas rather than assuming it fits.
     *
     * DescribePads can offer five candidates today - handheld plus four
     * players - and at a fixed 250 wide that is 1390px against a 1280 canvas,
     * so the fifth card would sit off the edge with no way to reach it. Sizing
     * from the count keeps every candidate reachable, and keeps doing so if
     * multi-controller support raises the ceiling.
     */
    constexpr float kCanvas   = 1280.0f;
    constexpr float kGutter   = 28.0f;   /* 14 either side */
    constexpr float kMaxCard  = 250.0f;
    constexpr float kMinCard  = 156.0f;

    const float count    = (float)(m_pads.size() > 0 ? m_pads.size() : 1);
    const float budget   = (kCanvas - 60.0f) / count - kGutter;
    const float cardW    = budget > kMaxCard ? kMaxCard : (budget < kMinCard ? kMinCard : budget);
    const float scale    = cardW / kMaxCard;

    /*
     * Drawn at its native 128, never resampled. The source is one-pixel
     * line work, and scaling it to an arbitrary height broke the strokes
     * up - it read as pixellated because it was being resampled, not
     * because the art is low resolution.
     */
    const float artW     = 128.0f;
    const float artH     = 128.0f;

    for (std::size_t i = 0; i < m_pads.size(); i++) {
        padInitialize(&m_states[i], m_pads[i].npad);
        padUpdate(&m_states[i]);

        auto* card = new brls::Box();
        card->setAxis(brls::Axis::COLUMN);
        card->setAlignItems(brls::AlignItems::CENTER);
        card->setJustifyContent(brls::JustifyContent::CENTER);
        card->setWidth(cardW);
        card->setHeight(258);
        card->setMarginLeft(14);
        card->setMarginRight(14);
        card->setBackgroundColor(nvgRGBA(255, 255, 255, 16));
        card->setCornerRadius(14);
        card->setBorderColor(nvgRGBA(255, 255, 255, 30));
        card->setBorderThickness(1.5f);

        auto* art = new brls::Image();
        art->setImageFromRes(akira::input::PadArtPath(m_pads[i]));
        art->setScalingType(brls::ImageScalingType::FIT);
        art->setWidth(artW);
        art->setHeight(artH);
        art->setMarginBottom(14);
        card->addView(art);

        auto* name = new brls::Label();
        name->setText(m_pads[i].label);
        name->setFontSize(scale < 0.8f ? 19 : 23);
        name->setTextColor(nvgRGB(232, 238, 245));
        name->setMarginBottom(4);
        card->addView(name);

        auto* slot = new brls::Label();
        slot->setText(SlotLine(m_pads[i].npad));
        slot->setFontSize(17);
        slot->setTextColor(nvgRGB(140, 155, 172));
        slot->setMarginBottom(8);
        card->addView(slot);

        const auto capLines = CapabilityLines(m_pads[i]);
        for (std::size_t c = 0; c < capLines.size(); c++) {
            auto* caps = new brls::Label();
            caps->setText(capLines[c]);
            caps->setFontSize(scale < 0.8f ? 14 : 16);
            caps->setTextColor(nvgRGB(140, 155, 172));
            caps->setMarginBottom(c + 1 == capLines.size() ? 10 : 2);
            card->addView(caps);
        }

        /* A Joy-Con pair has two batteries and they drain independently, so
         * showing one number for both would hide the half that is about to
         * die. Everything else has one. */
        if (m_pads[i].caps.battery) {
            const bool split = akira::input::PadHasTwoBatteries(m_pads[i]);
            card->addView(new BatteryPips(m_pads[i].npad, split, split ? 78 : 44, 18));
        }

        m_cards.push_back(card);
        m_row->addView(card);
    }
}

/*
 * Two enumerations describe the same set when the same npads carry the same
 * kinds. Capability changes - a pad finishing training, say - deliberately do
 * not force a rebuild: the row would flicker under the user's thumb for a
 * caption change nobody is reading.
 */
bool ControllerPickerView::SameSet(const std::vector<PadDescription>& a,
                                   const std::vector<PadDescription>& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); i++) {
        if (a[i].npad != b[i].npad || a[i].kind != b[i].kind)
            return false;
    }
    return true;
}

void ControllerPickerView::rebuild(std::vector<PadDescription> fresh)
{
    /*
     * Remember what was selected as a controller rather than as a row index.
     * Joy-Cons are why: attaching them to the console moves the same two pads
     * from a player slot to Handheld and changes their kind on the way, so an
     * index - or an npad - would drop a selection the user never changed.
     */
    bool         hadSelection = m_selected >= 0 && m_selected < (int)m_pads.size();
    PadPathKind  selKind      = hadSelection ? m_pads[m_selected].kind : PadPathKind::JoyCon;
    HidNpadIdType selNpad     = hadSelection ? m_pads[m_selected].npad : HidNpadIdType_No1;
    uint16_t     selVid       = hadSelection ? m_pads[m_selected].vendor_id  : 0;
    uint16_t     selPid       = hadSelection ? m_pads[m_selected].product_id : 0;

    m_pads = std::move(fresh);
    m_cards.clear();
    m_row->clearViews();
    buildCards();

    /* Single exit, because A's availability has to follow the selection and
     * there is more than one way for the carry-over to give up. */
    m_selected = carryOverSelection(hadSelection, selNpad, selKind, selVid, selPid);
    setActionAvailable(brls::ControllerButton::BUTTON_A, m_selected >= 0);
}

int ControllerPickerView::carryOverSelection(bool hadSelection, HidNpadIdType selNpad,
                                             PadPathKind selKind, uint16_t selVid,
                                             uint16_t selPid) const
{
    if (!hadSelection)
        return -1;

    /* Exact match first: same slot, same kind. */
    for (std::size_t i = 0; i < m_pads.size(); i++) {
        if (m_pads[i].npad == selNpad && m_pads[i].kind == selKind)
            return (int)i;
    }

    /* A MissionControl pad keeps its identity even if HOS moves it. */
    if (selVid != 0) {
        for (std::size_t i = 0; i < m_pads.size(); i++) {
            if (m_pads[i].vendor_id == selVid && m_pads[i].product_id == selPid)
                return (int)i;
        }
    }

    /*
     * Joy-Cons carry no identity we can see, so a detach reads as one pad
     * leaving and another arriving. If exactly one Joy-Con entry existed before
     * and exactly one exists now, it is the same pair in the same hands.
     */
    if (selKind == PadPathKind::JoyCon) {
        int count = 0, only = -1;
        for (std::size_t i = 0; i < m_pads.size(); i++) {
            if (m_pads[i].kind == PadPathKind::JoyCon) { count++; only = (int)i; }
        }
        if (count == 1)
            return only;
    }

    return -1;
}

void ControllerPickerView::choose(std::size_t index)
{
    if (m_done || index >= m_pads.size())
        return;

    m_done = true;
    if (m_onChosen)
        m_onChosen(m_pads[index].npad);
}

void ControllerPickerView::draw(NVGcontext* vg, float x, float y, float width, float height,
                                brls::Style style, brls::FrameContext* ctx)
{
    Box::draw(vg, x, y, width, height, style, ctx);

    /*
     * Ring the chosen card. Drawn here rather than by restyling the card so
     * the glow can sit outside its bounds without disturbing the row's layout.
     */
    if (m_selected >= 0 && m_selected < (int)m_cards.size()) {
        brls::Box* card = m_cards[m_selected];
        const float cx = card->getX(), cy = card->getY();
        const float cw = card->getWidth(), ch = card->getHeight();

        for (int pass = 3; pass >= 0; pass--) {
            const float grow  = 3.0f + pass * 4.0f;
            const int   alpha = pass == 0 ? 255 : 46 - pass * 10;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cx - grow, cy - grow, cw + grow * 2, ch + grow * 2, 14.0f + grow);
            nvgStrokeColor(vg, nvgRGBA(64, 208, 122, (unsigned char)alpha));
            nvgStrokeWidth(vg, pass == 0 ? 3.0f : 6.0f);
            nvgStroke(vg);
        }
    }

    if (m_done)
        return;

    /*
     * Polled here rather than through borealis input, because borealis reports
     * a merged pad and the question is precisely which physical one pressed.
     */
    m_frames++;

    /*
     * Re-enumerate twice a second. The picker exists so you can decide which
     * pad to use, and "let me go and switch the other one on" is a normal
     * response to seeing it - a snapshot would never show that pad, and would
     * happily let you select one that had since been turned off.
     */
    if (m_describe && (m_frames % 30) == 0) {
        auto fresh = m_describe();
        if (!SameSet(fresh, m_pads)) {
            brls::Logger::info("ControllerPicker: pads changed, {} -> {}",
                               m_pads.size(), fresh.size());
            rebuild(std::move(fresh));
            return;
        }
    }

    /* Last-resort backstop: about thirty seconds at 60fps. If no pad
     * here can be read at all, the screen lets go by itself rather
     * than holding the session hostage. */
    if (m_frames > 1800) {
        brls::Logger::warning("ControllerPicker: no input in ~30s, continuing");
        accept();
        return;
    }

    for (std::size_t i = 0; i < m_states.size(); i++) {
        padUpdate(&m_states[i]);

        const u64 held = padGetButtons(&m_states[i]);

        /*
         * Once every couple of seconds, say what each pad is actually
         * reporting. A picker that does not respond is otherwise
         * indistinguishable between "HOS reports nothing for this npad",
         * "it reports buttons but never ZL" and "ZL arrives and the edge is
         * missed" - and only the third is about this loop.
         */
        if ((m_frames % 120) == 1) {
            brls::Logger::info("ControllerPicker: npad {} connected={} held=0x{:x}",
                               (int)m_pads[i].npad, padIsConnected(&m_states[i]) ? 1 : 0,
                               (unsigned long long)held);
        }

        /*
         * Edge-detected by hand rather than through padGetButtonsDown, which
         * compares against the previous update of this same PadState - and
         * this state is updated from draw(), so anything else that polls
         * between frames eats the edge. Requiring a clear frame first also
         * stops a pad that is already holding ZL from choosing itself the
         * instant the picker opens.
         */
        /*
         * L + R together, which is the console's own "this controller"
         * gesture and exists on every shape this screen can list: both halves
         * of a handheld or dual pair, a Pro Controller, and anything
         * MissionControl presents as one. A single sideways Joy-Con has SL/SR
         * instead, but DescribePads does not offer those as candidates.
         */
        constexpr u64 kShoulder = HidNpadButton_L  | HidNpadButton_R;
        constexpr u64 kTrigger   = HidNpadButton_ZL | HidNpadButton_ZR;

        const bool picking = ((held & kShoulder) == kShoulder) ||
                             ((held & kTrigger)  == kTrigger);

        if (!picking) {
            m_seen_clear[i] = true;
            continue;
        }

        if (!m_seen_clear[i])
            continue;

        if (m_selected != (int)i) {
            m_selected = (int)i;
            setActionAvailable(brls::ControllerButton::BUTTON_A, true);
            brls::Logger::info("ControllerPicker: selected {} on npad {}",
                m_pads[i].label, (int)m_pads[i].npad);
        }
        return;
    }
}
