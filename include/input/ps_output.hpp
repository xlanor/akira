#ifndef AKIRA_INPUT_PS_OUTPUT_HPP
#define AKIRA_INPUT_PS_OUTPUT_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace akira::input {

/*
 * Writing to a PlayStation pad, rather than asking HOS to.
 *
 * The read side of this pairs with ps_report.hpp; this is the other half of the
 * same protocol and lives beside it for that reason. Everything downstream of
 * here - the sysmodule, the IPC call - carries opaque bytes, so a second pad
 * model is a change to this header alone.
 *
 * Why not the HOS vibration API: MissionControl translates a Switch vibration
 * into an ERM command and, having no resonance to tune, discards frequency
 * entirely. Both amplitude bands survive, nothing else does, and the trigger
 * effects the console sends never had a Switch equivalent to be translated into
 * in the first place.
 *
 * Deliberately free of libnx and chiaki types so it can be tested on the host.
 *
 * The report layout is SDL's DS5EffectsState_t (zlib licensed, Sam Lantinga),
 * expressed here as offsets rather than a packed struct.
 */

/* Bytes the pad expects in an effects payload, whatever we choose to fill in. */
inline constexpr std::size_t kDs5EffectsBytes = 47;

/* Parameters one adaptive trigger takes: an effect type, then ten bytes whose
 * meaning depends on it. Carried opaquely - the console composes them and the
 * pad reads them, and nothing in between has any business interpreting them. */
inline constexpr std::size_t kDs5TriggerParamBytes = 10;

/*
 * A Bluetooth 0x31 frame: report id, a sequence byte, a tag, the payload, zero
 * padding, and a trailing CRC32. Fixed length - the pad rejects a short one.
 *
 * The three header bytes are not decoration. Linux's hid-playstation declares
 * the same layout (report_id, seq_tag, tag, common, reserved[24], crc32) and
 * so does DS5Dongle, which drives a real pad over the air: the sequence is the
 * frame counter in the high nibble, and the tag is a constant 0x10. Getting
 * either wrong - or starting the payload a byte early, which is the same
 * mistake twice - produces a frame the pad accepts and ignores.
 */
inline constexpr std::size_t kDs5BluetoothFrameBytes   = 78;
inline constexpr std::size_t kDs5BluetoothPayloadStart = 3;
inline constexpr std::uint8_t kDs5OutputTag            = 0x10;

enum Ds5EffectsOffset : std::size_t {
    Ds5Effects_EnableBits1  = 0,
    Ds5Effects_EnableBits2  = 1,
    Ds5Effects_RumbleRight  = 2,
    Ds5Effects_RumbleLeft   = 3,
    Ds5Effects_HeadphoneVol = 4,
    Ds5Effects_SpeakerVol   = 5,
    Ds5Effects_AudioControl = 7,
    Ds5Effects_RightTrigger = 10, /* 11 bytes: type, then 10 of parameters */
    Ds5Effects_LeftTrigger  = 21, /* 11 bytes, same shape */
    Ds5Effects_Intensity     = 36, /* two nibbles, 0 is full strength each */
    Ds5Effects_AudioControl2 = 37,
    Ds5Effects_EnableBits3  = 38,
    Ds5Effects_LedRed       = 44,
    Ds5Effects_LedGreen     = 45,
    Ds5Effects_LedBlue      = 46,
};

/*
 * Which fields of the payload the pad should read. Anything not enabled is
 * ignored rather than applied as zero, which is what makes it safe to send a
 * rumble-only frame without blanking the lightbar MissionControl set.
 */
enum Ds5Enable1 : std::uint8_t {
    Ds5Enable1_RumbleLegacy = 1u << 0,

    /*
     * Selects emulated rumble, taking the voice coils away from audio haptics.
     *
     * It is a claim bit like the rest, which is the part that cost us: leaving
     * it clear does not hand the coils back, it only declines to say anything,
     * so the pad stays in whichever mode it was last put into. The transition
     * the other way is a frame with the motors at zero that sets RumbleLegacy
     * and leaves this clear - see ApplyDs5VibrationMode.
     */
    Ds5Enable1_HapticsSelect = 1u << 1,

