#include "input/extended_input_manager.hpp"

#include <borealis.hpp>

#include <cstring>
#include <format>
#include <string>

#include <mutex>

#include "core/settings_manager.hpp"
#include "core/thread_affinity.h"
#include "input/pad_path.hpp"
#include "input/ps_output.hpp"

namespace {

/* Existence check that does not block. Atmosphere's sm defers on a name nobody
 * has registered rather than returning an error, so asking for the service
 * directly would hang on a console without the sysmodule. Registering the name
 * ourselves succeeds only when it is free, which is exactly the negative
 * answer we want, and we hand it straight back. */
bool serviceIsRunning(const char* name)
{
    Handle handle;
    const SmServiceName encoded = smEncodeName(name);
    if (R_SUCCEEDED(smRegisterService(&handle, encoded, false, 1))) {
        svcCloseHandle(handle);
        smUnregisterService(encoded);
        return false;
    }
    return true;
}

} // namespace

ExtendedInputManager::Availability ExtendedInputManager::probe(uint32_t* out_mc_version)
{
    if (out_mc_version) {
        *out_mc_version = 0;
    }

    if (!serviceIsRunning(AKIRA_INPUT_IPC_SERVICE_NAME)) {
        return Availability::NotInstalled;
    }

    Service srv{};
    if (R_FAILED(smGetService(&srv, AKIRA_INPUT_IPC_SERVICE_NAME))) {
        return Availability::NotInstalled;
    }

    uint32_t api = 0;
    Result rc = serviceDispatchOut(&srv, AkiraInputCmd_GetApiVersion, api);
    if (R_FAILED(rc) || api != AKIRA_INPUT_IPC_API_VERSION) {
        serviceClose(&srv);
        return Availability::VersionMismatch;
    }

    AkiraInputStatus status{};
    rc = serviceDispatchOut(&srv, AkiraInputCmd_GetStatus, status);
    serviceClose(&srv);

    if (R_FAILED(rc)) {
        return Availability::Failed;
    }
    if (out_mc_version) {
        *out_mc_version = status.mc_version;
    }

    if (status.backend_state == AkiraInputBackend_Unavailable) {
        return Availability::Unsupported;
    }
    if (status.backend_state == AkiraInputBackend_Failed) {
        return Availability::Failed;
    }
    return Availability::Available;
}

bool ExtendedInputManager::listDevicesOnce(AkiraInputDeviceList* out)
{
    if (out == nullptr || !serviceIsRunning(AKIRA_INPUT_IPC_SERVICE_NAME))
        return false;

    Service srv{};
    if (R_FAILED(smGetService(&srv, AKIRA_INPUT_IPC_SERVICE_NAME)))
        return false;

    uint32_t api = 0;
    if (R_FAILED(serviceDispatchOut(&srv, AkiraInputCmd_GetApiVersion, api)) ||
        api != AKIRA_INPUT_IPC_API_VERSION) {
        serviceClose(&srv);
        return false;
    }

    const Result rc = serviceDispatchOut(&srv, AkiraInputCmd_ListDevices, *out);
    serviceClose(&srv);

    return R_SUCCEEDED(rc);
}

bool ExtendedInputManager::writeOutputReportOnce(const uint8_t* address, const uint8_t* data, uint16_t length)
{
    if (address == nullptr || data == nullptr || length == 0 || length > AKIRA_INPUT_OUTPUT_MAX)
        return false;
    if (!serviceIsRunning(AKIRA_INPUT_IPC_SERVICE_NAME))
        return false;

    Service srv{};
    if (R_FAILED(smGetService(&srv, AKIRA_INPUT_IPC_SERVICE_NAME)))
        return false;

    uint32_t api = 0;
    if (R_FAILED(serviceDispatchOut(&srv, AkiraInputCmd_GetApiVersion, api)) ||
        api != AKIRA_INPUT_IPC_API_VERSION) {
        serviceClose(&srv);
        return false;
    }

    AkiraInputPadRef which{};
    std::memcpy(which.bt_addr, address, sizeof(which.bt_addr));

    const Result rc = serviceDispatchIn(&srv, AkiraInputCmd_WriteOutputReport, which,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In },
        .buffers = { { data, length } },
    );
    serviceClose(&srv);

    return R_SUCCEEDED(rc);
}

bool ExtendedInputManager::probeAudio(const uint8_t* address, AkiraInputAudioProbe* out)
{
    if (address == nullptr || out == nullptr || !serviceIsRunning(AKIRA_INPUT_IPC_SERVICE_NAME))
        return false;

    Service srv{};
    if (R_FAILED(smGetService(&srv, AKIRA_INPUT_IPC_SERVICE_NAME)))
        return false;

    uint32_t api = 0;
    if (R_FAILED(serviceDispatchOut(&srv, AkiraInputCmd_GetApiVersion, api)) ||
        api != AKIRA_INPUT_IPC_API_VERSION) {
        serviceClose(&srv);
        return false;
    }

    AkiraInputPadRef which{};
    std::memcpy(which.bt_addr, address, sizeof(which.bt_addr));

    const Result rc = serviceDispatchInOut(&srv, AkiraInputCmd_ProbeAudio, which, *out);
    serviceClose(&srv);

    return R_SUCCEEDED(rc);
}

bool ExtendedInputManager::probeDirectWrite(const uint8_t* address, uint8_t method,
                                           const uint8_t* frame, uint16_t length,
                                           uint32_t* out_rc, uint32_t* out_init_rc)
{
    if (address == nullptr || frame == nullptr || length == 0 || out_rc == nullptr)
        return false;
    if (!serviceIsRunning(AKIRA_INPUT_IPC_SERVICE_NAME))
        return false;

    Service srv{};
    if (R_FAILED(smGetService(&srv, AKIRA_INPUT_IPC_SERVICE_NAME)))
        return false;

    uint32_t api = 0;
    if (R_FAILED(serviceDispatchOut(&srv, AkiraInputCmd_GetApiVersion, api)) ||
        api != AKIRA_INPUT_IPC_API_VERSION) {
        serviceClose(&srv);
        return false;
    }

    AkiraInputDirectProbe probe{};
    std::memcpy(probe.bt_addr, address, sizeof(probe.bt_addr));
    probe.method = method;

    AkiraInputDirectResult result{};
    const Result rc = serviceDispatchInOut(&srv, AkiraInputCmd_ProbeDirectWrite, probe, result,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In },
        .buffers = { { frame, length } },
    );
    serviceClose(&srv);

    if (R_FAILED(rc))
        return false;

    *out_rc = result.rc;
    if (out_init_rc != nullptr)
        *out_init_rc = result.init_rc;
    return true;
}

/*
 * The pad's own report, fetched once, without subscribing.
 *
 * Settings deliberately does not turn redirection on, so nothing is publishing
 * live. The sysmodule keeps the last report it saw per device though, and for
 * the pad's audio state - headphones attached, haptic filter engaged - the last
 * one is what we want anyway: those change when hardware is plugged in, not
 * between packets.
 */
bool ExtendedInputManager::readRawReportOnce(AkiraInputRawReport* out)
{
    if (out == nullptr)
        return false;
    if (!serviceIsRunning(AKIRA_INPUT_IPC_SERVICE_NAME))
        return false;

    Service srv{};
    if (R_FAILED(smGetService(&srv, AKIRA_INPUT_IPC_SERVICE_NAME)))
        return false;

    uint32_t api = 0;
    if (R_FAILED(serviceDispatchOut(&srv, AkiraInputCmd_GetApiVersion, api)) ||
        api != AKIRA_INPUT_IPC_API_VERSION) {
        serviceClose(&srv);
        return false;
    }

    AkiraInputDeviceList devices{};
    if (R_FAILED(serviceDispatchOut(&srv, AkiraInputCmd_ListDevices, devices)) ||
        devices.count == 0) {
        serviceClose(&srv);
        return false;
    }

    AkiraInputTriggerQuery query{};
    std::memcpy(query.bt_addr, devices.devices[0].bt_addr, sizeof(query.bt_addr));

    const Result rc = serviceDispatchInOut(&srv, AkiraInputCmd_GetRawReport, query, *out);
    serviceClose(&srv);

    return R_SUCCEEDED(rc);
}

