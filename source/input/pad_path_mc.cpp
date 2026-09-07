#include "input/pad_path_mc.hpp"

#include <algorithm>
#include <cstring>

#include "input/extended_input_manager.hpp"
#include "input/ps_output.hpp"
#include "core/settings_manager.hpp"

#include <borealis.hpp>

namespace akira::input {

namespace {

struct ButtonMap {
    uint32_t ps;
    uint32_t chiaki;
};

/*
 * One to one, because a PlayStation pad has every button the PS5 expects.
 * Note Create maps to SHARE - the button was renamed, the protocol was not.
 */
constexpr ButtonMap kButtons[] = {
    { PsButton_Cross,    CHIAKI_CONTROLLER_BUTTON_CROSS      },
    { PsButton_Circle,   CHIAKI_CONTROLLER_BUTTON_MOON       },
    { PsButton_Square,   CHIAKI_CONTROLLER_BUTTON_BOX        },
    { PsButton_Triangle, CHIAKI_CONTROLLER_BUTTON_PYRAMID    },
    { PsButton_Left,     CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT  },
    { PsButton_Right,    CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT },
    { PsButton_Up,       CHIAKI_CONTROLLER_BUTTON_DPAD_UP    },
    { PsButton_Down,     CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN  },
    { PsButton_L1,       CHIAKI_CONTROLLER_BUTTON_L1         },
    { PsButton_R1,       CHIAKI_CONTROLLER_BUTTON_R1         },
    { PsButton_L3,       CHIAKI_CONTROLLER_BUTTON_L3         },
    { PsButton_R3,       CHIAKI_CONTROLLER_BUTTON_R3         },
    { PsButton_Options,  CHIAKI_CONTROLLER_BUTTON_OPTIONS    },
    { PsButton_Create,   CHIAKI_CONTROLLER_BUTTON_SHARE      },
    { PsButton_Touchpad, CHIAKI_CONTROLLER_BUTTON_TOUCHPAD   },
    { PsButton_Ps,       CHIAKI_CONTROLLER_BUTTON_PS         },
};

} // namespace

McGenericPath::McGenericPath(HidNpadIdType npad, ExtendedInputManager& extended,
                             uint16_t vendor_id, uint16_t product_id)
    : HosPadPath(npad, HidNpadStyleTag_NpadFullKey)
    , m_extended(extended)
    , m_vendor_id(vendor_id)
    , m_product_id(product_id)
{
    /*
     * Rebuild this pad's vibration handles before using them.
     *
     * They encode the controller style, and one built for the wrong style
     * addresses a device that does not exist - HOS then drops the vibration
     * with no error, which is indistinguishable from sending nothing. borealis
     * normally refreshes them from its controller-state loop, but akira blocks
     * input while streaming, so that loop is not running and the handles are
     * whatever they were when it stopped.
     *
     * That never mattered while this path was only ever built before a stream.
     * It does now: handing a DualSense back to MissionControl mid-stream builds
     * one here, and the result was akira sending real amplitudes into handles
     * nothing was listening on - MissionControl writing to the pad, the
     * lightbar proving it, and no rumble.
     */
    if (auto* input = brls::Application::getPlatform()->getInputManager())
        input->refreshRumbleHandles((unsigned int)npad);
}

PadCapabilities McGenericPath::capabilities() const
{
    PadCapabilities caps;
    caps.gyro            = true;
    caps.rumble          = true;
    /* A pad the backend cannot name gives us no pressure, and there is no
     * longer a training run that could change that. */
    caps.analog_triggers = false;
    return caps;
}

bool McGenericPath::poll()
{
    return HosPadPath::poll();
}

void McGenericPath::sendRumble(float left, float right, float freqLow, float freqHigh)
{
    /*
     * No Joy-Con ceiling: that cap exists because a Joy-Con actuator at full
     * amplitude is harsh, which is not true of an ERM motor - capping it there
     * only made the pad feel weaker than the console asked for.
     *
     * Frequency is passed along unchanged even though MissionControl throws it
     * away, because this same call reaches a genuine Switch pad when the
     * backend is not mediating one, and it matters there.
     */
    const float lo = std::clamp(left,  0.0f, 1.0f);
    const float hi = std::clamp(right, 0.0f, 1.0f);

    /* Once a second, and only while asking for something audible: this is the
     * last point inside akira before HOS, so a value here with nothing felt
     * puts the fault on the far side of the vibration API. */
    if (lo > 0.0f || hi > 0.0f) {
        const uint32_t now =
            (uint32_t)(armTicksToNs(armGetSystemTick()) / 1000000ull);
        if (now >= m_rumble_log_next) {
            m_rumble_log_next = now + 1000;
            brls::Logger::info("generic rumble: npad {} lo={:.3f} hi={:.3f}",
                               (int)m_npad, lo, hi);
        }
    }

    auto* inputMgr = brls::Application::getPlatform()->getInputManager();
    inputMgr->sendRumbleToNpad((unsigned int)m_npad, freqLow, freqHigh, lo, hi);
}

void McGenericPath::readTriggers(ChiakiControllerState* state)
{
    /* Silence is not zero pressure - it means the backend has nothing to say
     * about this pad right now. Leaving the digital value the mapping layer
     * already computed is the safe reading; overwriting it with zero would
     * release a trigger the user is still holding. */
    if (!m_extended.hasFreshAnalogState())
        return;

    state->l2_state = m_extended.l2();
    state->r2_state = m_extended.r2();
}

McPsNativePath::McPsNativePath(HidNpadIdType npad, ExtendedInputManager& extended,
                               const PsModel& model)
    : McGenericPath(npad, extended, model.vendor_id, model.product_id)
    , m_model(&model)
{
    m_label = model.name;
    m_extended.setRawWanted(true);

    /*
     * Short press only. A long PS hold puts the DualSense into Bluetooth
     * pairing from its own firmware, before any host sees it, so blocking the
     * long press buys nothing and only risks leaving a block behind.
     */
    m_home_blocked = R_SUCCEEDED(appletBeginBlockingHomeButton(0));

    m_direct_output = PadTakesDirectOutput(model.vendor_id, model.product_id);
    m_extended.setDirectOutput(m_direct_output);

    brls::Logger::info("McPsNativePath: home blocking short={} direct rumble={}",
                       m_home_blocked, m_direct_output);
}

McPsNativePath::~McPsNativePath()
{
    /* Unblocked on the way out, and only what we actually took. Leaving HOME
     * blocked after the stream would strand the user on a console whose home
     * button had stopped working. */
    if (m_home_blocked)
        appletEndBlockingHomeButton();

    /* Zero first: the manager owes the pad one more frame after this, and what
     * it sends is whatever was last asked for. */
    if (m_direct_output) {
        m_extended.setDirectRumble(0, 0);
        m_extended.setDirectOutput(false);
    }
}

PadCapabilities McPsNativePath::capabilities() const
{
    PadCapabilities caps = McGenericPath::capabilities();
    caps.touchpad = m_model->has_touchpad;

    /* A native path reads pressure out of the pad's own report, at fixed
     * offsets we know, so this is unconditional. */
    caps.analog_triggers = true;
    return caps;
}

bool McPsNativePath::poll()
{
    const bool connected = McGenericPath::poll();

    AkiraInputRawReport raw{};
    m_report_valid = false;

    if (m_extended.readRawReport(&raw)) {
        /* The feed serves whichever pad the backend is tracking, so a report
         * from a different model is not ours to parse - the offsets would be
         * someone else's. */
        if (raw.vendor_id == m_vendor_id && raw.product_id == m_product_id) {
            m_report_valid = ParsePsReport(*m_model, raw.data, raw.length, &m_report);

            std::memcpy(m_address, raw.bt_addr, sizeof(m_address));
            m_have_address = true;
        }
    }

    return connected;
}

void McPsNativePath::readButtons(ChiakiControllerState* state)
{
    /*
     * Nothing on a stale frame. The alternative - falling back to the HOS
     * buttons for this frame - would mix two pipelines with different delays
     * and produce frames where a button and its trigger disagree about when.
     * Degrading wholesale is the caller's decision to make, not this one's.
     */
    if (!m_report_valid)
        return;

    /*
     * Combos and remapping are gone here - the pad already has every button the
     * console expects, so there is nothing to synthesise. Enable and disable
     * still applies: it is keyed on the chiaki button rather than on any Switch
     * one, so it says which buttons should reach the console at all, and
     * someone who turned off PS so they stop opening the console menu means
     * that whichever pad they picked.
     */
    auto* settings = SettingsManager::getInstance();

    for (const ButtonMap& m : kButtons) {
        if (!(m_report.buttons & m.ps))
            continue;
        if (!settings->isButtonEnabled(m.chiaki))
            continue;
        state->buttons |= m.chiaki;
    }
}

void McPsNativePath::sendRumble(float left, float right, float freqLow, float freqHigh)
{
    /*
     * The gate has to send us back to the old path, not just stop us.
     *
     * Choosing the direct branch and then having the write suppressed
     * downstream left the amplitude sitting in a variable with nothing to
     * carry it - no direct frame, and no HOS rumble either, because taking
     * this branch is what skips it. Silence, and nothing in any log to say
     * why.
     */
    const bool direct = m_direct_output && ExtendedInputManager::directStreamAllowed();

    /*
     * Said once per session, on the first rumble that is actually asking for
     * something. Which of the two paths produced a given buzz was otherwise
     * invisible, so "rumble works" and "direct rumble works" were impossible to
     * tell apart from the sofa - and they are completely different claims.
     */
    if (!m_rumble_source_logged && (left > 0.0f || right > 0.0f)) {
        m_rumble_source_logged = true;
        brls::Logger::info("rumble source: {}",
                           direct ? "DIRECT - written to the pad by us"
                                  : (m_direct_output
                                        ? "MissionControl - direct path present but the stream toggle is off"
                                        : "MissionControl - this pad is not driven directly"));
    }

    if (!direct) {
        McGenericPath::sendRumble(left, right, freqLow, freqHigh);
        return;
    }

    /*
     * Full scale, and no frequency. The pad's own report takes two amplitudes
     * for two ERM motors and has nowhere to put a frequency - which is not a
     * loss, because the route this replaces discarded it too, one layer further
     * down and without saying so.
     */
    const float lo = std::clamp(left,  0.0f, 1.0f);
    const float hi = std::clamp(right, 0.0f, 1.0f);

    m_extended.setDirectRumble((uint8_t)(lo * 255.0f + 0.5f),
                               (uint8_t)(hi * 255.0f + 0.5f));
}

void McPsNativePath::sendTriggerEffects(const TriggerEffect& left, const TriggerEffect& right)
{
    if (!m_direct_output)
        return;

    m_extended.setDirectTriggerEffects(left.type, left.params, right.type, right.params);
}

void McPsNativePath::sendEffectIntensity(uint8_t vibration, uint8_t trigger)
{
    if (!m_direct_output)
        return;

    m_extended.setConsoleIntensity(akira::input::Ds5IntensityFromWire(vibration),
                                   akira::input::Ds5IntensityFromWire(trigger));
}

void McPsNativePath::sendLightbar(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!m_direct_output)
        return;

