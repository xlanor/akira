#ifndef AKIRA_INPUT_RUMBLE_PROFILE_HPP
#define AKIRA_INPUT_RUMBLE_PROFILE_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#include "input/ps_output.hpp"

namespace akira::input {

/*
 * How one controller's rumble is shaped, and how the right shaping is found.
 *
 * The values used to be four constants in HapticManager, chosen for a Joy-Con
 * because that was the only pad Akira could drive. Applied to every controller
 * they are simply wrong: 160 and 320 Hz are actuator resonances that an ERM
 * motor does not have, and the 0.63 ceiling exists because a Joy-Con at full
 * travel is unpleasant, which is not true of a motor that has a stall threshold
 * at the other end of its range.
 *
 * Deliberately free of libnx, borealis and chiaki types so it can be tested on
 * the host.
 */

/* Anything unrecognised, and the tier every lookup ends at. */
inline constexpr const char* kRumbleKeyDefault = "default";

/*
 * Joy-Cons and a genuine Pro Controller, together.
 *
 * Superseded by the two category keys below and kept only so an existing
 * config still reads - on load it seeds both of them and is not written back.
 */
inline constexpr const char* kRumbleKeySwitch = "switch";

/*
 * What kind of thing this is, rather than which one it is.
 *
 * The tiers above this are a MAC and a vid/pid: one exact pad, and one model.
 * Neither is a useful place to put "how a Joy-Con should feel", because that
 * answer is the same for every Joy-Con and there is no vendor id to hang it
 * on. These are the categories worth tuning as a group, and they are also the
 * ones whose capabilities genuinely differ - a DualSense has voice coils and
 * adaptive triggers, a Joy-Con has HD rumble, a Pro Controller has neither.
 */
enum class PadCategory {
    DualSense = 0,
    JoyCon,
    SwitchPro,
    Generic,
};

inline constexpr const char* kRumbleKeyDualSense = "dualsense";
inline constexpr const char* kRumbleKeyJoyCon    = "joycon";
inline constexpr const char* kRumbleKeySwitchPro = "switchpro";
inline constexpr const char* kRumbleKeyGeneric   = "generic";

inline const char* RumbleKeyForCategory(PadCategory category)
{
    switch (category) {
        case PadCategory::DualSense: return kRumbleKeyDualSense;
        case PadCategory::JoyCon:    return kRumbleKeyJoyCon;
        case PadCategory::SwitchPro: return kRumbleKeySwitchPro;
        case PadCategory::Generic:   return kRumbleKeyGeneric;
    }
    return kRumbleKeyGeneric;
}

/*
 * Which category a pad belongs to.
 *
 * switch_native means HOS drives it directly, so there is no vendor id to read
 * and the only distinction available is whether it is a Joy-Con or a Pro
 * Controller - which the caller knows from the npad style and passes in,
 * because this header stays free of libnx so it can be tested on the host.
 */
inline PadCategory PadCategoryFor(std::uint16_t vendor_id, std::uint16_t product_id,
                                  bool switch_native, bool joycon)
{
    if (switch_native)
        return joycon ? PadCategory::JoyCon : PadCategory::SwitchPro;

    if (PadTakesDirectOutput(vendor_id, product_id))
        return PadCategory::DualSense;

    return PadCategory::Generic;
}

inline const char* PadCategoryName(PadCategory category)
{
    switch (category) {
        case PadCategory::DualSense: return "DualSense";
        case PadCategory::JoyCon:    return "Joy-Con";
        case PadCategory::SwitchPro: return "Switch Pro";
        case PadCategory::Generic:   return "Other controllers";
    }
    return "Other controllers";
}

/*
 * Native or through MissionControl, chosen rather than detected.
 */
enum class PadOutputMode {
    Native = 0,
    Basic,
};

/*
 * Where this pad's rumble comes from.
 *
 * Not a request to the console - the console sends the haptic waveform either
 * way, because we always announce as a DualSense. This is what akira does with
 * it locally, and it is only a question on a pad with no voice coils to play
 * the waveform on: derive rumble from the haptic track, which carries the
 * envelope and the transients a rumble command cannot, or take the game's own
 * rumble commands and play them as written.
 *
 * A DualSense is never asked. It has the coils, so it plays the thing itself.
 */
enum class RumbleSource {
    Off = 0,
    Derived,
    Game,
};

enum class HapticIntensity {
    Off = 0,
    VeryWeak,
    Weak,
    Normal,
    Strong,
    VeryStrong,
};

struct RumbleProfile {
    float strength  = 1.0f;   /* master scale on whatever the console asks for */
    float ceiling   = 1.0f;   /* loudest this pad may be driven */