/*
 * Why direct output is or is not going to happen, in one line.
 *
 * In-stream writes have now silently produced nothing twice, and both times I
 * guessed at which gate was shut instead of looking. There are four, they are
 * all cheap to read, and none of them is visible from outside. So: read them.
 */
std::string ExtendedInputManager::describeDirectState()
{
    /* Only the two that survive outside a stream. The rest live on the
     * manager, which does not exist until one starts - which is itself worth
     * knowing, and is why the others are logged from the pump instead. */
    return std::string(directStreamAllowed()  ? "stream toggle: ON"  : "stream toggle: OFF")
         + (directHapticsAllowed() ? ",  haptics: on" : ",  haptics: off");
}

bool ExtendedInputManager::backendDirectOutputEnabled()
{
    if (!serviceIsRunning(AKIRA_INPUT_IPC_SERVICE_NAME))
        return true;

    Service srv{};
    if (R_FAILED(smGetService(&srv, AKIRA_INPUT_IPC_SERVICE_NAME)))
        return true;

    u8 on = 1;
    const Result rc = serviceDispatchOut(&srv, AkiraInputCmd_GetDirectOutput, on);
    serviceClose(&srv);

    /* A backend too old to answer is one with no switch to be off. */
    return R_FAILED(rc) || on != 0;
}

std::string ExtendedInputManager::describeActiveDevice()
{
    if (!serviceIsRunning(AKIRA_INPUT_IPC_SERVICE_NAME)) {
        return "akira-input is not running";
    }

    Service srv{};
    if (R_FAILED(smGetService(&srv, AKIRA_INPUT_IPC_SERVICE_NAME))) {
        return "akira-input is not running";
    }

    uint32_t api = 0;
    if (R_FAILED(serviceDispatchOut(&srv, AkiraInputCmd_GetApiVersion, api)) ||
        api != AKIRA_INPUT_IPC_API_VERSION) {
        serviceClose(&srv);
        return "version mismatch - update akira-input";
    }

    /*
     * Whether redirection is currently on decides what the device list means.
     *
     * Nothing reports until a client subscribes, and this screen deliberately
     * does not - opening settings must not take MissionControl's report path.
     * So the entries are whatever was seen last time something did subscribe,
     * with timestamps minutes old, and the "reporting" flag on every one of
     * them is stale rather than false. Saying "asleep" there is a confident
     * claim about a pad we simply are not listening to.
     */
    AkiraInputStatus status{};
    const bool live = R_SUCCEEDED(serviceDispatchOut(&srv, AkiraInputCmd_GetStatus, status)) &&
                      status.backend_state == AkiraInputBackend_Active;

    AkiraInputDeviceList devices{};
    const Result rc = serviceDispatchOut(&srv, AkiraInputCmd_ListDevices, devices);
    serviceClose(&srv);

    if (R_FAILED(rc)) {
        return "could not read controllers";
    }
    if (devices.count == 0) {
        /* Nothing reports until something subscribes, so an idle console
         * genuinely has nothing to list rather than having lost the pad. */
        return live ? "no controller reporting"
                    : "no controllers seen yet - start a stream or open the overlay";
    }

    for (uint8_t i = 0; i < devices.count; i++) {
        const AkiraInputDeviceInfo& d = devices.devices[i];
        if (!(d.flags & AkiraInputDevice_Active)) {
            continue;
        }
        const char* liveness = "";
        if (live) {
            liveness = (d.flags & AkiraInputDevice_Reporting) ? ", reporting" : ", asleep";
        }
        return std::format("{:04x}:{:04x}{}", d.vendor_id, d.product_id, liveness);
    }

    /* No explicit choice: whichever analog pad spoke last wins, which is worth
     * saying out loud rather than presenting as a decision. */
    return std::format("{} controller(s), none selected - automatic", devices.count);
}

const char* ExtendedInputManager::describe(Availability availability)
{
    switch (availability) {
        case Availability::NotInstalled:     return "akira/settings/analog_triggers_not_installed";
        case Availability::Unsupported:      return "akira/settings/analog_triggers_unsupported";
        case Availability::VersionMismatch:  return "akira/settings/analog_triggers_mismatch";
        case Availability::Available:        return "akira/settings/analog_triggers_available";
        case Availability::Failed:           return "akira/settings/analog_triggers_failed";
    }
    return "akira/settings/analog_triggers_failed";
}

ExtendedInputManager::~ExtendedInputManager()
{
    shutdown();
}

uint64_t ExtendedInputManager::pack(uint8_t l2, uint8_t r2, bool valid, uint32_t ms)
{
    return static_cast<uint64_t>(l2)
         | (static_cast<uint64_t>(r2) << 8)
         | (valid ? kValidBit : 0)
         | (static_cast<uint64_t>(ms) << 32);
}

uint32_t ExtendedInputManager::nowMs()
{
    return static_cast<uint32_t>(armTicksToNs(armGetSystemTick()) / 1'000'000ull);
}

bool ExtendedInputManager::initializeOptional()
{
    /*
     * No setting gates this.
     *
     * Installing the sysmodule is the opt-in - a separate download, files on
     * the SD card, a reboot. Asking again inside Akira is the same question
     * twice, and it strands anyone who installs akira-input and cannot work out
     * why nothing happens. Turning it off is stopping or removing the
     * sysmodule, which actually stops the process rather than merely declining
     * to subscribe.
     *
     * The check below is one sm register/unregister pair - microseconds, once
     * per stream, and it cannot block on a console without the service.
     */
    if (!serviceIsRunning(AKIRA_INPUT_IPC_SERVICE_NAME)) {
        m_availability = Availability::NotInstalled;
        brls::Logger::info("analog triggers: akira-input not installed");
        return false;
    }

    Result rc = smGetService(&m_srv, AKIRA_INPUT_IPC_SERVICE_NAME);
    if (R_FAILED(rc)) {
        m_availability = Availability::NotInstalled;
        brls::Logger::info("analog triggers: could not open akira-input (0x{:x})", rc);
        return false;
    }

    /* Refuse on a version mismatch rather than warning. The overlay repo
     * vendors this header, so a stale peer reading a changed struct would be
     * silent corruption, not a cosmetic problem. */
    uint32_t api = 0;
    rc = serviceDispatchOut(&m_srv, AkiraInputCmd_GetApiVersion, api);
    if (R_FAILED(rc) || api != AKIRA_INPUT_IPC_API_VERSION) {
        brls::Logger::warning("analog triggers: api {} != {} (rc=0x{:x})",
                              api, AKIRA_INPUT_IPC_API_VERSION, rc);
        m_availability = Availability::VersionMismatch;
        serviceClose(&m_srv);
        return false;
    }

    AkiraInputStatus status{};
    rc = serviceDispatchOut(&m_srv, AkiraInputCmd_GetStatus, status);
    if (R_FAILED(rc)) {
        m_availability = Availability::Failed;
        serviceClose(&m_srv);
        return false;
    }

    m_mc_version = status.mc_version;

    if (status.backend_state == AkiraInputBackend_Unavailable
        || status.backend_state == AkiraInputBackend_Failed) {
        m_availability = Availability::Unsupported;
        brls::Logger::info("analog triggers: backend unavailable (state {}, last 0x{:x})",
                           status.backend_state, static_cast<uint32_t>(status.last_error));
        serviceClose(&m_srv);
        return false;
    }

    /* Subscribing is what makes the sysmodule take MissionControl's report
     * redirection. Holding it for no longer than we are actually streaming is
     * the whole point of the subscription model. */
    rc = serviceDispatch(&m_srv, AkiraInputCmd_Subscribe);
    if (R_FAILED(rc)) {
        m_availability = Availability::Unsupported;
        brls::Logger::info("analog triggers: subscribe refused (0x{:x})", rc);
        serviceClose(&m_srv);
        return false;
    }
    m_subscribed = true;

    m_running.store(true, std::memory_order_release);
    rc = threadCreate(&m_thread, pollThreadFunc, this, nullptr, 0x4000, 0x2C, -2);
    if (R_FAILED(rc) || R_FAILED(threadStart(&m_thread))) {
        brls::Logger::warning("analog triggers: poll thread failed (0x{:x})", rc);
        if (R_SUCCEEDED(rc)) {
            threadClose(&m_thread);
        }
        m_running.store(false, std::memory_order_release);
        m_availability = Availability::Failed;
        shutdown();
        return false;
    }
    m_thread_started = true;

    m_availability = Availability::Available;
    brls::Logger::info("analog triggers: available (mc 0x{:06x})", m_mc_version);
    return true;
}

