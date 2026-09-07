#include "stream/haptic_manager.hpp"

#include <cstdio>
#include "stream/input_manager.hpp"
#include "core/settings_manager.hpp"
#include "input/ps_output.hpp"
#include "input/rumble_profile.hpp"
#include <algorithm>
#include <cstring>
#include <borealis.hpp>

HapticManager::HapticManager()
{
}

HapticManager::~HapticManager()
{
}


/*
 * Every send goes through the picked pad. Resolved fresh each time rather than
 * cached, because the path can be swapped while a stream is running.
 */
/*
 * A pad with voice coils has nothing to choose.
 *
 * The waveform is the thing it plays, so "derive rumble from the haptic track
 * or take the game's rumble commands instead" is not a question the page asks
 * on a DualSense - which means its stored answer is whatever a config written
 * before that narrowing happens to carry, and honouring it would silence the
 * coils on the strength of a setting nobody was shown.
 */
akira::input::RumbleSource HapticManager::effectiveSource() const
{
    if (m_rumble_source == akira::input::RumbleSource::Off)
        return akira::input::RumbleSource::Off;

    auto* path = m_input ? m_input->path() : nullptr;
    if (path != nullptr
        && akira::input::PadTakesDirectOutput(path->vendorId(), path->productId()))
        return akira::input::RumbleSource::Derived;

    return m_rumble_source;
}

void HapticManager::emit(float left, float right)
{
    if (!m_input)
        return;

    auto* path = m_input->path();
    if (!path)
        return;

    /*
     * Every motor amplitude in this class ends here, which is the only reason
     * the console's setting can be applied in one place.
     *
     * Not on a natively driven pad: there the two values travel as the rumble
     * fields of a frame that also carries the intensity byte, and the pad
     * applies the setting itself. Everything else - a Joy-Con, a pad reached
     * through MissionControl's translation - has no idea the setting exists,
     * so this is where it gets honoured.
     */
    if (!path->nativeRumble()) {
        const float scale = akira::input::Ds5IntensityScale(
            akira::input::Ds5IntensityFromWire(
                m_console_vibration.load(std::memory_order_relaxed)));
        left  *= scale;
        right *= scale;
    }

    /* The loudest this pad may be driven, from its profile. This used to be a
     * constant inside the Switch path, which is why it could not apply to any
     * other pad and could not be changed on that one. */
    if (left  > m_ceiling) left  = m_ceiling;
    if (right > m_ceiling) right = m_ceiling;

    path->sendRumble(left, right, m_freq_low, m_freq_high);
}

void HapticManager::setRumble(uint8_t left, uint8_t right)
{
    if (SettingsManager::getInstance()->getDebugChiakiLog() && (left > 0 || right > 0))
        brls::Logger::info("setRumble: left={}, right={}, strength={:.2f}", left, right, m_rumble_strength);

    /*
     * Counted before it is dropped, because "the console sends no rumble
     * while we are announced as a DualSense" was an assumption in a comment
     * and never a measurement. If these arrive alongside the haptic stream,
     * Game is a local choice; if they never arrive, it is not reachable.
     */
    m_rumble_events++;
    if (m_rumble_events == 1)
        brls::Logger::info("rumble events: console sent one while announced as a DualSense"
                           " (haptic buffers so far: {})", m_haptic_buffers);

    /* Deriving from the haptic track and playing the game's own commands are
     * alternatives, not layers - running both puts the second underneath the
     * thing it was meant to replace. */
    if (effectiveSource() != akira::input::RumbleSource::Game)
        return;

    /*
     * Passed through at full scale, the way chiaki does it - it hands SDL
     * left << 8 and lets the pad decide. The 160 ceiling that used to live here
     * is a Joy-Con tuning: full-amplitude HD rumble is harsh, so it was capped.
     * Applied to every pad it meant an ERM motor could never exceed 160/255 of
     * its range no matter what the console asked for, which is most of why
     * rumble felt weak on a DualSense. The ceiling now lives in the Switch
     * paths that need it.
     */
    float leftAmp  = (left  > 0) ? ((float)left  / 255.0f) * m_rumble_strength : 0.0f;
    float rightAmp = (right > 0) ? ((float)right / 255.0f) * m_rumble_strength : 0.0f;

    emit(leftAmp, rightAmp);
}

