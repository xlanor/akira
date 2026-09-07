#ifndef AKIRA_INPUT_EXTENDED_INPUT_MANAGER_HPP
#define AKIRA_INPUT_EXTENDED_INPUT_MANAGER_HPP

#include <atomic>
#include <cstdint>
#include <string>
#include <switch.h>

#include "akira_input/ipc.h"
#include "input/pad_output_state.hpp"
#include "input/ps_output.hpp"

/*
 * Optional analog L2/R2 source, served by the akira-input sysmodule.
 *
 * Everything here is best effort. A console without the sysmodule, without
 * MissionControl, or with a controller that has no analog triggers must behave
 * exactly as it did before this existed: initialisation fails quietly, the
 * digital values Akira already computes stand, and nothing is retried on the
 * input path.
 *
 * Failure is one-way on purpose. Once the backend has degraded we stop using
 * it for the rest of the session rather than reconnecting, because the only
 * place we could retry from is the render tick.
 */
class ExtendedInputManager
{
public:
    enum class Availability {
        NotInstalled,  /* no akira-input sysmodule on this console */
        Unsupported,   /* sysmodule present, but no usable MissionControl */
        VersionMismatch,
        Available,
        Failed,
    };

    ExtendedInputManager() = default;
    ~ExtendedInputManager();

    ExtendedInputManager(const ExtendedInputManager&) = delete;
    ExtendedInputManager& operator=(const ExtendedInputManager&) = delete;

    /* Answers "would this work?" without subscribing, so the settings screen
     * can report a definite result. */
    static Availability probe(uint32_t* out_mc_version);

    /* Human-readable form of a probe result, for the settings screen. */
    static const char* describe(Availability availability);

    /*
     * What the settings screen actually needs to answer: is a pad with analog
     * triggers selected, and is it the one you are holding?
     *
     * describe() alone reports whether the backend could work, which is not the
     * same question and reads as reassuring while nothing is happening. This
     * names the chosen controller and whether it is reporting, so
     * "MissionControl detected" cannot stand in for "your DualSense is in use".
     *
     * Opens its own short-lived session, so it is safe to call from the UI
     * thread whether or not a stream is running.
     */
    static std::string describeActiveDevice();

    /*
     * The device list without holding a session open.
     *
     * For screens that need to know which controllers exist but must not take
     * MissionControl's report path to find out - subscribing is what enables
     * redirection, and opening Settings should not do that. The entries are
     * whatever was seen last time something did subscribe, which is why the
     * screen has to say "press a button on a sleeping pad".
     */
    static bool listDevicesOnce(AkiraInputDeviceList* out);

    /*
     * One output report, without holding a session open.
     *
     * So the settings screen can preview through the same route a stream uses.
     * A preview that went through HOS instead would be testing MissionControl's
     * translation rather than the setting being previewed - which is exactly
     * how a knob comes to feel like it does nothing.
     */
    static bool writeOutputReportOnce(const uint8_t* address, const uint8_t* data, uint16_t length);

    /* Whether a Bluetooth audio channel to this pad can be opened at all -
     * the only route to a DualSense's haptic actuators, which the HID output
     * report cannot reach. */
    static void setDirectStreamAllowed(bool allowed);
    static bool directStreamAllowed();

    /* Whether the backend refused the claim - see m_output_refused. Read by
     * ResolvePadDriver, which is the only thing that should act on it. */
    /*
     * Re-resolve the gates for one pad. Public because the settings page has to
     * be able to say "this changed" without reaching past ResolvePadDriver into
     * the gate itself, which is what it used to do.
     */
    /* What akira is allowed to do with the current pad, resolved in one place -
     * see pad_output_state.hpp. Everything that used to recombine the gates
     * asks this instead. */
    akira::input::PadOutputState outputState() const;

    static void refreshDirectGates(uint16_t vendor_id, uint16_t product_id,
                                   const uint8_t* address);

    static bool outputOwnershipRefused()
    {
        return s_output_refused.load(std::memory_order_acquire);
    }

    /*
     * Whether the backend's native-output switch is on, asked of the backend.
     *
     * An IPC round trip, so this is for screens rather than for the pump - the
     * pump learns the same thing for free from its ownership call. Defaults to
     * true when there is no backend to ask, because then nothing is being
     * withheld and the honest answer is "not this".
     */
    static bool backendDirectOutputEnabled();