void ExtendedInputManager::shutdown()
{
    m_running.store(false, std::memory_order_release);

    if (m_thread_started) {
        threadWaitForExit(&m_thread);
        threadClose(&m_thread);
        m_thread_started = false;
    }

    if (serviceIsActive(&m_srv)) {
        /*
         * Release the pad before the session closes. Both of these persist
         * until something says otherwise, and after this nothing will - the
         * controller would keep buzzing, and keep its triggers stiff, with the
         * stream long gone. Safe to send from here because the poll thread has
         * already been joined.
         */
        if (m_direct_addr_valid) {
            uint8_t frame[akira::input::kDs5BluetoothFrameBytes];

            if (m_direct_sent_valid && m_direct_sent != 0) {
                const size_t len = akira::input::BuildDs5RumbleFrame(0, 0, 0, frame, sizeof(frame));
                if (len > 0)
                    (void)writeDirectFrame(frame, (uint16_t)len);
            }

            /*
             * Hand the coils back unconditionally.
             *
             * HAPTICS_SELECT latches, so a session that streamed audio leaves
             * the voice coils parked for whatever runs next - and quitting the
             * app is exactly when nothing is coming to unpark them. The zero
             * case above only covers motors; this covers the mode.
             */
            {
                const size_t len = akira::input::BuildDs5HapticsRestoreFrame(
                    0, frame, sizeof(frame));
                if (len > 0)
                    (void)writeDirectFrame(frame, (uint16_t)len);
            }

            /* Only when something non-null was actually sent - a null effect
             * written to a pad that never had one is a frame for nothing. */
            static const uint8_t kNull[sizeof(m_direct_trigger_last)] = {};
            if (m_direct_trigger_sent &&
                std::memcmp(m_direct_trigger_last, kNull, sizeof(kNull)) != 0) {
                const size_t len = akira::input::BuildDs5TriggerFrame(0, 0, kNull, 0, kNull,
                                                                      frame, sizeof(frame));
                if (len > 0)
                    (void)writeDirectFrame(frame, (uint16_t)len);
            }
        }

        /* Hand the pad back before the session closes. Leaving ownership held
         * would keep MissionControl locked out and the pad's link out of power
         * saving until it disconnects. */
        ensureOutputOwnership(false);

        /* One last line before the subscription drops, so a run that ended
         * badly still leaves the counters behind. */
        if (m_subscribed) {
            logStatus("shutdown");
        }
        if (m_subscribed) {
            /* Best effort. If this does not land the sysmodule drops the
             * subscription when the session closes below, which is the case it
             * was built to handle. */
            (void)serviceDispatch(&m_srv, AkiraInputCmd_Unsubscribe);
            m_subscribed = false;
        }
        serviceClose(&m_srv);
    }

    m_snapshot.store(0, std::memory_order_relaxed);

    m_direct_wanted.store(false, std::memory_order_release);
    m_direct_rumble.store(0, std::memory_order_relaxed);
    m_direct_addr_valid = false;
    m_direct_sent_valid = false;
    m_direct_logged     = false;
    m_direct_trigger_sent   = false;
    m_direct_trigger_logged = false;
}

bool ExtendedInputManager::listDevices(AkiraInputDeviceList* out) const
{
    if (out == nullptr || m_availability != Availability::Available)
        return false;

    mutexLock(&m_srv_lock);
    const Result rc = serviceDispatchOut(const_cast<Service*>(&m_srv),
                                         AkiraInputCmd_ListDevices, *out);
    mutexUnlock(&m_srv_lock);

    return R_SUCCEEDED(rc);
}

void ExtendedInputManager::setRawWanted(bool wanted)
{
    m_raw_wanted.store(wanted, std::memory_order_relaxed);
}

/*
 * Off until someone turns it on, and it does not persist.
 *
 * Streaming output through btdrv is what froze the console, twice, hard enough
 * to need the power button - and the cause is still open. Until the single-shot
 * probe says which of the operation and the rate is responsible, nothing may
 * put that traffic on MissionControl's report thread by merely starting a
 * stream. The switch is in Settings, it defaults off on every boot, and the
 * probe does not go through here.
 */
std::atomic<bool> ExtendedInputManager::s_direct_stream_allowed{false};
std::atomic<bool> ExtendedInputManager::s_output_refused{false};
std::atomic<uint32_t> ExtendedInputManager::s_direct_vid_pid{0};
std::atomic<bool> ExtendedInputManager::s_backend_released{false};

/*
 * Haptics are gated apart from rumble, because they are not the same risk.
 *
 * Rumble and trigger effects are event-driven: a handful of frames a second,
 * only when something changes. Haptics are a stream - forty-seven frames a
 * second, every second, and a successful write measured ten to thirteen
 * milliseconds against a twenty-one millisecond budget. That is half of
 * MissionControl's single server thread, which also carries every forward and
 * every write the HID sysmodule makes.
 *
 * One of those is a control plane doing what it is for. The other is a media
 * stream on the same wire, and it is the one that locks the console.
 */
std::atomic<bool> ExtendedInputManager::s_direct_haptics_allowed{false};

void ExtendedInputManager::setDirectHapticsAllowed(bool allowed)
{
    s_direct_haptics_allowed.store(allowed, std::memory_order_release);
}

bool ExtendedInputManager::directHapticsAllowed()
{
    loadDirectGatesOnce();
    return s_direct_haptics_allowed.load(std::memory_order_acquire);
}

/*
 * Read back from the config the first time either gate is consulted.
 *
 * Statics start false at every launch, and before this was persisted that meant
 * a toggle set in Settings was gone by the next run with nothing on screen to
 * say so - so a test could quietly run against a disabled path. Done lazily
 * because these are read from the poll thread as well as the UI, and there is
 * no startup hook that is guaranteed to run before both.
 */
/*
 * What to assume before any pad has been latched.
 *
 * The DualSense category, because it is the only one direct output means
 * anything for - and because the settings screen consults these before a
 * stream has started, when there is no address to resolve against yet. Once a
 * pad is latched, refreshDirectGates replaces this with its own answer.
 */
void ExtendedInputManager::loadDirectGatesOnce()
{
    static std::once_flag once;
    std::call_once(once, []() {
        auto* settings = SettingsManager::getInstance();
        if (settings == nullptr)
            return;

        const akira::input::RumbleProfile ds =
            settings->getRumbleProfile(akira::input::kRumbleKeyDualSense);

        s_direct_stream_allowed.store(
            ds.output_mode == akira::input::PadOutputMode::Native, std::memory_order_release);
        s_direct_haptics_allowed.store(
            ds.rumble_source != akira::input::RumbleSource::Off,
            std::memory_order_release);
    });
}