    Ds5Enable1_RightTrigger = 1u << 2,
    Ds5Enable1_LeftTrigger  = 1u << 3,

    Ds5Enable1_HeadphoneVol  = 1u << 4,
    Ds5Enable1_SpeakerVol    = 1u << 5,
    Ds5Enable1_AudioControl  = 1u << 7,
};

enum Ds5Enable2 : std::uint8_t {
    /* The three colour bytes are read only when this is set, which is what
     * lets every other frame we send leave the lightbar alone. */
    Ds5Enable2_LedColor      = 1u << 2,

    Ds5Enable2_Intensity     = 1u << 6,
    Ds5Enable2_AudioControl2 = 1u << 7,
};

enum Ds5Enable3 : std::uint8_t {
    Ds5Enable3_Rumble          = 1u << 2,
    Ds5Enable3_RumbleNotHaptic = 1u << 3,
};

/*
 * How hard the pad is allowed to play what it is sent.
 *
 * These are the console's own values - Settings, Accessories, Controller -
 * which arrive over the stream because the user set them for this account and
 * they are meant to hold wherever the pad is being driven from. The numbering
 * is the wire's, not a scale: Off is zero and Strong is one, so anything that
 * treats it as an amount gets it backwards.
 */
enum class Ds5EffectIntensity : std::uint8_t {
    Off    = 0,
    Strong = 1,
    Medium = 2,
    Weak   = 3,
};

/*
 * The pad attenuates, not us.
 *
 * One byte carries both: triggers in the high nibble, vibration in the low.
 * Zero is full strength in either and the value counts down from there, and
 * "off" is the top of the range rather than the bottom - which is the second
 * place this encoding inverts and the reason it is written out rather than
 * computed.
 *
 * Doing it here rather than scaling amplitudes ourselves is what keeps the
 * trigger side honest: an effect's ten parameters are opaque, so there is no
 * arithmetic we could do to make one gentler. The pad knows how.
 */
inline std::uint8_t Ds5IntensityByte(Ds5EffectIntensity vibration,
                                     Ds5EffectIntensity trigger)
{
    std::uint8_t low = 0x00;
    switch (vibration) {
        case Ds5EffectIntensity::Strong: low = 0x00; break;
        case Ds5EffectIntensity::Medium: low = 0x02; break;
        case Ds5EffectIntensity::Weak:   low = 0x03; break;
        case Ds5EffectIntensity::Off:    low = 0x0f; break;
    }

    std::uint8_t high = 0x00;
    switch (trigger) {
        case Ds5EffectIntensity::Strong: high = 0x00; break;
        case Ds5EffectIntensity::Medium: high = 0x60; break;
        case Ds5EffectIntensity::Weak:   high = 0x90; break;
        case Ds5EffectIntensity::Off:    high = 0xf0; break;
    }

    return (std::uint8_t)(high | low);
}

/* What the pad is sent before the console has said otherwise: everything at
 * full, which is what the field held when it was a constant. */
inline constexpr std::uint8_t kDs5IntensityFull = 0x00;

/*
 * The same setting as a plain multiplier, for the amplitudes we generate.
 *
 * The byte above covers everything the pad renders for itself. It does not
 * cover a haptic waveform we stream into the coils, and it does not reach a
 * pad driven through MissionControl's translation at all - neither has any
 * idea the setting exists. Those we scale here, so that turning vibration down
 * on the console turns it down whatever is in your hands.
 */
inline float Ds5IntensityScale(Ds5EffectIntensity intensity)
{
    switch (intensity) {
        case Ds5EffectIntensity::Off:    return 0.0f;
        case Ds5EffectIntensity::Weak:   return 0.33f;
        case Ds5EffectIntensity::Medium: return 0.5f;
        case Ds5EffectIntensity::Strong: return 1.0f;
    }
    return 1.0f;
}