    /* Cached answer to the above, so a screen or a resolver can ask without an
     * IPC round trip. Refreshed once a second by the pump while refused, which
     * is the only time it can change under us without our doing it. */
    bool backendReleased() const
    {
        return s_backend_released.load(std::memory_order_acquire);
    }
    static void setDirectHapticsAllowed(bool allowed);
    static bool directHapticsAllowed();

    static bool probeAudio(const uint8_t* address, AkiraInputAudioProbe* out);
    static bool readRawReportOnce(AkiraInputRawReport* out);
    static std::string describeDirectState();

    /*
     * One direct write, issued synchronously, with the sysmodule log committed
     * either side of it. Answers whether the operation itself is what freezes
     * the console or whether it was only ever the rate.
     */
    static bool probeDirectWrite(const uint8_t* address, uint8_t method,
                                 const uint8_t* frame, uint16_t length,
                                 uint32_t* out_rc, uint32_t* out_init_rc);

private:
    /*
     * Resolved from the pad's own profile when its address is latched, then
     * cached.
     *
     * These were two global switches read once at startup, so every pad shared
     * one answer. They are per category now, which means a lookup - and the
     * lookup walks a map the settings screen can rewrite, from a thread that
     * must not block. Refreshing on the latch keeps the map off the hot path:
     * the poll thread only ever reads an atomic.
     */
    static void loadDirectGatesOnce();
    static std::atomic<bool> s_direct_stream_allowed;
    static std::atomic<bool> s_direct_haptics_allowed;


    /*
     * A ceiling on direct writes for the lifetime of this process.
     *
     * Writes consume something in the bluetooth sysmodule that is only
     * returned by a reboot, and running it out does not merely stop haptics -
     * it takes input down for the whole console, rail Joy-Cons included, and
     * costs a hard reset. The budget has been measured at roughly three
     * hundred. Stopping short of it is the difference between a feature that
     * degrades and one that bricks the machine until it is power-cycled.
     */
    bool                     m_direct_gate_logged = false;

public:

    /* Never throws, never returns an error, never blocks for long. Returns
     * false when analog triggers are simply not available here. */
    bool initializeOptional();
    void shutdown();

    Availability availability() const { return m_availability; }
    uint32_t missionControlVersion() const { return m_mc_version; }

    /* True once, the first time the backend degrades mid-session, so the UI can
     * show a single toast and not one per frame. */
    bool consumeDegradedNotice();

    /* Hot path. Reads one relaxed atomic - no IPC, no lock, no allocation. */
    bool hasFreshAnalogState() const;
    uint8_t l2() const;
    uint8_t r2() const;

    /*
     * The most recent raw report, for a pad we read natively rather than
     * through MissionControl's Switch translation.
     *
     * Too large for the packed-word trick the trigger snapshot uses, so it goes
     * through a seqlock: the poll thread marks the sequence odd while it
     * copies, and a reader that sees an odd or changed sequence retries. The
     * render tick must never block, and a torn report is worse than a missed
     * one - half of last frame's buttons and half of this frame's is input
     * nobody performed.
     *
     * False when nothing has been published, when the report is stale, or when
     * the pad it came from is not the one asked for.
     */
    bool readRawReport(AkiraInputRawReport* out) const;

    /*
     * Which pads the backend can see, with the vid/pid that decides what kind
     * of path each one gets.
     *
     * Uses the live session, so it is only meaningful once something has
     * subscribed - nothing reports before then, and the entries are whatever
     * was seen last time one did. Not for the hot path.
     */
    bool listDevices(AkiraInputDeviceList* out) const;

    /* Whether raw reports are being fetched at all. Off by default, because
     * every pad that does not need native reading should not pay for them. */
    void setRawWanted(bool wanted);

    /*
     * Drive the pad's own output report rather than asking HOS to vibrate it.
     *
     * The translated route loses almost everything: MissionControl folds the
     * two Switch frequency bands onto two ERM motors and discards frequency,
     * because an ERM has no resonance to tune. Writing the pad's own report
     * puts back what the console actually sent.
     *
     * Enabled by the path that knows the pad takes it - a Switch controller
     * must keep using the vibration API, and a console with no backend at all
     * never gets here. Requires raw reports, which is where the address comes
     * from.
     */
    void setDirectOutput(bool enabled);