/*
 * With haptics enabled the PS5 stops sending rumble commands and sends this
 * stream instead, so for a pad we drive ourselves this is the only source of
 * rumble there is - not a fallback for one we could not reach.
 *
 * The conversion is chiaki's rather than a re-tuning of the Joy-Con one, and
 * the differences are all things that read as a broken pad rather than a quiet
 * one: no gate, so the stream's residual level becomes a permanent hum; no
 * stall floor, so quiet passages whine without turning the motors; and a
 * divisor a stream sets to 50, which saturates about a tenth of the way up and
 * leaves every effect arriving at the same full blast.
 */
/*
 * Re-resolved when the pad changes rather than on every buffer.
 *
 * The path can be swapped mid-stream, and a profile read once at session start
 * would keep shaping for whichever controller happened to be chosen first.
 */
void HapticManager::refreshProfile()
{
    auto* path = m_input ? m_input->path() : nullptr;
    if (path == nullptr) {
        m_profile_path = nullptr;
        return;
    }

    m_profile = SettingsManager::getInstance()->resolveRumbleProfile(
        path->vendorId(), path->productId(), path->address(), path->switchNative(),
        path->kind() == akira::input::PadPathKind::JoyCon);
    m_profile_path = path;
}

/*
 * Straight through: chiaki's haptic sink and the DualSense's coils run at the
 * same 3000 Hz stereo, so there is nothing to resample and nothing to average.
 * The only conversion is sixteen bits down to the eight the report carries.
 */
bool HapticManager::streamNativeHaptics(const int16_t* stereo, size_t frames)
{
    if (!m_extended || !m_extended->directHapticsReady()) {
        /*
         * Said once. Direct output going away mid-stream - the pad dropping,
         * or ownership being lost - is silent otherwise and looks identical to
         * haptics simply not working.
         */
        if (!m_haptic_stopped_logged && m_haptic_sent != 0) {
            m_haptic_stopped_logged = true;
            brls::Logger::info("haptic stream: stopped after {} frames - handing the"
                               " signal back to the motors", m_haptic_sent);
        }
        return false;
    }

    /*
     * Sixteen bits down to eight, scaled by the intensity setting.
     *
     * There was no gain here at all: the actuators played whatever the console
     * sent, and the intensity control only ever touched the motor fallback. A
     * value above unity clips rather than compresses, which for haptics reads
     * as a harder impact rather than as distortion.
     */
    /*
     * Two scales, and they are not the same thing.
     *
     * The profile's is per-pad tuning and can exceed unity; the console's is
     * the user's own setting and only ever attenuates. Multiplying them makes
     * the console's the ceiling this pad is tuned underneath, which is the
     * precedence that matches where each was set.
     */
    const float gain = akira::input::HapticSampleGain(m_profile.haptic_intensity)
                     * akira::input::Ds5IntensityScale(
                           akira::input::Ds5IntensityFromWire(
                               m_console_vibration.load(std::memory_order_relaxed)));

    for (size_t i = 0; i < frames * 2; i++) {
        int32_t v = (int32_t)((float)(stereo[i] >> 8) * gain);
        if (v >  127) v =  127;
        if (v < -128) v = -128;

        m_haptic_block[m_haptic_fill++] = (int8_t)v;

        if (m_haptic_fill < akira::input::kDs5HapticBlockBytes * 2)
            continue;

        m_haptic_fill = 0;

        /*
         * A block of silence costs the same air time as a block of content,
         * and the actuators do nothing with it either way. Games are quiet
         * between impacts, so skipping these is most of the traffic - which
         * now buys radio time and pad battery rather than avoiding a refusal.
         */
        int block_peak = 0;
        for (std::size_t k = 0; k < akira::input::kDs5HapticBlockBytes * 2; k++) {
            const int a = m_haptic_block[k] < 0 ? -(int)m_haptic_block[k]
                                                :  (int)m_haptic_block[k];
            if (a > block_peak)
                block_peak = a;
        }
        if (block_peak <= akira::input::kHapticBlockFloor) {
            m_haptic_skipped++;
            continue;
        }

        m_haptic_last_send = std::chrono::steady_clock::now();
        m_haptic_last_send_valid = true;

        /*
         * The 547-byte form rather than the 142-byte one.
         *
         * Both reach the pad now, but this carries two blocks - 21.3ms of
         * audio against 10.7ms - so real time costs half the writes, and the
         * writes were always the scarce thing. It is also the frame that has
         * actually been felt: byte for byte what DS5Dongle sends, verified
         * against a capture taken out of the console's memory while the
         * actuators were moving.
         */
        uint8_t frame[akira::input::kDs5HapticFrameBytes];
        const size_t len = akira::input::BuildDs5HapticFrame(
            0, m_haptic_counter,
            m_haptic_block, akira::input::kDs5HapticBlockBytes * 2,
            frame, sizeof(frame));

        m_haptic_counter  = (uint8_t)(m_haptic_counter + 2);

        if (len == 0)
            continue;

        /*
         * A refusal hands this buffer back to the motors and nothing more.
         *
         * There were three strikes here and a session-long latch behind them,
         * written when every frame over 78 bytes was refused by a link that
         * could not carry one. That is fixed, and the latch is now the worse
         * failure: one transient write error would drop a whole session back
         * to two rumble motors with no way to recover. Dropping the block and
         * letting the next one try is both simpler and correct.
         */
        if (!m_extended->sendDirectHaptics(frame, (uint16_t)len)) {
            m_haptic_fill = 0;
            return false;
        }

        m_haptic_sent++;

        /*
         * Every twenty-five rather than every two hundred. At the rates this
         * path now runs, two hundred sends is over a minute, and the console
         * has been going down inside twenty seconds - the old interval would
         * lose the entire run.
         */
        if ((m_haptic_sent % 25) == 0) {
            if (!m_haptic_first_send_valid) {
                m_haptic_first_send = m_haptic_last_send;
                m_haptic_first_send_valid = true;
            }
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                m_haptic_last_send - m_haptic_first_send).count();
            brls::Logger::info("haptic stream: {} sent, {} skipped,"
                               " {} ms elapsed, {} per sec",
                               m_haptic_sent, m_haptic_skipped,
                               (long long)ms,
                               ms > 0 ? (long long)(m_haptic_sent * 1000ll / ms) : 0ll);
        }

        if (!m_haptic_logged) {
            m_haptic_logged = true;
            brls::Logger::info("haptic stream: writing to the pad's actuators ({} bytes)", len);
        }
    }

    return true;
}