void ExtendedInputManager::setDirectStreamAllowed(bool allowed)
{
    s_direct_stream_allowed.store(allowed, std::memory_order_release);
}

bool ExtendedInputManager::directStreamAllowed()
{
    loadDirectGatesOnce();
    return s_direct_stream_allowed.load(std::memory_order_acquire);
}

void ExtendedInputManager::refreshDirectGates(uint16_t vendor_id, uint16_t product_id,
                                             const uint8_t* address)
{
    auto* settings = SettingsManager::getInstance();
    if (settings == nullptr)
        return;

    /* Never switch-native: this path only ever drives a pad that reached us
     * through MissionControl, and HOS-driven pads have no output report of
     * ours to take over. */
    const akira::input::RumbleProfile profile =
        settings->resolveRumbleProfile(vendor_id, product_id, address, false, false);

    /*
     * Asked of ResolvePadDriver rather than worked out again here.
     *
     * This used to read output_mode itself, which meant the gate and the path
     * could disagree - the gate knew about the per-pad setting and nothing
     * else, so a backend that had refused the claim still left it open.
     */
    s_direct_vid_pid.store(((uint32_t)vendor_id << 16) | product_id,
                           std::memory_order_release);

    akira::input::PadOutputInputs in;
    in.supported_pad     = akira::input::PadTakesDirectOutput(vendor_id, product_id);
    in.profile_native    = profile.output_mode == akira::input::PadOutputMode::Native;
    in.backend_enabled   = !s_backend_released.load(std::memory_order_acquire);
    in.ownership_refused = outputOwnershipRefused();

    /* The gate says what this pad is allowed to be, not whether anything is
     * asking for it right now - the pump owns that, and asks the same resolver
     * with the same fields filled in for real. */
    in.wanted        = true;
    in.address_valid = true;
    in.owns_output   = !in.ownership_refused;

    const bool native =
        akira::input::ResolvePadOutput(in).driver == akira::input::PadDriver::Akira;

    s_direct_stream_allowed.store(native, std::memory_order_release);
    /*
     * Derived, not stored.
     *
     * A pad with coils is never asked where its rumble comes from - it plays
     * the waveform, which is the whole reason it has them. The only thing that
     * can stop the coils is the profile being off altogether.
     */
    const bool haptics = profile.rumble_source != akira::input::RumbleSource::Off;
    s_direct_haptics_allowed.store(haptics, std::memory_order_release);

    brls::Logger::info("direct gates for {:04x}:{:04x}: driver={} haptics={}",
                       vendor_id, product_id,
                       akira::input::PadDriverName(
                           native ? akira::input::PadDriver::Akira
                                  : akira::input::PadDriver::MissionControl),
                       haptics);
}

void ExtendedInputManager::setDirectOutput(bool enabled)
{
    if (!enabled) {
        m_direct_rumble.store(0, std::memory_order_relaxed);

        /* The game that chose it is gone, so the pad goes back to saying who
         * is driving it rather than keeping the last frame's colour. */
        clearGameLightbar();
    }

    m_direct_wanted.store(enabled, std::memory_order_release);
}

void ExtendedInputManager::setDirectRumble(uint8_t left, uint8_t right)
{
    m_direct_rumble.store((uint16_t)(((uint16_t)left << 8) | right),
                          std::memory_order_relaxed);
}

void ExtendedInputManager::setConsoleIntensity(akira::input::Ds5EffectIntensity vibration,
                                              akira::input::Ds5EffectIntensity trigger)
{
    const uint8_t was_v = m_console_vibration.exchange((uint8_t)vibration,
                                                       std::memory_order_relaxed);
    const uint8_t was_t = m_console_trigger.exchange((uint8_t)trigger,
                                                     std::memory_order_relaxed);

    if (was_v == (uint8_t)vibration && was_t == (uint8_t)trigger)
        return;

    brls::Logger::info("console intensity: vibration={} triggers={} (byte 0x{:02x})",
                       (int)vibration, (int)trigger,
                       akira::input::Ds5IntensityByte(vibration, trigger));
}

akira::input::Ds5EffectIntensity ExtendedInputManager::consoleVibrationIntensity() const
{
    return (akira::input::Ds5EffectIntensity)m_console_vibration.load(std::memory_order_relaxed);
}

akira::input::Ds5EffectIntensity ExtendedInputManager::consoleTriggerIntensity() const
{
    return (akira::input::Ds5EffectIntensity)m_console_trigger.load(std::memory_order_relaxed);
}

void ExtendedInputManager::setGameLightbar(uint8_t red, uint8_t green, uint8_t blue)
{
    m_game_lightbar.store(kGameLightbarSet
                          | ((uint32_t)red << 16)
                          | ((uint32_t)green << 8)
                          | (uint32_t)blue,
                          std::memory_order_release);
}

void ExtendedInputManager::clearGameLightbar()
{
    m_game_lightbar.store(0, std::memory_order_release);
}

void ExtendedInputManager::setDirectTriggerEffects(uint8_t left_type, const uint8_t* left_params,
                                                   uint8_t right_type, const uint8_t* right_params)
{
    if (left_params == nullptr || right_params == nullptr)
        return;

    /* Odd while writing, even when whole - the same shape the raw report uses
     * in the other direction, for the same reason: twenty-two bytes cannot be
     * published in one store, and a half-updated effect is a resistance curve
     * nobody asked for. */
    const uint32_t seq = m_direct_trigger_seq.load(std::memory_order_relaxed);
    m_direct_trigger_seq.store(seq + 1, std::memory_order_release);

    m_direct_trigger_left[0] = left_type;
    std::memcpy(m_direct_trigger_left + 1, left_params, akira::input::kDs5TriggerParamBytes);
    m_direct_trigger_right[0] = right_type;
    std::memcpy(m_direct_trigger_right + 1, right_params, akira::input::kDs5TriggerParamBytes);

    m_direct_trigger_seq.store(seq + 2, std::memory_order_release);
}

bool ExtendedInputManager::writeOutputReport(const uint8_t* address, const uint8_t* data, uint16_t length)
{
    if (length == 0 || length > AKIRA_INPUT_OUTPUT_MAX)
        return false;

    AkiraInputPadRef which{};
    std::memcpy(which.bt_addr, address, sizeof(which.bt_addr));

    mutexLock(&m_srv_lock);
    const Result rc = serviceDispatchIn(&m_srv, AkiraInputCmd_WriteOutputReport, which,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In },
        .buffers = { { data, length } });
    mutexUnlock(&m_srv_lock);

    return R_SUCCEEDED(rc);
}

/*
 * The sequence has to be taken and sent without a gap.
 *
 * Two threads drive this pad - the poll thread with rumble and triggers, the
 * haptic thread with audio - and both used to draw a sequence number, build a
 * frame, and only then take the send lock. Preempt one between those steps and
 * the pad receives the higher sequence first. It treats that as reordering on
 * a link where reordering is not supposed to happen, and what follows is
 * exactly the load-dependent, stochastic refusal we have been chasing.
 *
 * The dongle firmware that does this successfully assigns its sequence at the
 * send boundary for the same reason. Byte one is the sequence nibble in every
 * DualSense Bluetooth output report, so one stamp under the send lock covers
 * 0x31, 0x32 and 0x39 alike.
 */