    /*
     * Whether the console's two values are two sides or two bands.
     *
     * False on a Switch pad and that is not a detail: one actuator plays a low
     * and a high band simultaneously, so left and right are not sides at all
     * and driving them as such produces something nobody asked for. True on any
     * pad with a motor per side.
     */
    bool  per_motor = true;

    /* Only ever read when per_motor is false - an ERM has no resonance. */
    float freq_low  = 160.0f;
    float freq_high = 320.0f;

    /* Smoothing for an actuator that would otherwise chatter on a signal
     * moving faster than it can settle. A motor's own inertia does this. */
    float envelope_attack = 0.60f;
    float envelope_decay  = 0.85f;

    /*
     * The only field a directly-written pad has.
     *
     * Everything above shapes what the console sent; on a pad we write to
     * ourselves the console's rumble arrives unaltered and there is nothing to
     * shape. But with haptics enabled the console sends no rumble at all - only
     * the haptic stream - and turning that into two amplitudes is our maths,
     * which is what this scales.
     */
    HapticIntensity haptic_intensity = HapticIntensity::Normal;

    /*
     * What this pad is asked to be.
     *
     * Native reads the pad's own input report and writes its own output
     * report, which is where analog triggers, adaptive triggers and haptics
     * come from. Basic hands the pad back to MissionControl's translation and
     * takes what every other controller gets - rumble, and nothing else.
     *
     * Basic is not a fallback we pick when something breaks; it is what
     * somebody chooses when they would rather not run the sysmodule's output
     * path at all. Nothing reaches Native without passing PadTakesDirectOutput
     * first, so a pad that cannot read a DualSense report is never offered
     * one whatever its profile says.
     */
    PadOutputMode output_mode = PadOutputMode::Native;


    /*
     * Where this pad's rumble comes from. Only ever asked of a pad without
     * coils; see RumbleSource.
     */
    RumbleSource rumble_source = RumbleSource::Derived;

    /*
     * The lightbar, when akira is the one driving the pad.
     *
     * Off means we never claim the LED, so it keeps whatever MissionControl
     * painted - a player number. On means the pad shows this colour for as long
     * as akira holds it, and reverts the moment MissionControl takes it back,
     * which makes the pad itself the indicator of who is driving it.
     */
    bool          lightbar_enabled = false;
    std::uint8_t  lightbar_r = 0x00;
    std::uint8_t  lightbar_g = 0x80;
    std::uint8_t  lightbar_b = 0xff;
};

/*
 * What chiaki does for every pad that is not a DualSense: hand the console's
 * two values across at full range, per motor, unshaped.
 */
inline RumbleProfile DefaultRumbleProfile()
{
    RumbleProfile p;
    p.strength  = 1.0f;
    p.ceiling   = 1.0f;
    p.per_motor = true;
    return p;
}

/*
 * Exactly what Joy-Cons do today, including the ceiling that was never a
 * setting.
 *
 * 0.63 is 255/400, the cap hiding in HapticManager's hapticBase. Writing it
 * down is what makes it tunable; seeding it at the constant's own value is what
 * makes today's Joy-Cons feel identical tomorrow.
 */
inline RumbleProfile SwitchRumbleProfile()
{
    RumbleProfile p;
    p.strength  = 1.0f;
    p.ceiling   = 0.63f;
    p.per_motor = false;
    p.freq_low  = 160.0f;
    p.freq_high = 320.0f;
    return p;
}

/* "054c:0ce6" - every pad of one model. */
std::string RumbleKeyForModel(std::uint16_t vendor_id, std::uint16_t product_id);

/* "24:a6:fa:a1:f1:11" - the exact pad in your hands. */
std::string RumbleKeyForUnit(const std::uint8_t* address);

/*
 * Which tier a key belongs to, by length. A MAC is seventeen characters and a
 * vid/pid is nine, so the two can never be confused and neither needs a prefix
 * to remember when reading the file by hand.
 */
bool RumbleKeyIsUnit(const std::string& key);
bool RumbleKeyIsModel(const std::string& key);
bool RumbleKeyIsCategory(const std::string& key);

/*
 * Converting the console's haptic audio into two motor amplitudes.
 *
 * This is not a fallback for a pad we cannot reach. With haptics enabled the
 * PS5 stops sending discrete rumble commands entirely and sends the haptic
 * stream instead, so on a DualSense this is the *only* source of rumble there
 * is - which is why it needs a setting of its own rather than being a constant.
 */
struct HapticRumble {
    bool          emit  = false;  /* false means silence, not zero amplitude */
    std::uint16_t left  = 0;
    std::uint16_t right = 0;

