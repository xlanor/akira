#ifndef AKIRA_INPUT_PAD_PATH_HPP
#define AKIRA_INPUT_PAD_PATH_HPP

#include <chiaki/controller.h>
#include <switch.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "input/pad_output_state.hpp"

class ExtendedInputManager;

namespace akira::input {

/*
 * One controller, one path.
 *
 * Akira used to answer "which pad is the user holding" separately in four
 * places, and they disagreed: buttons came from a merge of handheld and player
 * one, gyro checked handheld first and so read the Joy-Cons in the rail while
 * you held a Pro Controller, rumble addressed a JoyDual handle that does not
 * exist on a Pro Controller, and triggers came from whichever pad reported
 * last. A path answers it once, and everything reads from the answer.
 *
 * The pad is chosen before the stream starts and does not change under you.
 */
enum class PadPathKind {
    JoyCon,      /* handheld or dual wireless Joy-Cons */
    SwitchPro,   /* genuine Pro Controller */
    McPsNative,  /* PlayStation pad via MissionControl - read as a PlayStation pad */
    McGeneric,   /* anything else MissionControl mediates */
};

struct PadCapabilities {
    bool analog_triggers = false;
    bool touchpad        = false;
    bool gyro            = false;
    bool rumble          = false;

    /* Capable but unconfigured - a PlayStation pad whose trigger bytes have not
     * been learned yet. The picker says so rather than letting the user find out
     * mid-game that the triggers are digital. */

    /* Only true where a charge reading is real rather than
     * MissionControl's untouched default. */
    bool battery         = false;
};

class PadPath {
public:
    virtual ~PadPath() = default;

    virtual PadPathKind kind() const = 0;

    /* Shown in the picker. "Pro Controller", "DualSense", "Joy-Con (L/R)". */
    virtual const char* label() const = 0;

    /* The one pad this path reads. Never a merge of several. */
    virtual HidNpadIdType   npad()  const = 0;
    virtual HidNpadStyleTag style() const = 0;

    virtual PadCapabilities capabilities() const = 0;

    /*
     * Refresh for this frame. False means the pad's feed is gone - for a raw
     * path a stalled report stream, for a HOS path a disconnect.
     *
     * Mixing sources per signal would mix latency: the raw feed and HOS's pad
     * state are two pipelines with different delays, so a fast trigger pull
     * produces frames where l2_state and the ZL bit disagree about when. A path
     * commits to one source for the whole pad and degrades wholesale.
     */
    virtual bool poll() = 0;

    /*
     * Only a path that bypasses the shared mapping layer implements this.
     * Every HOS path expresses its buttons through heldButtons() and lets
     * InputManager's ButtonMapping loop translate them, which is the one place
     * that knows how a Switch button becomes a chiaki button.
     */
    virtual void readButtons(ChiakiControllerState* state) { (void)state; }

    /*
     * Raw stick position, not written straight into the state: a stick
     * direction bound into a button combo has to read as zero, and that is the
     * mapping layer's business rather than the pad's.
     */
    virtual HidAnalogStickState stickPos(int index) const = 0;

    virtual void readTriggers(ChiakiControllerState* state) = 0;

    virtual bool readGyro(HidSixAxisSensorState* out) = 0;

    /* Re-zero the sensor fusion on whatever handles this path actually owns. */
    virtual void resetMotion() = 0;

    /* False when the pad has no touchpad, so callers can fall through to the
     * Switch touchscreen without asking about the model. */
    virtual bool readTouchpad(ChiakiControllerState* state) { (void)state; return false; }

    /*
     * The caller's intent, in amplitudes of 0..1 and hertz. What a path does
     * with it depends on the hardware: a real Switch actuator plays a low and a
     * high band at once and the side is the handle index, so left/right does
     * not map onto the bands at all. MissionControl instead keys the physical
     * motor off the band - low band drives the left motor, high the right - and
     * reads no frequency whatsoever, because an ERM motor has no resonance to
     * drive, only a speed.
     */
    virtual void sendRumble(float left, float right, float freqLow, float freqHigh) = 0;

    /*
     * Whether this path drives the pad's own motors rather than asking HOS to.
     *
     * Callers upstream need to know because the shaping differs, not just the
     * destination: amplitudes tuned for a Joy-Con's HD actuator are the wrong
     * numbers for an ERM motor, and a path that reaches one should not be fed
     * the other's curve.
     */
    virtual bool nativeRumble() const { return false; }

    /*
     * Enough to find this pad's rumble profile.
     *
     * Switch-native is its own answer rather than a vid/pid of zero, because
     * zero is also what an unidentified third-party pad reports - and a Joy-Con
     * sharing a profile with whatever unknown controller was plugged in last is
     * exactly the confusion the reserved key exists to prevent.
     */
    virtual bool switchNative() const { return true; }
    virtual uint16_t vendorId() const { return 0; }
    virtual uint16_t productId() const { return 0; }

    /* The physical unit, when we know it. Null on a pad HOS drives itself. */
    virtual const uint8_t* address() const { return nullptr; }