/*
 * The waveform, turned into two motor amplitudes.
 *
 * Used for every pad that is not playing the waveform on its own actuators -
 * whether that is a controller with no coils, or a DualSense we have handed
 * back to MissionControl. It used to be reachable only through the native path,
 * so a pad we stopped driving fell through to a much older conversion instead:
 * sum the absolute samples, divide by 64, divide by 50, clamp. That saturates
 * about a fifth of the way up, so every effect arrived at the same full blast,
 * and it has no gate, so the stream's noise floor sat under all of it as a hum.
 *
 * One waveform, one conversion, wherever it ends up.
 */
void HapticManager::emitMotorsFromWaveform(const int16_t* stereo, size_t frames)
{
    /*
     * A DualSense has no rotating mass to lift over a stall threshold, whichever
     * side is driving it - our own report, or MissionControl's translation of
     * the HOS vibration we hand it. Every other pad here keeps the floor.
     */
    auto* path = m_input ? m_input->path() : nullptr;
    const bool motor_stalls =
        path == nullptr
        || !akira::input::PadTakesDirectOutput(path->vendorId(), path->productId());

    const akira::input::HapticRumble r =
        akira::input::HapticAudioToRumble(stereo, frames, m_profile.haptic_intensity,
                                          motor_stalls);

    /*
     * A sub-gate buffer is the tail of what was playing, not silence to cut to.
     *
     * Returning here and letting cleanupHaptic zero the motors 30ms later is
     * what made this stutter: the haptic stream crosses the noise floor
     * constantly, so every dip stopped the motors dead and every rise started
     * them again. An ERM's inertia can blur a fast change; it cannot bridge a
     * gap where we deliberately send nothing.
     */
    const float target_left  = r.emit ? (float)r.left  / 65535.0f : 0.0f;
    const float target_right = r.emit ? (float)r.right / 65535.0f : 0.0f;

    /*
     * Instant on the way up, eased on the way down.
     *
     * Attack has to be immediate or the impact arrives late and softened -
     * that is the transient, and averaging three buffers before sending was
     * blunting exactly the thing worth feeling. Release is what makes
     * consecutive hits read as one event with a tail instead of as chatter.
     */
    m_native_env_left  = target_left  > m_native_env_left
                             ? target_left
                             : m_native_env_left  * kNativeRelease + target_left  * (1.0f - kNativeRelease);
    m_native_env_right = target_right > m_native_env_right
                             ? target_right
                             : m_native_env_right * kNativeRelease + target_right * (1.0f - kNativeRelease);

    /* Below what turns a motor there is nothing to lose by stopping, and
     * stopping cleanly is what lets the next impact register as a start. */
    if (m_native_env_left  < kNativeSilence) m_native_env_left  = 0.0f;
    if (m_native_env_right < kNativeSilence) m_native_env_right = 0.0f;

    if (++m_native_logged >= kNativeHapticLogInterval) {
        m_native_logged = 0;
        if (SettingsManager::getInstance()->getDebugChiakiLog()) {
            brls::Logger::info("haptic->rumble: raw={}/{} scaled={}/{} out={}/{}{}",
                               r.raw_left, r.raw_right, r.left, r.right,
                               (int)(m_native_env_left * 255.0f),
                               (int)(m_native_env_right * 255.0f),
                               (r.left >= 0xffff || r.right >= 0xffff) ? " SATURATED" : "");
        }
    }

    /* Published every buffer, so cleanupHaptic's 30ms timeout can never fire
     * against an envelope that is still coming down. */
    m_haptic_val = (int)((m_native_env_left > m_native_env_right
                              ? m_native_env_left : m_native_env_right) * 255.0f);
    m_haptic_lock_time = std::chrono::high_resolution_clock::now();
    m_haptic_lock = m_haptic_val != 0;

    emit(m_native_env_left, m_native_env_right);
}