    /* The gated mean before the intensity multiply. Kept because after scaling
     * a clipped signal and a loud one are the same number, and telling them
     * apart is the whole diagnosis. */
    std::uint32_t raw_left  = 0;
    std::uint32_t raw_right = 0;
};

/*
 * Below this the stream is room tone rather than an effect.
 *
 * Without a gate the residual level in the haptic signal becomes a permanent
 * low hum from the motors, which reads as broken rather than quiet.
 */
inline constexpr std::uint32_t kHapticNoiseFloor = 100;

/*
 * An ERM below its stall threshold draws current and whines without turning.
 * Anything audible at all is rounded up to something that actually spins.
 *
 * Three of 255 rather than two, because HapticManager zeroes an envelope below
 * two and the old floor of 1<<9 converted to 0.00781 against a threshold of
 * 0.00784 - so every value this lifted was zeroed on the next line and the
 * floor never once did its job. It has to sit above that threshold, not on it.
 */
inline constexpr std::uint16_t kHapticMotorFloor = 3u << 8;

/*
 * Below this an 8-bit haptic sample moves nothing, so the block is worth more
 * as air time we did not spend than as a frame the actuators ignore.
 */
/*
 * Only true silence is skipped.
 *
 * This was 3 out of 127 - a block under about two percent of full scale was
 * dropped - back when every write was scarce and most of them were refused
 * anyway. Against real game audio that discarded three blocks in four, which
 * is the quiet texture between impacts rather than nothing at all. Air time is
 * no longer the constraint, so the gate now only skips blocks that would move
 * the actuators not at all.
 */
inline constexpr int kHapticBlockFloor = 0;

/*
 * The same intensity setting, as a gain on the samples themselves.
 *
 * It only ever scaled the motor approximation, so a pad driven through its own
 * actuators ignored it entirely and played whatever the console sent at unity.
 * Normal is unity, so nothing changes for anyone who has not touched it.
 */
inline float HapticSampleGain(HapticIntensity intensity)
{
    switch (intensity) {
        case HapticIntensity::Off:        return 0.0f;
        case HapticIntensity::VeryWeak:   return 0.4f;
        case HapticIntensity::Weak:       return 0.7f;
        case HapticIntensity::Normal:     return 1.0f;
        case HapticIntensity::Strong:     return 1.6f;
        case HapticIntensity::VeryStrong: return 2.4f;
    }
    return 1.0f;
}

/*
 * The same intensity, as the divisor the legacy motor path wants.
 *
 * That path came with two hard-coded divisors reached through a Weak/Strong
 * setting - 128 and 50 - and no way to ask for anything else. 50 is what
 * Strong was, so Normal lands exactly where the old Strong did and the rest of
 * the scale follows the same gain curve the coils use, which is what makes one
 * intensity control mean one thing on both paths.
 */
inline int HapticBaseForIntensity(HapticIntensity intensity)
{
    const float gain = HapticSampleGain(intensity);
    if (gain <= 0.0f)
        return 400;
    return (int)(50.0f / gain);
}

/*
 * Peak absolute amplitude per channel, gated, scaled, floored.
 *
 * Peak rather than mean, because the mean of a waveform is about two thirds of
 * its crest - so a full-scale haptic signal converted by its average could
 * never ask for more than 162 of the motor's 255, and ordinary game content
 * landed far below that. What the motor should follow is how hard the effect
 * hits, and the envelope in HapticManager is what keeps a single loud sample
 * from reading as a spike.
 *
 * Sixteen bit throughout: the stream is int16 and the pad takes eight, so
 * narrowing early - as Akira did, dividing by 64 before anything else - throws
 * away the resolution the later scaling needs.
 */
/*
 * motor_stalls says whether the thing being driven is a real rotating mass.
 *
 * The floor exists for one, and a DualSense does not have one: what it calls
 * motors are the same voice coils the haptics play through, run in rumble
 * emulation, and a coil has no stall to lift a signal over. Applying it there
 * makes quiet passages audible that should have stayed quiet - which is the
 * opposite of what the floor is for.
 */
HapticRumble HapticAudioToRumble(const std::int16_t* stereo, std::size_t frames,
                                 HapticIntensity intensity, bool motor_stalls = true);

} // namespace akira::input

#endif // AKIRA_INPUT_RUMBLE_PROFILE_HPP