/* The console's numbering, which is not an order - see Ds5EffectIntensity.
 * Anything outside it is a newer console than this build knows, and full
 * strength is what it already gets before the first message arrives. */
inline Ds5EffectIntensity Ds5IntensityFromWire(std::uint8_t value)
{
    switch (value) {
        case 0: return Ds5EffectIntensity::Off;
        case 1: return Ds5EffectIntensity::Strong;
        case 2: return Ds5EffectIntensity::Medium;
        case 3: return Ds5EffectIntensity::Weak;
        default: return Ds5EffectIntensity::Strong;
    }
}

/*
 * CRC-32/ISO-HDLC, the one every Sony pad uses on its Bluetooth frames.
 * Bitwise rather than table-driven: this runs a few times a second at most, and
 * a 1 KiB table in a header that several translation units include is a worse
 * trade than the loop.
 */
inline std::uint32_t Ds5Crc32(std::uint32_t crc, const std::uint8_t* data, std::size_t len)
{
    for (std::size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xedb88320u) : (crc >> 1);
    }
    return crc;
}

/*
 * The trailing checksum, over the whole frame bar the four bytes it occupies.
 *
 * Shared by every report this pad takes, because the scheme is the same for
 * all of them - only the length differs.
 */
inline void StampDs5Crc(std::uint8_t* out, std::size_t len)
{
    const std::uint8_t header = 0xa2;
    std::uint32_t crc = 0xffffffffu;
    crc = Ds5Crc32(crc, &header, 1);
    crc = Ds5Crc32(crc, out, len - 4);
    crc ^= 0xffffffffu;

    out[len - 4] = (std::uint8_t)(crc & 0xff);
    out[len - 3] = (std::uint8_t)((crc >> 8) & 0xff);
    out[len - 2] = (std::uint8_t)((crc >> 16) & 0xff);
    out[len - 1] = (std::uint8_t)((crc >> 24) & 0xff);
}

/*
 * Wrap an effects payload as the Bluetooth output report.
 *
 * The CRC covers a leading 0xa2 that is never transmitted - it is the HID
 * Bluetooth DATA/Output header byte, which the host adds below this layer and
 * the pad checksums as though it were part of the frame.
 */
/*
 * Where the payload starts, and why there is a choice.
 *
 * Linux, DS5Dongle and DS5_Bridge all write report id, sequence, tag 0x10,
 * then the payload at offset 3. MissionControl writes report id, 0x02, then
 * the payload at offset 2 - one byte earlier, no tag - and MissionControl is
 * the implementation demonstrably driving this pad, on this console, over the
 * link it established itself.
 *
 * Three references against the one that is actually working here. Rather than
 * decide from a distance which is right, both are buildable and the hands can
 * settle it.
 */
enum class Ds5FrameLayout {
    Tagged,       /* offset 3, tag 0x10 - Linux, DS5Dongle, DS5_Bridge */
    MissionCtl,   /* offset 2, no tag  - what MissionControl sends */
};

inline std::size_t BuildDs5BluetoothFrameAs(Ds5FrameLayout layout, std::uint8_t seq,
                                            const std::uint8_t* effects, std::size_t effects_len,
                                            std::uint8_t* out, std::size_t out_len)
{
    if (effects == nullptr || out == nullptr)
        return 0;
    if (effects_len > kDs5EffectsBytes || out_len < kDs5BluetoothFrameBytes)
        return 0;

    std::memset(out, 0, kDs5BluetoothFrameBytes);
    out[0] = 0x31;

    if (layout == Ds5FrameLayout::MissionCtl) {
        out[1] = 0x02;
        std::memcpy(out + 2, effects, effects_len);
    } else {
        out[1] = (std::uint8_t)((seq & 0x0f) << 4);
        out[2] = kDs5OutputTag;
        std::memcpy(out + kDs5BluetoothPayloadStart, effects, effects_len);
    }

    StampDs5Crc(out, kDs5BluetoothFrameBytes);
    return kDs5BluetoothFrameBytes;
}

