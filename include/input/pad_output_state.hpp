#ifndef AKIRA_INPUT_PAD_OUTPUT_STATE_HPP
#define AKIRA_INPUT_PAD_OUTPUT_STATE_HPP

#include <cstdint>

namespace akira::input {

/*
 * What akira is allowed to do with a pad right now, decided once.
 *
 * This used to be seven pieces of state recombined by whoever needed an answer:
 * two gate atomics, a refusal, a wanted flag, an address validity flag, a
 * landing flag and a claim. directHapticsReady took some, pumpDirectOutput took
 * others, ResolvePadDriver took a third set, and the settings page wrote one of
 * them directly. Every one of them was right on its own and they disagreed with
 * each other, which is what the last several faults have been - a pad neither
 * side was driving, a page contradicting the overlay, a refusal nothing could
 * undo, a claim nobody re-asked for.
 *
 * One input struct, one answer, and callers read fields rather than rebuilding
 * the reasoning. Pure and free of libnx, so the precedence is testable on the
 * host rather than only on a console.
 */

enum class PadDriver {
    /* MissionControl translates this pad, exactly as it does any other. Rumble
     * goes out through the HOS vibration API; we send it nothing. */
    MissionControl = 0,

    /* We hold the claim and write the pad's own output reports. */
    Akira,
};

enum class PadDriverReason {
    Native = 0,          /* driven by akira, as asked */
    Released,            /* the backend's switch is off, for the whole console */
    Unsupported,         /* this MissionControl has no ownership to give */
    Basic,               /* set to basic - so no claim, and therefore no analog either */
    NotSupportedPad,     /* not a pad we can drive natively */
    NoAddress,           /* nothing has told us where the pad is yet */
    NotWanted,           /* nothing is streaming to it */
};

struct PadOutputInputs {
    /* The pad is one we know how to write reports for. */
    bool supported_pad = false;

    /* Its profile asks for native output rather than basic. */
    bool profile_native = false;

    /* The backend's console-wide switch is on. */
    bool backend_enabled = true;

    /* The backend refused the claim, or we have not been told otherwise. */
    bool ownership_refused = false;

    /* We hold the claim right now. */
    bool owns_output = false;

    /* Something is driving output at all - a stream, rather than a menu. */
    bool wanted = false;

    /* A raw report has told us the pad's Bluetooth address. */
    bool address_valid = false;

    /* The profile asks the console for a haptic waveform. */
    bool haptics_wanted = false;

    /* Frames have been landing, or nothing has failed yet. */
    bool haptics_landing = true;
};

/* What the settings page and the path builder need: the answer and the why.
 * The rest of PadOutputState is for the pump. */
struct PadDriverState {
    PadDriver       driver = PadDriver::MissionControl;
    PadDriverReason reason = PadDriverReason::NotSupportedPad;
};

struct PadOutputState {
    PadDriver       driver = PadDriver::MissionControl;
    PadDriverReason reason = PadDriverReason::NotSupportedPad;

    /* Ask the backend to cede this pad. Separate from owning it: this is the
     * question the pump puts once a second, and the answer to it is what tells
     * everything else whether the switch has moved. */
    bool want_claim = false;

    /* Send 0x31 state frames - rumble, triggers, trigger effects. */
    bool write_state = false;

    /* Stream 0x39 haptic frames to the coils. */
    bool stream_haptics = false;
};

PadOutputState ResolvePadOutput(const PadOutputInputs& in);

const char* PadDriverName(PadDriver driver);
const char* PadDriverReasonName(PadDriverReason reason);

} // namespace akira::input

#endif // AKIRA_INPUT_PAD_OUTPUT_STATE_HPP