bool ExtendedInputManager::writeDirectFrame(uint8_t* frame, uint16_t length)
{
    /* Five, not two: the sequence goes in byte one and the checksum takes the
     * last four, so anything shorter has nowhere to put them. */
    if (frame == nullptr || length < 5 || length > AKIRA_INPUT_OUTPUT_MAX)
        return false;

    AkiraInputPadRef which{};
    std::memcpy(which.bt_addr, m_direct_addr, sizeof(which.bt_addr));

    mutexLock(&m_srv_lock);
    frame[1] = (uint8_t)((m_direct_seq.fetch_add(1, std::memory_order_relaxed) & 0x0f) << 4);

    /*
     * Re-stamp, because the sequence is inside what the checksum covers.
     *
     * The builders finish by stamping the CRC over the whole frame, and byte
     * one is part of that range. Writing the sequence here after the fact left
     * every frame carrying a checksum for the frame it used to be. btdrv takes
     * those without complaint - it does not read the payload - so the writes
     * all reported success while the pad silently dropped every one of them.
     */
    akira::input::StampDs5Crc(frame, length);

    const Result rc = serviceDispatchIn(&m_srv, AkiraInputCmd_WriteOutputReport, which,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In },
        .buffers = { { frame, length } });
    mutexUnlock(&m_srv_lock);

    return R_SUCCEEDED(rc);
}

/*
 * Everything this needs to know, asked once.
 *
 * The fields were checked here in one combination, in pumpDirectOutput in
 * another, and in ResolvePadDriver in a third - each right on its own and each
 * able to disagree with the others. Notably this never asked whether we hold
 * the claim, so it kept streaming to a pad MissionControl had been handed back.
 */
akira::input::PadOutputState ExtendedInputManager::outputState() const
{
    akira::input::PadOutputInputs in;

    const uint32_t ids = s_direct_vid_pid.load(std::memory_order_acquire);
    in.supported_pad = ids != 0
        && akira::input::PadTakesDirectOutput((uint16_t)(ids >> 16),
                                              (uint16_t)(ids & 0xffff));

    in.profile_native    = s_direct_stream_allowed.load(std::memory_order_acquire);
    in.haptics_wanted    = s_direct_haptics_allowed.load(std::memory_order_acquire);
    in.backend_enabled   = !s_backend_released.load(std::memory_order_acquire);
    in.ownership_refused = s_output_refused.load(std::memory_order_acquire);
    in.owns_output       = m_owns_output;
    in.wanted            = m_direct_wanted.load(std::memory_order_acquire);
    in.address_valid     = m_direct_addr_valid;
    in.haptics_landing   = m_haptics_landing.load(std::memory_order_acquire);

    return akira::input::ResolvePadOutput(in);
}

bool ExtendedInputManager::directHapticsReady() const
{
    return outputState().stream_haptics;
}

bool ExtendedInputManager::sendDirectHaptics(uint8_t* frame, uint16_t length)
{
    if (!directHapticsReady() || frame == nullptr)
        return false;

    return writeDirectFrame(frame, length);
}

/*
 * Runs on the poll thread, once per tick.
 *
 * Comparing against what was last sent rather than against the previous tick is
 * what makes the rate floor safe - a write that is skipped for being too soon
 * is retried on the next tick, so the pad always ends up holding the amplitude
 * the console last asked for rather than one from the middle of a burst.
 */
void ExtendedInputManager::ensureOutputOwnership(bool want)
{
    if (!serviceIsActive(&m_srv))
        return;

    /*
     * A claim we hold is re-asserted, not assumed.
     *
     * This used to return whenever the state already matched, which meant that
     * once the claim was taken akira never asked about it again - and the
     * backend can let go without us: the overlay switch releases every pad
     * immediately, on purpose, so that a claim cannot outlive the answer.
     *
     * The symptom was a pad nobody was driving. akira went on writing to it
     * believing it still owned it, MissionControl had resumed writing to it,
     * and turning the switch back on could not recover because akira saw no
     * transition to act on.
     *
     * Both calls are idempotent, so re-asking costs one IPC a second and is the
     * only thing that can notice a release we were not told about.
     */
    const uint32_t now = nowMs();
    if (want) {
        /*
         * At most once a second, whether or not we believe we already hold it.
         * The first version threw the throttle away as soon as a refusal
         * cleared m_owns_output, which turned a once-a-second re-assert into an
         * IPC every frame and buried the log under it.
         */
        if (now < m_owns_recheck)
            return;
        m_owns_recheck = now + 1000;
    } else if (!m_owns_output) {
        return;
    }

    /* Only acquiring needs a live address; releasing uses the one we took it
     * with, which outlives the valid flag. Requiring it for both meant a pad
     * that dropped mid-stream kept ownership and left its link wide open. */
    if (want && !m_direct_addr_valid)
        return;

    AkiraInputExternalControl in{};
    std::memcpy(in.bt_addr, m_direct_addr, sizeof(in.bt_addr));
    in.acquire = want ? 1 : 0;

    uint32_t out_rc = 0;
    mutexLock(&m_srv_lock);
    const Result rc = serviceDispatchInOut(&m_srv, AkiraInputCmd_ExternalControl,
                                           in, out_rc);
    mutexUnlock(&m_srv_lock);

    /*
     * Two ways to be told no, and they mean the same thing.
     *
     * The IPC itself can fail, or it can succeed and carry a refusal from the
     * backend - the toggle is off, or this MissionControl has no ownership
     * extension. Either way MissionControl is still writing to the pad, so
     * ResolvePadDriver hands it back and we stop writing too. Only an acquire
     * sets this; a release that fails leaves nothing to fall back from.
     */
    const bool refused = R_FAILED(rc) || R_FAILED((Result)out_rc);
    if (want)
        s_output_refused.store(refused, std::memory_order_release);

    if (R_FAILED(rc)) {
        brls::Logger::warning("output ownership: {} failed ipc=0x{:x}",
                              want ? "acquire" : "release", rc);
        return;
    }

    if (want && refused) {
        /* Refused means we do not have it, including when we thought we did -
         * the backend can let go without us. Saying so is what lets the next
         * tick try again instead of resting on a claim that is gone. */
        const bool had = m_owns_output;
        m_owns_output = false;
        if (had) {
            brls::Logger::warning("output ownership: lost - the backend released it");
        }
        brls::Logger::info("output ownership: refused rc=0x{:x} - leaving the pad "
                           "to MissionControl", out_rc);
        return;
    }

    const bool took_it = want && !m_owns_output;
    const bool gave_it = !want && m_owns_output;
    m_owns_output = want;

    /* MissionControl paints a player number as soon as it has the pad back, so
     * what we last sent is no longer what the pad is showing. */
    if (!want)
        m_lightbar_painted_valid = false;

    /*
     * Only when the claim actually changes hands.
     *
     * The claim is re-asserted once a second because the backend can let go
     * without telling us, and this line used to report every one of those as a
     * fresh acquisition - about a hundred and forty identical lines in a
     * two-minute session. The cost was not the noise: it made a real
     * reacquisition, which is the thing worth seeing, indistinguishable from
     * the heartbeat that says nothing happened.
     */
    if (took_it || gave_it)
        brls::Logger::info("output ownership: {} rc=0x{:x} - link {}",
                           want ? "acquired" : "released", out_rc,
                           want ? "widened for haptics" : "back to its default width");

    /* Painted on the way in, not on every re-assert. MissionControl repaints
     * the LED by player number as soon as it has the pad back, so the colour
     * reverting is the release becoming visible rather than something to
     * correct. */
    if (took_it)
        paintLightbar();
}

