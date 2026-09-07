#ifndef AKIRA_IO_HAPTIC_MANAGER_HPP
#define AKIRA_IO_HAPTIC_MANAGER_HPP

#include <cstdint>
#include <chrono>
#include <atomic>

#include "input/ps_output.hpp"
#include "input/rumble_profile.hpp"
#include <chiaki/log.h>

class InputManager;
class ExtendedInputManager;

class HapticManager
{
public:
    HapticManager();
    ~HapticManager();

    // Disable copy
    HapticManager(const HapticManager&) = delete;
    HapticManager& operator=(const HapticManager&) = delete;

    void setLogger(ChiakiLog* log) { m_log = log; }

    void setRumble(uint8_t left, uint8_t right);
    void processHapticAudio(uint8_t* buf, size_t buf_size);
    void cleanup();

    int hapticBase = 400;

    /* Rumble goes to whichever pad the user picked, so it is resolved through
     * the input manager on every send rather than cached - the path can be
     * swapped mid-session. */
    void setInputManager(InputManager* input) { m_input = input; }

    /* The backend, for the pads we write to ourselves. Null on a console with
     * no sysmodule, which is the case every fallback here already handles. */
    void setExtendedInput(ExtendedInputManager* extended) { m_extended = extended; }

    void setRumbleStrength(float strength) { m_rumble_strength = strength; }

    /*
     * The console's own vibration setting, in the wire's numbering.
     *
     * Applied to what we generate: the haptic waveform we stream into a
     * DualSense's coils, and the motor amplitudes we hand to any pad that is
     * not attenuating for itself. A natively driven DualSense already has the
     * setting in every frame's intensity byte, so scaling its rumble here as
     * well would apply it twice.
     */
    void setConsoleVibration(uint8_t wire)
    {
        m_console_vibration.store(wire, std::memory_order_relaxed);
    }
    /*
     * Where this pad's rumble comes from, and the loudest it may be driven.
     *
     * Both are the pad's own profile rather than a console-wide setting, which
     * is what they were read from before - the profile carried them, and
     * nothing here asked for them.
     */
    void setRumbleSource(akira::input::RumbleSource source) { m_rumble_source = source; }
    void setRumbleCeiling(float ceiling) { m_ceiling = ceiling; }

    void setRumbleFreqLow(float freq) { m_freq_low = freq; }
    void setRumbleFreqHigh(float freq) { m_freq_high = freq; }
    void setEnvelopeDecay(float decay) { m_envelope_decay = decay; }
    void setEnvelopeAttack(float attack) { m_envelope_attack = attack; }

    bool isLocked() const { return m_haptic_lock; }

    /* Re-read the chosen pad's profile. Cheap, and called when the path
     * changes rather than every buffer. */
    void refreshProfile();

private:
    void setHapticRumble(uint8_t left, uint8_t right);
    void cleanupHaptic();

    /* Whether the chosen pad is driven at its own motors rather than through
     * HOS. Asked of the path every time rather than cached, because the path
     * can be swapped while a stream is running. */
    bool nativeRumble() const;

    /* True when it handled the buffer, so the caller stops rather than running
     * the Joy-Con path over the same bytes. */
    void emitMotorsFromWaveform(const int16_t* stereo, size_t frames);
    void emitMotorsLegacy(const uint8_t* buf, size_t buf_size);

    /*
     * The console's waveform to the pad's coils, unreduced.
     *
     * Everything else here turns the haptic stream into two motor amplitudes
     * because a Switch pad has nothing else to drive. A DualSense does: the
     * motors are for PS4 compatibility and the coils are what a PS5 game
     * actually plays through. This hands the waveform over as it arrived.
     */
    bool streamNativeHaptics(const int16_t* stereo, size_t frames);

    /*
     * How much of the previous level survives one buffer on the way down.
     *
     * Only the release is shaped - the rise is instant. Higher drags the tail
     * out, lower brings back the chatter this exists to remove.
     */
    static constexpr float kNativeRelease = 0.75f;

    /* Below this an ERM does not turn, so holding it only whines. */
    static constexpr float kNativeSilence = 2.0f / 255.0f;

    akira::input::RumbleProfile   m_profile;
    const void*                   m_profile_path = nullptr;
    float    m_native_env_left  = 0.0f;
    float    m_native_env_right = 0.0f;

    /* One frame's worth of stereo signed 8-bit samples, filled across however
     * many callbacks it takes before it goes out whole. */
    int8_t   m_haptic_block[akira::input::kDs5HapticBlockBytes * 2]{};
    size_t   m_haptic_fill    = 0;
    uint8_t  m_haptic_counter = 0;
    uint32_t m_haptic_sent    = 0;
    uint32_t m_haptic_skipped = 0;
    bool     m_haptic_stopped_logged = false;

    /*
     * The link, not the pad, is what this protects.
     *
     * An unthrottled stream takes the whole Bluetooth link down after about
     * seventeen seconds, and it takes adaptive triggers with it - they travel
     * the same link and are otherwise entirely healthy. Spacing the haptic
     * writes keeps a feature that works from being collateral damage to one
     * that does not yet.
     */
    std::chrono::steady_clock::time_point m_haptic_last_send{};
    bool m_haptic_last_send_valid = false;
    std::chrono::steady_clock::time_point m_haptic_first_send{};
    bool m_haptic_first_send_valid = false;
    bool     m_haptic_logged  = false;

    /*
     * A refusal now backs off rather than ending the session.
     *
     * With zero-retransmission set on the haptic report ids the stack pushes
     * back instead of dying, so a refusal is a transport that is momentarily
     * full rather than one that is gone. Standing down for a beat and trying
     * again is the response that fits; three refusals in a row is a transport
     * that really is gone, and then the motors keep the signal for good.
     */


    /* ~2s at the rate haptic buffers arrive. */
    static constexpr uint32_t kNativeHapticLogInterval = 60;
    uint32_t m_native_logged    = 0;


    ChiakiLog* m_log = nullptr;

    void emit(float left, float right);

    InputManager* m_input = nullptr;
    ExtendedInputManager* m_extended = nullptr;
    float m_rumble_strength = 1.0f;

    /* Strong until the console says otherwise, matching what the stream
     * connection assumes before its first intensity message. */
    std::atomic<uint8_t> m_console_vibration{
        (uint8_t)akira::input::Ds5EffectIntensity::Strong};
    float m_freq_low = 140.0f;
    float m_freq_high = 185.0f;
    float m_envelope = 0.0f;
    float m_envelope_decay = 0.85f;
    float m_envelope_attack = 0.60f;
    float m_ceiling = 1.0f;

    /* Counted on arrival, before any source gate drops them - the only way to
     * tell "the console stopped sending this" from "we stopped playing it". */
    std::uint32_t m_rumble_events = 0;
    std::uint32_t m_haptic_buffers = 0;
    std::uint32_t m_stream_logged = 0;

    akira::input::RumbleSource m_rumble_source = akira::input::RumbleSource::Derived;

    /* A pad with coils plays the waveform itself, so it is never asked where
     * its rumble should come from - the stored answer is whatever a config
     * written before the question narrowed happens to say. */
    akira::input::RumbleSource effectiveSource() const;




    bool m_haptic_lock = false;
    int m_haptic_val = 0;
    std::chrono::system_clock::time_point m_haptic_lock_time;
};

#endif // AKIRA_IO_HAPTIC_MANAGER_HPP