inline std::size_t BuildDs5BluetoothFrame(std::uint8_t seq,
                                          const std::uint8_t* effects, std::size_t effects_len,
                                          std::uint8_t* out, std::size_t out_len)
{
    if (effects == nullptr || out == nullptr)
        return 0;
    if (effects_len > kDs5EffectsBytes || out_len < kDs5BluetoothFrameBytes)
        return 0;

    std::memset(out, 0, kDs5BluetoothFrameBytes);
    out[0] = 0x31;
    out[1] = (std::uint8_t)((seq & 0x0f) << 4);
    out[2] = kDs5OutputTag;
    std::memcpy(out + kDs5BluetoothPayloadStart, effects, effects_len);

    StampDs5Crc(out, kDs5BluetoothFrameBytes);
    return kDs5BluetoothFrameBytes;
}

/*
 * A rumble-only frame, at the amplitudes the console asked for.
 *
 * Enable3 rather than the legacy bit in Enable1: firmware 0x0224 added a second
 * vibration mode that takes amplitudes at face value, where the legacy one
 * attenuates them on its own curve. That curve is why a MissionControl-driven
 * DualSense feels weaker than the console intended. A pad older than that
 * firmware ignores this bit and does not buzz at all, which is a visible
 * failure rather than a subtle one - and reading the firmware version means a
 * feature report we have no path for yet.
 */
/*
 * Which vibration mode the frame puts the pad into, and the transition back.
 *
 * The two mode bits in EnableBits1 and the two in EnableBits3 are claims, so
 * the pad keeps its current mode unless a frame states one. That makes silence
 * the important case: a zero-amplitude frame that still claims HapticsSelect
 * stops the motors but leaves the coils owned by rumble emulation, and any
 * haptic audio sent afterwards is silently discarded. Setting RumbleLegacy
 * with HapticsSelect clear is the transition that gives them back.
 */
inline void ApplyDs5VibrationMode(std::uint8_t* effects,
                                  std::uint8_t left, std::uint8_t right,
                                  bool legacy_vibration)
{
    effects[Ds5Effects_EnableBits1] &= (std::uint8_t)~(Ds5Enable1_RumbleLegacy |
                                                       Ds5Enable1_HapticsSelect);
    effects[Ds5Effects_EnableBits3] &= (std::uint8_t)~(Ds5Enable3_Rumble |
                                                       Ds5Enable3_RumbleNotHaptic);

    effects[Ds5Effects_RumbleLeft]  = left;
    effects[Ds5Effects_RumbleRight] = right;

    if ((left | right) == 0) {
        effects[Ds5Effects_EnableBits1] |= Ds5Enable1_RumbleLegacy;
        return;
    }

    effects[Ds5Effects_EnableBits1] |= Ds5Enable1_HapticsSelect;

    if (legacy_vibration)
        effects[Ds5Effects_EnableBits1] |= Ds5Enable1_RumbleLegacy;
    else
        effects[Ds5Effects_EnableBits3] |= Ds5Enable3_Rumble;
}