    m_extended.setGameLightbar(red, green, blue);
}

bool McPsNativePath::menuHeld() const
{
    /* Stale raw feed: fall back to what HOS can still see, so the menu does
     * not become unreachable at the moment the pad starts misbehaving - which
     * is exactly when someone wants it. */
    if (!m_report_valid)
        return McGenericPath::menuHeld();

    return (m_report.buttons & PsButton_Options) != 0;
}

void McPsNativePath::readTriggers(ChiakiControllerState* state)
{
    if (!m_report_valid) {
        /* The report is the better source, but the backend's own reading is
         * scaled against what training actually observed, so it is worth
         * having when the raw feed skips a frame. */
        McGenericPath::readTriggers(state);
        return;
    }

    state->l2_state = m_report.l2;
    state->r2_state = m_report.r2;
}

void McPsNativePath::releaseSlot(ChiakiControllerState* state, TouchSlot& slot)
{
    if (!slot.active)
        return;

    if (slot.chiaki_id >= 0)
        chiaki_controller_state_stop_touch(state, (uint8_t)slot.chiaki_id);

    slot.active    = false;
    slot.chiaki_id = -1;
}

bool McPsNativePath::readTouchpad(ChiakiControllerState* state)
{
    if (!m_model->has_touchpad)
        return false;

    /*
     * Hold the existing contacts rather than lifting them when there is nothing
     * new - a dropped frame, or a DualShock 4 packet carrying no live touch
     * samples. A contact that flickers off and on reads as a tap to the
     * console, which is a worse lie than a slightly stale position.
     */
    if (!m_report_valid || !m_report.touch_fresh)
        return true;

    for (int i = 0; i < 2; i++) {
        const PsTouchPoint& point = m_report.touch[i];
        TouchSlot& slot = m_slots[i];

        if (!point.down) {
            releaseSlot(state, slot);
            continue;
        }

        /* A new tracking id in the same slot is a new contact, even without a
         * frame of separation - lifting and landing between polls is ordinary
         * at this report rate. */
        if (slot.active && slot.pad_id != point.id)
            releaseSlot(state, slot);

        if (!slot.active) {
            const int8_t id = chiaki_controller_state_start_touch(state, point.x, point.y);
            if (id < 0)
                continue;

            slot.active    = true;
            slot.pad_id    = point.id;
            slot.chiaki_id = id;
            continue;
        }

        /* The surface is the same 1920 wide as the PS5's, so x needs no
         * scaling; height differs by model, which is why it is carried as
         * data. A DualShock 4 is 942 where a DualSense is 1080. */
        chiaki_controller_state_set_touch_pos(state, (uint8_t)slot.chiaki_id, point.x, point.y);
    }

    return true;
}

} // namespace akira::input