    /*
     * Take the pad's output away from MissionControl for the length of a
     * stream, and give it back afterwards.
     *
     * The sysmodule implements this, but nothing was ever asking: only the
     * probe called it. Without it MissionControl keeps writing to the same pad
     * and, more importantly, the sysmodule never widens the Bluetooth link -
     * so every haptic frame is silently dropped by the send path and reports
     * success, which is exactly what a stream with no rumble and no errors
     * looked like.
     */
    /* The pad's address, from the device list rather than from a report.
     * ListDevices needs no claim, and a claim is what a report needs. */
    void resolveDirectAddress();

    void ensureOutputOwnership(bool want);

    /* Paint the lightbar with whichever colour currently wins - see
     * resolveLightbar. Change-gated, so calling it every tick is free. */
    void paintLightbar();

    /* Which colour the pad should be showing, and whether it should be showing
     * one of ours at all. */
    bool resolveLightbar(uint8_t* rgb) const;

    /*
     * The colour the game asked for, which outranks the profile's while a
     * stream is running.
     *
     * A profile colour says who is driving the pad; a game colour is the game
     * talking to the player, and for as long as there is a game that is the
     * more interesting of the two. The profile's colour is what the pad goes
     * back to when the stream ends.
     */
    void setGameLightbar(uint8_t red, uint8_t green, uint8_t blue);
    void clearGameLightbar();

    /*
     * How hard the console's own settings allow the pad to play.
     *
     * Not a preference of akira's and not one it can override: the user set
     * these on the console, under Accessories, and they are meant to hold
     * wherever their pad is being driven from. Applied by the pad itself
     * through the intensity byte rather than by scaling anything here.
     */
    void setConsoleIntensity(akira::input::Ds5EffectIntensity vibration,
                             akira::input::Ds5EffectIntensity trigger);
    akira::input::Ds5EffectIntensity consoleVibrationIntensity() const;
    akira::input::Ds5EffectIntensity consoleTriggerIntensity() const;

    /* Whose profile to read when painting. Recorded by refreshDirectGates,
     * which is the one place that already knows, and static because it is. */
    static std::atomic<uint32_t> s_direct_vid_pid;

    /* See backendReleased. */
    static std::atomic<bool> s_backend_released;

    /* Hot path, called from the render tick. Stores; the poll thread sends,
     * because two threads on one IPC session is a torn request. */
    void setDirectRumble(uint8_t left, uint8_t right);

    /*
     * Adaptive trigger resistance, as the console composed it.
     *
     * Eleven bytes per trigger - an effect type and ten parameters - passed
     * through untouched. This has no Switch equivalent at all, so it exists
     * only on this route.
     */
    void setDirectTriggerEffects(uint8_t left_type, const uint8_t* left_params,
                                 uint8_t right_type, const uint8_t* right_params);

    /*
     * One frame of the console's haptic waveform, straight to the pad's coils.
     *
     * Sent from the caller's thread rather than queued for the poll thread, and
     * that is deliberate: this is a continuous stream at the coils' own rate,
     * so a frame delayed to the next 8ms tick is a gap you would feel. Writes
     * are serialised against the poll thread's own dispatches by a mutex - one
     * IPC session cannot carry two requests at once.
     */
    bool sendDirectHaptics(uint8_t* frame, uint16_t length);

    /*
     * Whether frames are actually reaching the pad - not merely whether a pad
     * that could take them is connected.
     *
     * The difference is the whole bug: submitting a report hands it to the
     * sysmodule's writer thread and returns, so "ready" said nothing about
     * whether btdrv accepted a single one. A haptic path that claims the audio
     * on readiness alone suppresses the motors and then plays nothing.
     */
    bool directHapticsReady() const;

    /* Next value for the four-bit report sequence, shared across every report
     * type so the pad sees one monotonic stream rather than two. */


private:
    static void pollThreadFunc(void* arg);
    void poll();
    void pumpDirectOutput();