inline std::size_t BuildDs5RumbleFrame(std::uint8_t seq,
                                       std::uint8_t left, std::uint8_t right,
                                       std::uint8_t* out, std::size_t out_len,
                                       bool legacy_vibration = false,
                                       Ds5FrameLayout layout = Ds5FrameLayout::Tagged,
                                       std::uint8_t intensity = kDs5IntensityFull)
{
    std::uint8_t effects[kDs5EffectsBytes] = {};

    /*
     * A complete frame every time, never a partial one.
     *
     * The enable bits are claims: a field you do not claim keeps whatever the
     * last frame left there. Sending minimal frames meant the pad accumulated
     * state across every write - a test at full amplitude inheriting the mode
     * a previous one set, a left-only frame leaving its zero on the right.
     * Identical bytes produced different results depending on what had gone
     * before, which made every comparison meaningless and looked exactly like
     * something else was fighting us.
     *
     * MissionControl sends every field it cares about on every push. So do we
     * now: the frame below fully determines the pad's rumble state, and
     * nothing carries over.
     */

    /*
     * Claim full motor power explicitly, because something else already had
     * an opinion.
     *
     * Linux never touches this control and for Linux that is correct - it is
     * the only thing driving the pad. We are not: MissionControl pushes its
     * own 0x31 frames to the same controller and bakes a reduction into every
     * one of them, from a setting that defaults to 4 of 8 - half power. A
     * frame that does not claim the control inherits that, which is exactly
     * what "very faint" felt like.
     *
     * So the claim is unconditional, and the value is ours to choose - which
     * is where the console's own intensity setting lands, full strength being
     * simply the case where it has not said otherwise.
     */
    effects[Ds5Effects_EnableBits2] = Ds5Enable2_Intensity;
    effects[Ds5Effects_Intensity]   = intensity;

    /*
     * Which vibration generation to ask for.
     *
     * Firmware from 2.21 onward takes the second one; older pads want the
     * legacy bit in EnableBits1 instead, and reading the feature report that
     * says which is a thing we cannot do yet. Both are offered so the answer
     * can come from a hand on the controller rather than a guess.
     */
    ApplyDs5VibrationMode(effects, left, right, legacy_vibration);

    return BuildDs5BluetoothFrame(seq, effects, sizeof(effects), out, out_len);
}

/*
 * Wake the pad's audio subsystem before sending it any.
 *
 * Haptic frames were being accepted - seventy of seventy, rc zero - and felt
 * like absolutely nothing, because nothing had ever told the controller its
 * audio path was in use. DS5Dongle and DS5_Bridge both own the pad from the
 * moment it connects and configure this at setup; we arrive late, on a link
 * MissionControl established, and MissionControl has no reason to enable an
 * audio route it never intends to use.
 *
 * So the claim bits, the volumes, and the output path, exactly as DS5_Bridge
 * sets them. Sent once before a haptic stream rather than per frame - it is
 * configuration, not content.
 */
inline std::size_t BuildDs5AudioEnableFrame(std::uint8_t seq,
                                            std::uint8_t* out, std::size_t out_len)
{
    std::uint8_t effects[kDs5EffectsBytes] = {};

    effects[Ds5Effects_EnableBits1] = Ds5Enable1_AudioControl
                                    | Ds5Enable1_SpeakerVol
                                    | Ds5Enable1_HeadphoneVol;
    effects[Ds5Effects_EnableBits2] = Ds5Enable2_AudioControl2;

    effects[Ds5Effects_HeadphoneVol] = 0x7f;
    effects[Ds5Effects_SpeakerVol]   = 0x64;

    /* Route to the speaker rather than headphones. The haptic actuators sit on
     * the internal path, and selecting headphones leaves them idle. */
    effects[Ds5Effects_AudioControl]  = 0x30;
    effects[Ds5Effects_AudioControl2] = 0x04;   /* speaker pre-amp gain, mid */

    return BuildDs5BluetoothFrame(seq, effects, sizeof(effects), out, out_len);
}

/*
 * The small haptic frame: report 0x32, 142 bytes.
 *
 * The 547-byte 0x39 form is what a dedicated dongle sends, and a dongle owns
 * the link it sends over. Ours was negotiated by MissionControl for a game
 * controller, and 547-byte writes on it were accepted and then degraded until
 * they were refused outright - which is what a link whose buffers cannot
 * sustain them looks like.
 *
 * This is the form dualsense-bt-haptics uses to do exactly this job over
 * Bluetooth on Windows. Same two packets - 0x11 for the audio section, 0x12
 * for the samples - with a shorter 0x11 payload, half the audio per frame, and
 * a quarter of the size. Twice the frames per second at half the bandwidth.
 *
 * Sixty-four sample bytes is thirty-two stereo pairs, which at 3000Hz is
 * 10.67ms - the interval that implementation paces at, and not a coincidence.
 */