    /*
     * One adaptive trigger's setting: an effect type and ten parameter bytes
     * whose meaning depends on it.
     *
     * Carried opaquely from the console to the pad. Nothing between the two
     * needs to know what a mode-2 effect with these parameters feels like, and
     * a layer that decoded it would have to be revised every time Sony added
     * one.
     */
    struct TriggerEffect {
        uint8_t type = 0;
        uint8_t params[10] = {};
    };

    /*
     * Resistance the console asked the triggers to apply.
     *
     * Ignored by default, and that default is nearly every path: no Switch
     * controller has anything to apply it with, and a pad reached through
     * MissionControl's translation has no route for it either - the Switch
     * vibration protocol this all travels over has no such concept to carry.
     */
    virtual void sendTriggerEffects(const TriggerEffect& left, const TriggerEffect& right)
    {
        (void)left;
        (void)right;
    }

    /*
     * How hard the console's own settings allow this pad to play.
     *
     * Wire values, passed through as the console sent them: Off is 0 and
     * Strong is 1, so the numbering is not a loudness order and a path that
     * treats it as one gets the setting backwards.
     *
     * Ignored by default for the same reason trigger effects are - a pad
     * reached through MissionControl's translation has nowhere to put it, and
     * a Switch pad has no such control at all.
     */
    virtual void sendEffectIntensity(uint8_t vibration, uint8_t trigger)
    {
        (void)vibration;
        (void)trigger;
    }

    /*
     * The colour the game asked the lightbar to show.
     *
     * Only a pad we write to ourselves has one, and only then if its profile
     * says akira may claim the LED at all.
     */
    virtual void sendLightbar(uint8_t red, uint8_t green, uint8_t blue)
    {
        (void)red;
        (void)green;
        (void)blue;
    }

    /*
     * Held HID buttons, for the ButtonMapping combo layer and synthetic swipes.
     * Zero on a path that does not use them.
     */
    virtual uint64_t heldButtons() const = 0;

    /*
     * Is the chord that opens Akira's own menu being held?
     *
     * Asked of the path rather than read off a merged pad, because the answer
     * differs by pad and the merged pad cannot say which one pressed. It also
     * means the chord follows the controller the user actually chose, instead
     * of only working on Handheld and player one.
     *
     * Minus by default: on a Switch pad that is the PS button, and holding it
     * has always been how this menu opens.
     */
    virtual bool menuHeld() const { return (heldButtons() & HidNpadButton_Minus) != 0; }

    /*
     * Akira's ButtonMapping binds PS5 buttons to Switch button *combos*, because
     * a Switch pad has no touchpad and no Create/Options. A PlayStation pad read
     * natively is missing none of them, so the layer has nothing left to solve
     * and would only re-introduce the ambiguity we read raw to avoid.
     */
    virtual bool usesButtonMapping() const { return true; }
};

/*
 * What a pad is, without building anything.
 *
 * Constructing a path acquires six-axis handles and starts the sensors, so a
 * picker that re-read the list every frame would churn them. Describing is
 * side-effect free; only the pad actually chosen gets built.
 */
struct PadDescription {
    HidNpadIdType   npad  = HidNpadIdType_No1;
    PadPathKind     kind  = PadPathKind::JoyCon;
    HidNpadStyleTag style = HidNpadStyleTag_NpadFullKey;
    const char*     label = "";
    PadCapabilities caps;

    /* Set for a MissionControl pad, so the chosen one can be rebuilt exactly. */
    uint16_t vendor_id  = 0;
    uint16_t product_id = 0;

    /* The physical unit, for the profile tier that names one pad rather than a
     * model. Absent on anything HOS drives itself. */
    uint8_t  bt_addr[6]{};
    bool     has_address = false;
};

/*
 * Who writes this pad's output reports.
 *
 * The precedence itself lives in pad_output_state.hpp, which is pure and free
 * of libnx so it can be tested on the host. These are the two shapes that need
 * a PadDescription, which is a Switch-side thing.
 */

/* The same question asked of a described pad, resolved through
 * ResolvePadOutput so there is one answer and not two to keep agreeing. */
PadDriverState ResolvePadDriverState(const PadDescription& desc,
                                     ExtendedInputManager& extended);

PadDriver ResolvePadDriver(const PadDescription& desc, ExtendedInputManager& extended);

std::vector<PadDescription> DescribePads(ExtendedInputManager& extended);

/* The same listing without a live session, for the settings screen. */
std::vector<PadDescription> DescribePads();

/* Build the one that was chosen. Null if it is no longer connected. */
std::unique_ptr<PadPath> MakePath(const PadDescription& desc, ExtendedInputManager& extended);

/*
 * What to use when the user has not picked - a merged handheld/player-one pad,
 * matching the behaviour before any of this existed. The fallback path is the
 * one that must never regress: someone with plain Joy-Cons and no sysmodule
 * should not be able to tell this refactor happened.
 */
std::unique_ptr<PadPath> DefaultPadPath(ExtendedInputManager& extended);

} // namespace akira::input

#endif // AKIRA_INPUT_PAD_PATH_HPP