/*
 * The conversion every pad used before this one existed.
 *
 * Sum the absolute samples, divide by 64, then by hapticBase, clamp. It
 * saturates about a fifth of the way up and has no gate, which is why it was
 * replaced on the DualSense - but it is also the shaping every Joy-Con and
 * every generic MissionControl pad has been tuned against, and changing what
 * those feel like was never part of fixing the DualSense.
 */
void HapticManager::emitMotorsLegacy(const uint8_t* buf, size_t buf_size)
{
    int16_t amplitudel = 0, amplituder = 0;
    int32_t suml = 0, sumr = 0;
    const size_t sample_size = 2 * sizeof(int16_t);

    size_t buf_count = buf_size / sample_size;
    if (buf_count == 0)
        return;

    for (size_t i = 0; i < buf_count; i++)
    {
        size_t cur = i * sample_size;
        memcpy(&amplitudel, buf + cur, sizeof(int16_t));
        memcpy(&amplituder, buf + cur + sizeof(int16_t), sizeof(int16_t));
        suml += (amplitudel < 0) ? -amplitudel : amplitudel;
        sumr += (amplituder < 0) ? -amplituder : amplituder;
    }

    int32_t avgLeft = suml / static_cast<int32_t>(buf_count) / 64;
    int32_t avgRight = sumr / static_cast<int32_t>(buf_count) / 64;
    uint8_t left = static_cast<uint8_t>(avgLeft > 255 ? 255 : avgLeft);
    uint8_t right = static_cast<uint8_t>(avgRight > 255 ? 255 : avgRight);

    setHapticRumble(left, right);
    if ((left != 0 || right != 0) && !m_haptic_lock)
    {
        m_haptic_lock = true;
    }
}