bool ExtendedInputManager::resolveLightbar(uint8_t* rgb) const
{
    if (!m_direct_addr_valid || rgb == nullptr)
        return false;

    auto* settings = SettingsManager::getInstance();
    if (settings == nullptr)
        return false;

    const uint32_t ids = s_direct_vid_pid.load(std::memory_order_acquire);
    if (ids == 0)
        return false;

    /*
     * One switch for both colours.
     *
     * Off means akira never claims the LED, and a game asking for red does not
     * change that - the setting is about whether the pad's light is ours to
     * write at all, not about which of two sources wins.
     */
    const akira::input::RumbleProfile profile =
        settings->resolveRumbleProfile((uint16_t)(ids >> 16), (uint16_t)(ids & 0xffff),
                                       m_direct_addr, false, false);
    if (!profile.lightbar_enabled)
        return false;

    const uint32_t game = m_game_lightbar.load(std::memory_order_acquire);
    if (game & kGameLightbarSet) {
        rgb[0] = (uint8_t)(game >> 16);
        rgb[1] = (uint8_t)(game >> 8);
        rgb[2] = (uint8_t)game;
        return true;
    }

    rgb[0] = profile.lightbar_r;
    rgb[1] = profile.lightbar_g;
    rgb[2] = profile.lightbar_b;
    return true;
}

/*
 * Say who is driving, on the pad - and let the game say it instead while there
 * is one.
 *
 * Nothing else akira sends claims the LED, so until now the colour was always
 * whatever MissionControl last painted. Setting our own makes ownership
 * observable from across the room: the chosen colour means akira holds the
 * claim, a player colour means MissionControl has taken it back. Off by
 * default, because it is a preference before it is an instrument.
 *
 * Only ever writes on a change. MissionControl pushes its own state to the
 * npad in response to a lightbar write, so a repaint on every tick is a pad
 * changing hands repeatedly for no reason.
 */
void ExtendedInputManager::paintLightbar()
{
    uint8_t rgb[3];
    if (!resolveLightbar(rgb))
        return;

    const uint32_t want = ((uint32_t)rgb[0] << 16) | ((uint32_t)rgb[1] << 8) | rgb[2];
    if (m_lightbar_painted_valid && m_lightbar_painted == want)
        return;

    const uint32_t now = nowMs();
    if (m_lightbar_painted_valid && (now - m_lightbar_painted_ms) < kLightbarIntervalMs)
        return;

    uint8_t frame[akira::input::kDs5BluetoothFrameBytes];
    const size_t len = akira::input::BuildDs5LightbarFrame(
        0, rgb[0], rgb[1], rgb[2], frame, sizeof(frame));
    if (len == 0)
        return;

    const bool ok = writeDirectFrame(frame, (uint16_t)len);
    if (ok) {
        m_lightbar_painted       = want;
        m_lightbar_painted_valid = true;
        m_lightbar_painted_ms    = now;
    }

    /*
     * Every write, which the rate limit above is what makes affordable: four a
     * second at worst. Logging only the first would save nothing worth having
     * and would remove the only way to tell a game's colour reaching the pad
     * from the profile's colour sitting there unchanged.
     */
    brls::Logger::info("lightbar: {:02x}{:02x}{:02x} {}",
                       rgb[0], rgb[1], rgb[2], ok ? "sent" : "refused");
}

void ExtendedInputManager::reconsiderBackendRefusal()
{
    if (!s_output_refused.load(std::memory_order_acquire))
        return;

    const uint32_t now = nowMs();
    if (now < m_refusal_recheck)
        return;
    m_refusal_recheck = now + 1000;

    const bool enabled = backendDirectOutputEnabled();
    s_backend_released.store(!enabled, std::memory_order_release);
    if (!enabled)
        return;

    /*
     * Cleared here and nowhere else. The pump will try the claim again on the
     * next tick, ResolvePadDriver will answer akira, and the path is rebuilt
     * from there - so this is the whole of the way back.
     */
    s_output_refused.store(false, std::memory_order_release);
    brls::Logger::info("output ownership: the backend has native output on again");
}

void ExtendedInputManager::pumpDirectOutput()
{
    reconsiderBackendRefusal();

    /*
     * Say why, once.
     *
     * In-stream direct output has now produced no writes at all on two
     * separate evenings, and both times the sysmodule log showed nothing
     * because there was nothing to show - the gate shut before any write was
     * attempted. Silence that means "blocked" and silence that means "sent
     * fine" looked identical, so this distinguishes them.
     */
    const akira::input::PadOutputState state = outputState();
    const bool allowed = state.want_claim || state.write_state;

    ensureOutputOwnership(state.want_claim);

    if (!allowed || !m_direct_addr_valid) {
        if (!m_direct_gate_logged) {
            m_direct_gate_logged = true;
            brls::Logger::info("direct output blocked: stream_toggle={} addr_valid={} wanted={}",
                               allowed, m_direct_addr_valid,
                               m_direct_wanted.load(std::memory_order_acquire));
        }
        return;
    }

    if (!m_direct_gate_logged) {
        m_direct_gate_logged = true;
        brls::Logger::info("direct output live: addr valid, wanted={}",
                           m_direct_wanted.load(std::memory_order_acquire));
    }

    paintLightbar();

    const uint32_t now = nowMs();
    pumpDirectState(now);
}

/*
 * A failed write, and whether that is now enough to give up on the address.
 *
 * A pad that has gone away refuses every write, and nothing else here would
 * ever stop asking. Forgetting the address ends that; the next raw report from
 * a pad that takes this puts it back.
 */
bool ExtendedInputManager::directWriteFailed()
{
    if (++m_direct_failures < kDirectFailureLimit)
        return false;

    ensureOutputOwnership(false);
    m_direct_addr_valid = false;
    return true;
}

void ExtendedInputManager::pumpDirectState(uint32_t now)
{
    /*
     * Rumble and triggers in one frame, on one schedule.
     *
     * These were two pumps sending two frames, on the reasoning that unclaimed
     * fields are left untouched so each could move independently. They are
     * left untouched - which is the problem: a frame that omits a field can
     * never correct it, so the pad kept whatever any earlier frame had set and
     * the same inputs stopped producing the same output.
     *
     * It also doubled the cost. A write is roughly seven milliseconds of the
     * one thread the whole console's Bluetooth shares, and that is per write,
     * not per byte. One complete frame is half the traffic and the only way to
     * fully determine what the pad is doing.
     */
    const bool wanted = m_direct_wanted.load(std::memory_order_acquire);

    const uint16_t want_rumble = wanted
        ? m_direct_rumble.load(std::memory_order_relaxed)
        : 0;

    uint8_t want_triggers[sizeof(m_direct_trigger_last)] = {};
    if (wanted) {
        const uint32_t seq = m_direct_trigger_seq.load(std::memory_order_acquire);
        if (seq != 0 && (seq & 1u) == 0) {
            std::memcpy(want_triggers, m_direct_trigger_left, sizeof(m_direct_trigger_left));
            std::memcpy(want_triggers + sizeof(m_direct_trigger_left),
                        m_direct_trigger_right, sizeof(m_direct_trigger_right));

            /* Changed underneath us - leave it for the next tick rather than
             * sending a frame made of two different updates. */
            if (m_direct_trigger_seq.load(std::memory_order_acquire) != seq)
                return;
        }
    }

    /* Nothing to say, and nothing outstanding to take back. */
    if (!wanted && (!m_direct_sent_valid || (m_direct_sent == 0 && !m_direct_trigger_sent)))
        return;

    /*
     * Read once and compared, not read again at build time.
     *
     * The console can change this mid-stream and it moves neither the rumble
     * nor the triggers, so it has to be part of what "unchanged" means - or
     * the new setting would not reach the pad until the game next happened to
     * rumble.
     */
    const uint8_t want_intensity = akira::input::Ds5IntensityByte(consoleVibrationIntensity(),
                                                                  consoleTriggerIntensity());

    const bool same = m_direct_sent_valid
                   && want_rumble == m_direct_sent
                   && want_intensity == m_direct_intensity_last
                   && std::memcmp(want_triggers, m_direct_trigger_last,
                                  sizeof(want_triggers)) == 0;
    if (same)
        return;

    if (m_direct_sent_valid && (now - m_direct_sent_ms) < kDirectWriteIntervalMs)
        return;

    const uint8_t* left  = want_triggers;
    const uint8_t* right = want_triggers + sizeof(m_direct_trigger_left);

    uint8_t frame[akira::input::kDs5BluetoothFrameBytes];
    const size_t len = akira::input::BuildDs5StateFrame(
        0,
        (uint8_t)(want_rumble >> 8), (uint8_t)(want_rumble & 0xff),
        left[0], left + 1, right[0], right + 1,
        frame, sizeof(frame), false, akira::input::Ds5FrameLayout::Tagged, want_intensity);
    if (len == 0)
        return;

    if (!writeDirectFrame(frame, (uint16_t)len)) {
        m_direct_sent_ms = now;
        (void)directWriteFailed();
        return;
    }

    m_direct_sent            = want_rumble;
    m_direct_sent_valid      = true;
    m_direct_intensity_last  = want_intensity;
    m_direct_sent_ms         = now;
    m_direct_trigger_sent    = true;
    m_direct_trigger_sent_ms = now;
    m_direct_failures        = 0;
    std::memcpy(m_direct_trigger_last, want_triggers, sizeof(want_triggers));

    if (!m_direct_trigger_logged) {
        m_direct_trigger_logged = true;
        brls::Logger::info("direct state: rumble={:04x} triggers left=0x{:02x} right=0x{:02x} "
                           "intensity=0x{:02x}",
                           want_rumble, left[0], right[0], want_intensity);
    }
}