inline constexpr std::size_t kDs5Haptic32FrameBytes  = 142;
inline constexpr std::size_t kDs5Haptic32SampleBytes = 64;

inline std::size_t BuildDs5Haptic32Frame(std::uint8_t seq, std::uint8_t packet_counter,
                                         const std::int8_t* samples, std::size_t sample_bytes,
                                         std::uint8_t* out, std::size_t out_len)
{
    if (samples == nullptr || out == nullptr)
        return 0;
    if (sample_bytes != kDs5Haptic32SampleBytes || out_len < kDs5Haptic32FrameBytes)
        return 0;

    std::memset(out, 0, kDs5Haptic32FrameBytes);

    out[0] = 0x32;
    out[1] = (std::uint8_t)((seq & 0x0f) << 4);

    /* Packet 0x11: the audio section, seven bytes of it. Bytes 5 through 8
     * are zero in this form - the 0x33 variant is the one that fills them,
     * and it terminates the section with 0x40 rather than 0xff. Mixing the
     * two gives a frame that belongs to neither. */
    out[2] = 0x11 | 0x80;
    out[3] = 7;
    out[4] = 0xfe;
    out[5] = out[6] = out[7] = out[8] = 0;
    out[9] = 0xff;
    out[10] = packet_counter;

    /* Packet 0x12: the samples themselves. */
    out[11] = 0x12 | 0x80;
    out[12] = (std::uint8_t)kDs5Haptic32SampleBytes;
    std::memcpy(out + 13, samples, sample_bytes);

    StampDs5Crc(out, kDs5Haptic32FrameBytes);
    return kDs5Haptic32FrameBytes;
}

/*
 * One frame carrying the whole output state.
 *
 * Rumble and trigger effects used to be two frames on two schedules, on the
 * reasoning that unclaimed fields are left alone so each could be sent
 * independently. That reasoning was wrong in the way that matters: unclaimed
 * fields are left alone, which means nothing a partial frame omits can ever be
 * corrected by it, and the pad accumulates whatever every earlier frame
 * happened to set.
 *
 * It also cost double. A write is about seven milliseconds of the single
 * thread every Bluetooth operation on this console shares, and that cost is
 * per write rather than per byte - two small frames are strictly worse than
 * one large one. MissionControl sends everything it owns in a single push, and
 * MissionControl is the implementation that works here.
 */
/*
 * The lightbar, on its own.
 *
 * Nothing else akira sends claims the LED, so the colour on the pad is whoever
 * last set it - which until now was always MissionControl, painting a player
 * number. Sending our own makes the pad say who is driving it: your colour
 * means akira holds the claim, and a player colour means MissionControl took it
 * back. That is the one piece of state that was never observable from across
 * the room.
 *
 * Sent as its own frame rather than folded into the state frame, because the
 * state frame goes out at the rumble rate and the colour changes almost never.
 */
inline std::size_t BuildDs5LightbarFrame(std::uint8_t seq,
                                         std::uint8_t red, std::uint8_t green,
                                         std::uint8_t blue,
                                         std::uint8_t* out, std::size_t out_len,
                                         Ds5FrameLayout layout = Ds5FrameLayout::Tagged)
{
    std::uint8_t effects[kDs5EffectsBytes] = {};

    effects[Ds5Effects_EnableBits2] = Ds5Enable2_LedColor;
    effects[Ds5Effects_LedRed]      = red;
    effects[Ds5Effects_LedGreen]    = green;
    effects[Ds5Effects_LedBlue]     = blue;

    return BuildDs5BluetoothFrameAs(layout, seq, effects, sizeof(effects), out, out_len);
}