void HapticManager::processHapticAudio(uint8_t* buf, size_t buf_size)
{
    const auto* stereo = reinterpret_cast<const int16_t*>(buf);
    const size_t frames = buf_size / (2 * sizeof(int16_t));

    if (m_input && m_input->path() != m_profile_path)
        refreshProfile();

    m_haptic_buffers++;
    if (m_haptic_buffers == 1)
        brls::Logger::info("haptic stream: first buffer (rumble events so far: {})",
                           m_rumble_events);
    if (++m_stream_logged >= 600) {
        m_stream_logged = 0;
        brls::Logger::info("haptic stream: {} buffers, {} rumble events, source={}",
                           m_haptic_buffers, m_rumble_events, (int)effectiveSource());
    }

    if (effectiveSource() != akira::input::RumbleSource::Derived)
        return;

    /*
     * Off is a gate, not the bottom of a scale.
     *
     * The console keeps sending the waveform whatever the user set, so this is
     * the only place that decision can be honoured - and it has to stop both
     * routes, or turning vibration off would silence the coils and leave the
     * motors approximating the same signal underneath.
     */
    if (akira::input::Ds5IntensityFromWire(m_console_vibration.load(std::memory_order_relaxed))
        == akira::input::Ds5EffectIntensity::Off)
        return;

    /*
     * The coils first, and the motors only if that route is unavailable.
     *
     * They are alternatives rather than layers: a pad playing the waveform
     * through its actuators does not also want two motors approximating the
     * same signal, which would be the buzz sitting underneath the thing it is
     * meant to replace.
     */
    if (nativeRumble() && streamNativeHaptics(stereo, frames))
        return;

    /*
     * Which approximation, decided by the pad rather than by the path.
     *
     * The DualSense gets the tuned one whichever side is driving it - our own
     * report, or MissionControl's translation of the HOS vibration we hand it -
     * because that is the pad the tuning was measured on. Everything else keeps
     * what it had: this used to be reachable only through the native path, so
     * lifting it out changed the feel of every Joy-Con and every generic pad as
     * a side effect of fixing one controller.
     */
    auto* path = m_input ? m_input->path() : nullptr;
    const bool tuned_for_this_pad =
        path != nullptr
        && akira::input::PadTakesDirectOutput(path->vendorId(), path->productId());

    if (tuned_for_this_pad)
        emitMotorsFromWaveform(stereo, frames);
    else
        emitMotorsLegacy(buf, buf_size);
}

bool HapticManager::nativeRumble() const
{
    if (!m_input)
        return false;

    auto* path = m_input->path();
    return path != nullptr && path->nativeRumble();
}

void HapticManager::setHapticRumble(uint8_t left, uint8_t right)
{
    uint8_t val = left > right ? left : right;
    m_haptic_val = val;
    m_haptic_lock_time = std::chrono::high_resolution_clock::now();


    /*
     * Off is silence here as well as on the coils.
     *
     * HapticBaseForIntensity returns the old default divisor for Off rather
     * than something that divides to nothing, so this path kept buzzing at a
     * setting that reads as off everywhere else - it was only ever masked by
     * the separate switch that zeroed rumble strength.
     */
    if (m_profile.haptic_intensity == akira::input::HapticIntensity::Off) {
        m_envelope = 0.0f;
        emit(0.0f, 0.0f);
        return;
    }

    float amplitude = (float)val / (float)hapticBase;
    if (amplitude > 1.0f) amplitude = 1.0f;
    amplitude *= m_rumble_strength;

    if (amplitude >= m_envelope)
        m_envelope = m_envelope * (1.0f - m_envelope_attack) + amplitude * m_envelope_attack;
    else
        m_envelope = m_envelope * m_envelope_decay + amplitude * (1.0f - m_envelope_decay);

    emit(m_envelope, m_envelope);
}

void HapticManager::cleanupHaptic()
{
    std::chrono::system_clock::time_point now = std::chrono::high_resolution_clock::now();
    auto dur = now - m_haptic_lock_time;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
    if (m_haptic_val == 0)
    {
        m_haptic_lock = false;
    }
    else if (ms > 30)
    {
        m_envelope = 0.0f;
        m_haptic_val = 0;
        m_haptic_lock = false;
        emit(0.0f, 0.0f);
    }
}

void HapticManager::cleanup()
{
    m_envelope = 0.0f;
    m_haptic_lock = false;
    m_haptic_val = 0;
    emit(0.0f, 0.0f);
}