bool ExtendedInputManager::readRawReport(AkiraInputRawReport* out) const
{
    if (out == nullptr)
        return false;

    /* Bounded rather than a spin: the writer runs at 120 Hz and holds the
     * sequence odd for one memcpy, so losing three races in a row means
     * something is wrong and the caller is better off falling back than
     * spinning on the render tick. */
    for (int attempt = 0; attempt < 3; attempt++) {
        const uint32_t before = m_raw_seq.load(std::memory_order_acquire);
        if (before & 1u)
            continue;

        AkiraInputRawReport copy = m_raw;

        const uint32_t after = m_raw_seq.load(std::memory_order_acquire);
        if (before != after)
            continue;

        if (before == 0 || copy.length == 0)
            return false;

        const uint32_t published = m_raw_published_ms.load(std::memory_order_acquire);
        if (nowMs() - published > kMaxAgeMs)
            return false;

        *out = copy;
        return true;
    }

    return false;
}

void ExtendedInputManager::pollThreadFunc(void* arg)
{
    akira_thread_set_affinity(AKIRA_THREAD_NAME_EXTENDED_INPUT);
    static_cast<ExtendedInputManager*>(arg)->poll();
}

/*
 * Periodic status line, so a silent path is distinguishable from an idle one.
 *
 * This used to watch one safety-critical invariant: a signal count and a
 * forward count that had to stay equal, because redirection stopped every
 * Bluetooth controller on the console until each report was answered and a gap
 * between them was a wedge in progress. A claim is per controller and the
 * backend's wait on it is bounded, so that invariant is gone.
 *
 * What is left is worth watching for a different reason. Nothing reports until
 * it is claimed, so claims held and reports arriving are the two halves of
 * "are we actually reading this pad" - and a claim held with no reports is now
 * the interesting failure rather than an impossible one.
 */
void ExtendedInputManager::logStatus(const char* when)
{
    AkiraInputStatus status{};
    const Result rc = serviceDispatchOut(&m_srv, AkiraInputCmd_GetStatus, status);
    if (R_FAILED(rc)) {
        brls::Logger::warning("analog triggers [{}]: status read failed (0x{:x})", when, rc);
        return;
    }

    /*
     * Whether the writer is getting anything through.
     *
     * Judged on the delta since the last read rather than the totals, so a
     * pad that failed earlier in a session and now works is allowed to come
     * back - and one that has started refusing everything gives the motors
     * their turn instead of leaving the user with silence.
     */
    const uint64_t wrote  = status.output_written - m_haptics_written_seen;
    const uint64_t missed = status.output_failed  - m_haptics_failed_seen;
    m_haptics_written_seen = status.output_written;
    m_haptics_failed_seen  = status.output_failed;

    if (missed > 0 && wrote == 0) {
        if (m_haptics_landing.exchange(false, std::memory_order_acq_rel)) {
            brls::Logger::warning(
                "haptic stream: {} report(s) refused and none accepted - falling back to motors",
                missed);
        }
    } else if (wrote > 0) {
        m_haptics_landing.store(true, std::memory_order_release);
    }

    /* Reports since the last line rather than the total, because the total
     * only ever grows and says nothing about whether anything is arriving
     * now. */
    const uint64_t reports = status.reports_received - m_reports_seen;
    m_reports_seen = status.reports_received;

    brls::Logger::info(
        "analog triggers [{}]: state={} subs={} claims={} reports=+{} last=0x{:x}",
        when, status.backend_state, status.subscriber_count,
        status.claims_held, reports,
        static_cast<uint32_t>(status.last_error));

    /* A claim held that produces nothing is the failure this API can have and
     * the old one could not: the pad was ceded and its reports are going
     * nowhere. Worth a warning rather than a number in a line. */
    if (status.claims_held > 0 && reports == 0) {
        brls::Logger::warning(
            "analog triggers [{}]: {} claim(s) held and no reports arriving",
            when, status.claims_held);
    }

    logRawReport(when);

    if (status.last_unknown_vid_pid != 0) {
        brls::Logger::warning(
            "analog triggers [{}]: unrecognised pad vid={:04x} pid={:04x} - no analog for it",
            when,
            status.last_unknown_vid_pid >> 16,
            status.last_unknown_vid_pid & 0xffff);
    }

    /*
     * The wedge alarm is gone with the thing it watched.
     *
     * An unanswered signal used to mean every Bluetooth controller on the
     * console was stopped until something answered it, which is why this was an
     * error rather than a warning. A claim is per controller and the backend's
     * wait on it is bounded, so the worst an unanswered report can now do is
     * cost its own pad a tenth of a second before the backend takes it back.
     * The claim-with-no-reports warning above is what replaces it.
     */
}

/*
 * Dump the last raw report as hex.
 *
 * This is the diagnostic for the case that is otherwise impossible to reason
 * about from the outside: triggers producing noise rather than nothing, which
 * means the byte offsets are wrong for this pad. Nothing else can tell you
 * that, because a wrong offset still yields plausible-looking numbers.
 */
void ExtendedInputManager::logRawReport(const char* when)
{
    if (!m_direct_addr_valid) {
        return;
    }

    AkiraInputTriggerQuery query{};
    std::memcpy(query.bt_addr, m_direct_addr, sizeof(query.bt_addr));

    AkiraInputRawReport raw{};
    const Result rc = serviceDispatchInOut(&m_srv, AkiraInputCmd_GetRawReport, query, raw);
    if (R_FAILED(rc)) {
        return; /* no pad has reported yet - not worth a line */
    }

    const size_t len = raw.length > AKIRA_INPUT_RAW_MAX ? AKIRA_INPUT_RAW_MAX : raw.length;

    std::string hex;
    hex.reserve(len * 3);
    for (size_t i = 0; i < len; i++) {
        hex += std::format("{:02x}", raw.data[i]);
        if (i + 1 < len) {
            hex += ' ';
        }
    }

    brls::Logger::info(
        "analog triggers [{}]: raw vid={:04x} pid={:04x} report=0x{:02x} len={} desc_len={}",
        when, raw.vendor_id, raw.product_id, raw.report_id, raw.length,
        raw.descriptor_length);
    brls::Logger::info("analog triggers [{}]: raw bytes {}", when, hex);
}