    /*
     * Ask the backend whether it will cede a pad again.
     *
     * Once refused, akira demotes to the generic path, and tearing down the
     * native path clears the flag that made it want the claim at all - so
     * nothing was left that would ever ask again, and the refusal could not be
     * undone without restarting the stream. The switch coming back on has to be
     * heard by something that does not depend on wanting the pad.
     */
    void reconsiderBackendRefusal();
    uint32_t m_refusal_recheck = 0;
    void pumpDirectState(uint32_t now);
    bool writeOutputReport(const uint8_t* address, const uint8_t* data, uint16_t length);
    bool writeDirectFrame(uint8_t* frame, uint16_t length);
    bool directWriteFailed();
    void logStatus(const char* when);
    void logRawReport(const char* when);

    /* Every ~5s at the poll rate. Frequent enough to catch a gap opening,
     * rare enough not to bury the log during a long session. */
    static constexpr uint32_t kStatusLogInterval = 600;

    /*
     * Packed snapshot published by the poll thread and read by the render tick.
     *
     * A single u64 rather than a struct plus a lock: the reader must never
     * block, and packing removes the possibility of a torn read between the
     * values and the timestamp they belong to.
     *
     *   [ 7:0]  l2
     *   [15:8]  r2
     *   [16]    valid
     *   [63:32] publish time, milliseconds
     */
    static constexpr uint64_t kValidBit = 1ull << 16;

    static uint64_t pack(uint8_t l2, uint8_t r2, bool valid, uint32_t ms);
    static uint32_t nowMs();

    /* Slightly tighter than the sysmodule's own staleness policy, so a backend
     * that stops publishing is caught here too. */
    static constexpr uint32_t kMaxAgeMs = 200;

    /* 120 Hz - twice the render tick, so the value the tick reads is at most
     * one backend frame old without polling harder than reports arrive. */
    static constexpr uint64_t kPollIntervalNs = 8'333'333ull;

    /*
     * Floor between direct writes, so a haptics-driven amplitude that moves
     * every frame cannot turn into a hundred Bluetooth writes a second. The
     * last value always lands: sending is gated on differing from what was
     * sent, not on having changed since the previous tick.
     *
     * Thirty a second rather than sixty, because sustained writes are what
     * takes this console down. A probe doing forty-seven a second was refused
     * after about two hundred of them and the Bluetooth process went with it,
     * and that is well inside the length of a stream. Rumble does not need
     * sixty: an ERM cannot follow amplitude changes at that rate anyway, so
     * the halving costs nothing that can be felt and buys most of the margin
     * back.
     */
    static constexpr uint32_t kDirectWriteIntervalMs = 33;

    /* Consecutive refusals before the address is dropped. Generous, because a
     * single failure in the middle of a stream is not evidence of anything. */
    static constexpr uint32_t kDirectFailureLimit = 16;

    /* One session, so every dispatch on it takes this - the poll thread's
     * included. A torn request is not a dropped frame, it is a corrupt one. */
    mutable Mutex         m_srv_lock{};

    /* Four bits, shared across every report type this pad takes, exactly as
     * the pad's own host does it. */
    std::atomic<uint32_t> m_direct_seq{0};

    std::atomic<bool>     m_direct_wanted{false};
    /* left << 8 | right, so the pair is published in one store. */
    std::atomic<uint16_t> m_direct_rumble{0};

    /*
     * Both triggers' settings, and a sequence the poll thread compares against.
     *
     * Twenty-two bytes cannot be published atomically, so the writer bumps the
     * sequence after storing and the sender only reads a value it saw settle.
     * Effects change at human pace - entering a weapon, leaving cover - so a
     * reader that skips one contested update picks it up on the next tick with
     * nothing lost.
     */
    std::atomic<uint32_t> m_direct_trigger_seq{0};
    uint8_t               m_direct_trigger_left[11]{};
    uint8_t               m_direct_trigger_right[11]{};

    /* Poll thread only - no other thread reads these. */
    uint8_t  m_direct_addr[6]{};
    bool     m_direct_addr_valid = false;
    bool     m_owns_output       = false;

    /*
     * The backend would not cede the pad.
     *
     * Either the overlay toggle is off, or this MissionControl has no ownership
     * extension to answer with. Both mean the same thing to us: MissionControl
     * is still writing to the pad, so we must not. Kept as the answer to the
     * ownership call the pump already makes, rather than a poll of its own.
     */
    /* Static because there is one backend, and the gates beside it already are.
     * An instance-local copy meant the settings page could not see what the
     * pump had been told. */
    static std::atomic<bool> s_output_refused;