inline std::size_t BuildDs5StateFrame(std::uint8_t seq,
                                      std::uint8_t left, std::uint8_t right,
                                      std::uint8_t left_trigger_type,
                                      const std::uint8_t* left_params,
                                      std::uint8_t right_trigger_type,
                                      const std::uint8_t* right_params,
                                      std::uint8_t* out, std::size_t out_len,
                                      bool legacy_vibration = false,
                                      Ds5FrameLayout layout = Ds5FrameLayout::Tagged,
                                      std::uint8_t intensity = kDs5IntensityFull)
{
    std::uint8_t effects[kDs5EffectsBytes] = {};

    ApplyDs5VibrationMode(effects, left, right, legacy_vibration);

    effects[Ds5Effects_EnableBits2] = Ds5Enable2_Intensity;
    effects[Ds5Effects_Intensity]   = intensity;

    /* Triggers ride along. Claimed unconditionally for the same reason as
     * everything else: a frame that declines to mention them cannot put them
     * back, so "no effect" has to be stated rather than implied. */
    effects[Ds5Effects_EnableBits1] |= Ds5Enable1_LeftTrigger | Ds5Enable1_RightTrigger;

    effects[Ds5Effects_LeftTrigger] = left_trigger_type;
    if (left_params != nullptr)
        std::memcpy(effects + Ds5Effects_LeftTrigger + 1, left_params, kDs5TriggerParamBytes);

    effects[Ds5Effects_RightTrigger] = right_trigger_type;
    if (right_params != nullptr)
        std::memcpy(effects + Ds5Effects_RightTrigger + 1, right_params, kDs5TriggerParamBytes);

    return BuildDs5BluetoothFrameAs(layout, seq, effects, sizeof(effects), out, out_len);
}

/*
 * Give the actuators back.
 *
 * They are claims, not values, so carrying the mode bits clear selects nothing
 * and the pad stays wherever it was. The transition is the zero-amplitude case
 * of the ordinary vibration mode: motors at zero, RumbleLegacy set,
 * HapticsSelect clear.
 */
inline std::size_t BuildDs5HapticsRestoreFrame(std::uint8_t seq,
                                               std::uint8_t* out, std::size_t out_len,
                                               Ds5FrameLayout layout = Ds5FrameLayout::Tagged)
{
    std::uint8_t effects[kDs5EffectsBytes] = {};

    ApplyDs5VibrationMode(effects, 0, 0, false);

    effects[Ds5Effects_EnableBits2] = Ds5Enable2_Intensity;
    effects[Ds5Effects_Intensity]   = kDs5IntensityFull;

    return BuildDs5BluetoothFrameAs(layout, seq, effects, sizeof(effects), out, out_len);
}


/*
 * A trigger-effects frame.
 *
 * Separate from the rumble frame rather than merged into one, because the
 * enable bits make each field independent: a frame that does not claim the
 * rumble fields leaves the motors exactly as they were. Two small frames on
 * their own schedules beat one frame that has to know about both.
 */
inline std::size_t BuildDs5TriggerFrame(std::uint8_t seq,
                                        std::uint8_t left_type, const std::uint8_t* left_params,
                                        std::uint8_t right_type, const std::uint8_t* right_params,
                                        std::uint8_t* out, std::size_t out_len,
                                        std::uint8_t intensity = kDs5IntensityFull)
{
    if (left_params == nullptr || right_params == nullptr)
        return 0;

    std::uint8_t effects[kDs5EffectsBytes] = {};

    effects[Ds5Effects_EnableBits1] = Ds5Enable1_LeftTrigger | Ds5Enable1_RightTrigger;
    effects[Ds5Effects_EnableBits2] = Ds5Enable2_Intensity;
    effects[Ds5Effects_Intensity]   = intensity;

    effects[Ds5Effects_LeftTrigger] = left_type;
    std::memcpy(effects + Ds5Effects_LeftTrigger + 1, left_params, kDs5TriggerParamBytes);

    effects[Ds5Effects_RightTrigger] = right_type;
    std::memcpy(effects + Ds5Effects_RightTrigger + 1, right_params, kDs5TriggerParamBytes);

    return BuildDs5BluetoothFrame(seq, effects, sizeof(effects), out, out_len);
}