/*
 * Learn the pad's address without needing it first.
 *
 * The address used to come out of a raw report, deliberately, so that it
 * belonged to the pad actually speaking rather than to whatever ListDevices
 * happened to name first. That reasoning inverted when the backend moved to
 * claims: nothing reports until it is claimed, a claim names an address, and
 * an address that could only be learned from a report was one we could never
 * learn at all. Both ends waited for the other and the path stayed dark.
 *
 * ListDevices needs no claim, so it is what breaks the circle. The report is
 * still preferred once one arrives - see the poll loop - because it remains
 * the better answer when there is one; this is only how the first one is
 * made possible.
 */
void ExtendedInputManager::resolveDirectAddress()
{
    AkiraInputDeviceList devices{};
    if (!listDevices(&devices)) {
        return;
    }

    for (uint8_t i = 0; i < devices.count; i++) {
        const AkiraInputDeviceInfo& dev = devices.devices[i];
        if (!akira::input::PadTakesDirectOutput(dev.vendor_id, dev.product_id)) {
            continue;
        }

        if (!m_direct_addr_valid ||
            std::memcmp(m_direct_addr, dev.bt_addr, sizeof(m_direct_addr)) != 0) {
            std::memcpy(m_direct_addr, dev.bt_addr, sizeof(m_direct_addr));
            m_direct_addr_valid = true;
            brls::Logger::info(
                "direct output: pad {:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x} "
                "({:04x}:{:04x}) from the device list",
                dev.bt_addr[0], dev.bt_addr[1], dev.bt_addr[2],
                dev.bt_addr[3], dev.bt_addr[4], dev.bt_addr[5],
                dev.vendor_id, dev.product_id);
        }

        /*
         * The gates too, not just the address.
         *
         * supported_pad is read from the vendor and product ids these set, and
         * ResolvePadOutput answers NotSupportedPad before it considers
         * anything else - so leaving them to the raw report handler left the
         * same circle intact one field over. The address was learnable and the
         * pad was still officially unrecognised, which meant no claim, so no
         * report, so nothing ever set them.
         *
         * Rumble went with it. A pad we do not claim is one MissionControl
         * keeps writing to, and akira sending nothing while MissionControl
         * parks the coils is silence at both ends.
         */
        refreshDirectGates(dev.vendor_id, dev.product_id, dev.bt_addr);
        return;
    }
}

void ExtendedInputManager::poll()
{
    uint32_t ticks = 0;

    while (m_running.load(std::memory_order_acquire)) {
        /*
         * Once a second while we have no address, and never once we do. The
         * claim cannot be asked for without one, and the reports that would
         * otherwise supply it cannot arrive without the claim.
         */
        if (!m_direct_addr_valid && (ticks % 60) == 0) {
            resolveDirectAddress();
        }

        if (!m_direct_addr_valid) {
            m_snapshot.store(0, std::memory_order_relaxed);
            svcSleepThread(kPollIntervalNs);
            continue;
        }

        AkiraInputTriggerQuery query{};
        std::memcpy(query.bt_addr, m_direct_addr, sizeof(query.bt_addr));

        AkiraInputTriggerState state{};
        mutexLock(&m_srv_lock);
        const Result rc = serviceDispatchInOut(&m_srv, AkiraInputCmd_GetTriggerState, query, state);
        mutexUnlock(&m_srv_lock);

        if (R_SUCCEEDED(rc) && state.capability == AkiraInputTrigger_Analog && state.raw_max > 0) {
            const uint32_t l2 = (static_cast<uint32_t>(state.l2_raw) * 0xff) / state.raw_max;
            const uint32_t r2 = (static_cast<uint32_t>(state.r2_raw) * 0xff) / state.raw_max;
            m_snapshot.store(pack(static_cast<uint8_t>(l2 > 0xff ? 0xff : l2),
                                  static_cast<uint8_t>(r2 > 0xff ? 0xff : r2),
                                  true, nowMs()),
                             std::memory_order_relaxed);
        } else if (R_FAILED(rc) && rc != AKIRA_INPUT_ERR_NO_STATE) {
            /* A hard error means the backend is gone, not that this controller
             * has nothing to report. Stop for the session and let the render
             * tick fall back to digital. */
            brls::Logger::warning("analog triggers: backend lost (0x{:x})", rc);
            m_snapshot.store(0, std::memory_order_relaxed);
            m_degraded_notice.store(true, std::memory_order_release);
            m_running.store(false, std::memory_order_release);
            return;
        } else {
            /* NO_STATE is ordinary: no analog controller connected right now.
             * Drop validity so the tick uses digital, and keep polling. */
            m_snapshot.store(0, std::memory_order_relaxed);
        }

        if (m_raw_wanted.load(std::memory_order_relaxed)) {
            AkiraInputRawReport raw{};
            mutexLock(&m_srv_lock);
            const Result raw_rc = serviceDispatchInOut(&m_srv, AkiraInputCmd_GetRawReport, query, raw);
            mutexUnlock(&m_srv_lock);

            if (R_SUCCEEDED(raw_rc) && raw.length > 0) {
                /* Odd while writing, even when whole. Release on the way in so
                 * the copy cannot be hoisted above the marker, and on the way
                 * out so a reader that sees the even value sees the copy too. */
                const uint32_t seq = m_raw_seq.load(std::memory_order_relaxed);
                m_raw_seq.store(seq + 1, std::memory_order_release);
                m_raw = raw;
                m_raw_seq.store(seq + 2, std::memory_order_release);

                m_raw_published_ms.store(nowMs(), std::memory_order_release);

                /* The address the direct path writes to. Taken from the report
                 * rather than from ListDevices so it belongs to the pad that is
                 * actually speaking, and only for a model whose output report
                 * we know - a DualSense frame sent to anything else is bytes
                 * that pad has no reading for. */
                if (akira::input::PadTakesDirectOutput(raw.vendor_id, raw.product_id)) {
                    if (!m_direct_addr_valid ||
                        std::memcmp(m_direct_addr, raw.bt_addr, sizeof(m_direct_addr)) != 0) {
                        std::memcpy(m_direct_addr, raw.bt_addr, sizeof(m_direct_addr));
                        m_direct_addr_valid = true;
                        refreshDirectGates(raw.vendor_id, raw.product_id, raw.bt_addr);
                        m_direct_sent_valid = false;
                        m_direct_failures   = 0;
                        m_haptics_landing.store(true, std::memory_order_release);
                    }
                }
            }
        }

        pumpDirectOutput();

        if (++ticks >= kStatusLogInterval) {
            ticks = 0;
            logStatus("stream");
        }

        svcSleepThread(kPollIntervalNs);
    }
}


bool ExtendedInputManager::consumeDegradedNotice()
{
    return m_degraded_notice.exchange(false, std::memory_order_acq_rel);
}

bool ExtendedInputManager::hasFreshAnalogState() const
{
    const uint64_t snap = m_snapshot.load(std::memory_order_relaxed);
    if ((snap & kValidBit) == 0) {
        return false;
    }
    /* Unsigned wrap gives the right answer across the 32-bit millisecond
     * rollover without a special case. */
    return (nowMs() - static_cast<uint32_t>(snap >> 32)) < kMaxAgeMs;
}

uint8_t ExtendedInputManager::l2() const
{
    return static_cast<uint8_t>(m_snapshot.load(std::memory_order_relaxed) & 0xff);
}

uint8_t ExtendedInputManager::r2() const
{
    return static_cast<uint8_t>((m_snapshot.load(std::memory_order_relaxed) >> 8) & 0xff);
}