    /* When to ask again for a claim we believe we already hold - see
     * ensureOutputOwnership. */
    uint32_t m_owns_recheck = 0;
    uint16_t m_direct_sent       = 0;
    bool     m_direct_sent_valid = false;
    uint32_t m_direct_sent_ms    = 0;
    uint32_t m_direct_failures   = 0;
    bool     m_direct_logged     = false;
    /* What the pad was last told, so switching off can be told from having
     * nothing to say - the two want opposite things and look identical if only
     * a dirty flag is kept. */
    /*
     * Strong until the console says otherwise, matching what the stream
     * connection assumes before the first intensity message arrives. Starting
     * at Off would silence a pad on every session up to that point.
     */
    std::atomic<uint8_t> m_console_vibration{
        (uint8_t)akira::input::Ds5EffectIntensity::Strong};
    std::atomic<uint8_t> m_console_trigger{
        (uint8_t)akira::input::Ds5EffectIntensity::Strong};

    /* 0 when the game has not asked for one; otherwise the marker bit and the
     * three colour bytes, so that reading it is a single load. */
    static constexpr uint32_t kGameLightbarSet = 0x01000000u;
    std::atomic<uint32_t> m_game_lightbar{0};

    /*
     * What the pad is already showing, so a colour that has not moved costs no
     * write. MissionControl responds to every lightbar write by pushing its own
     * state to the npad, and repainting per frame is how that becomes churn.
     */
    uint32_t m_lightbar_painted       = 0;
    bool     m_lightbar_painted_valid = false;
    uint32_t m_lightbar_painted_ms    = 0;

    /*
     * A ceiling on how often the LED may be written, separate from whether the
     * colour has moved.
     *
     * Change detection alone is not enough: a PS5 home screen animates its
     * lightbar, and a capture of one shows 241 colour events in 16 seconds,
     * about half of them genuinely different. Painting each one is seven
     * milliseconds of the console's shared Bluetooth thread per write, and
     * MissionControl pushes its own npad state in response to every one.
     *
     * Four a second is far below anything an eye reads as stepping, and the
     * pump re-resolves the colour every tick, so what eventually lands is the
     * current colour rather than a stale one.
     */
    static constexpr uint32_t kLightbarIntervalMs = 250;

    uint8_t  m_direct_intensity_last = akira::input::kDs5IntensityFull;
    uint8_t  m_direct_trigger_last[22]{};
    bool     m_direct_trigger_sent    = false;
    uint32_t m_direct_trigger_sent_ms = 0;
    bool     m_direct_trigger_logged  = false;

    /*
     * Whether the backend is actually getting reports out.
     *
     * Published by the poll thread from the sysmodule's own counters, because
     * submitting a report only queues it - the caller's return value says
     * nothing about whether btdrv accepted one. Without this, a haptic path
     * suppresses the motors on the strength of being enabled and then plays
     * silence.
     */
    std::atomic<bool> m_haptics_landing{false};
    uint64_t m_haptics_written_seen = 0;
    uint64_t m_haptics_failed_seen  = 0;

    /* Reports the backend had read last time we looked. The line reports the
     * delta: the total only grows and cannot say whether anything is arriving
     * now. */
    uint64_t m_reports_seen = 0;

    /* Even and unchanged across the copy means the reader saw a whole report. */
    mutable std::atomic<uint32_t> m_raw_seq{0};
    AkiraInputRawReport           m_raw{};
    std::atomic<uint32_t>         m_raw_published_ms{0};
    std::atomic<bool>             m_raw_wanted{false};

    std::atomic<uint64_t> m_snapshot{0};
    std::atomic<bool>     m_running{false};
    std::atomic<bool>     m_degraded_notice{false};

    Service      m_srv{};
    Thread       m_thread{};
    bool         m_thread_started = false;
    bool         m_subscribed = false;
    Availability m_availability = Availability::NotInstalled;
    uint32_t     m_mc_version = 0;
};

#endif // AKIRA_INPUT_EXTENDED_INPUT_MANAGER_HPP