/*
 * Streaming the console's haptic waveform to the pad's voice coils.
 *
 * This is the report a DualSense actually feels through. Its two motors are
 * for PS4 compatibility; everything a PS5 game does to your hands is this
 * waveform driving the coils, which is why no amount of tuning the motors ever
 * felt right.
 *
 * It travels as an ordinary HID output report over the interrupt channel - the
 * same route the effects report takes - rather than over Bluetooth audio. That
 * matters because it means the transport we already have reaches it.
 *
 *   [0]        0x39
 *   [1]        sequence, four bits in the high nibble
 *   [2]        0x91
 *   [3]        6          blocks described
 *   [4]        0x7e       audio enables; low bit adds the microphone
 *   [5..8]     buffer length
 *   [9]        packet counter, stepping by two
 *   [10]       0xd2       haptics block header
 *   [11]       64         bytes per block
 *   [12..140)  two blocks of stereo signed 8-bit samples
 *   [140..]    the speaker's opus blocks, which we leave empty
 */
inline constexpr std::size_t kDs5HapticFrameBytes  = 547;
inline constexpr std::size_t kDs5HapticBlockBytes  = 64;

/* 32 stereo pairs per block, two blocks - 21.3ms at the coils' own rate. */
inline constexpr std::size_t kDs5HapticFrames      = kDs5HapticBlockBytes;

/*
 * 3000 Hz, found by sweeping a generator against the hardware rather than from
 * any published figure. It is also exactly what chiaki's haptic sink delivers,
 * so nothing needs resampling on the way through.
 */
inline constexpr int kDs5HapticSampleRate = 3000;

/* DS5Dongle's default, and what the working frame carries. Deliberately not
 * the block size. */
inline constexpr std::uint8_t kDs5AudioBufferLength = 48;

inline std::size_t BuildDs5HapticFrame(std::uint8_t sequence, std::uint8_t packet_counter,
                                       const std::int8_t* samples, std::size_t sample_bytes,
                                       std::uint8_t* out, std::size_t out_len)
{
    if (samples == nullptr || out == nullptr)
        return 0;
    if (sample_bytes != kDs5HapticBlockBytes * 2 || out_len < kDs5HapticFrameBytes)
        return 0;

    std::memset(out, 0, kDs5HapticFrameBytes);

    out[0] = 0x39;
    out[1] = (std::uint8_t)((sequence & 0x0f) << 4);
    out[2] = 0x91;
    out[3] = 6;
    out[4] = 0x7e;

    /*
     * The audio buffer length, which is not the block size.
     *
     * These were 64 here on the reasoning that the field had to agree with the
     * per-block sample count below. It does not: DS5Dongle sends its own
     * audio_buffer_length, 48 by default, alongside a 64-byte block, and a
     * frame captured out of the console's memory during a run that moved the
     * actuators reads 7e 30 30 30 30 - four bytes of 0x30, which is 48.
     */
    out[5] = out[6] = out[7] = out[8] = kDs5AudioBufferLength;

    out[9]  = packet_counter;
    out[10] = 0xd2;
    out[11] = (std::uint8_t)kDs5HapticBlockBytes;

    std::memcpy(out + 12, samples, sample_bytes);

    StampDs5Crc(out, kDs5HapticFrameBytes);
    return kDs5HapticFrameBytes;
}

/* Pads driven this way rather than through MissionControl's translation. A
 * DualShock 4 is absent on purpose: it takes a different output report, and
 * claiming it here would send a DualSense frame to a pad that cannot read it. */
inline bool PadTakesDirectOutput(std::uint16_t vendor_id, std::uint16_t product_id)
{
    return vendor_id == 0x054c && (product_id == 0x0ce6 || product_id == 0x0df2);
}

} // namespace akira::input

#endif // AKIRA_INPUT_PS_OUTPUT_HPP
