#include "stream/session.hpp"
#include "core/settings_manager.hpp"
#include <borealis.hpp>

#include "stream/audio_manager.hpp"
#include "stream/haptic_manager.hpp"
#include "stream/input_manager.hpp"
#include "stream/video_decoder.hpp"
#include "stream/deko3d_renderer.hpp"
#include "stream/ipc_service.hpp"

#include <cstring>

#include <chiaki/packetstats.h>

Session* Session::GetInstance()
{
    static Session* instance = new Session();
    return instance;
}

int Session::getHapticBase() const
{
    return m_haptic_manager ? m_haptic_manager->hapticBase : 400;
}

void Session::setHapticBase(int base)
{
    if (m_haptic_manager)
        m_haptic_manager->hapticBase = base;
}

void Session::setRumbleStrength(float strength)
{
    if (m_haptic_manager)
        m_haptic_manager->setRumbleStrength(strength);
}

void Session::setRumbleFreqs(float freqLow, float freqHigh)
{
    if (m_haptic_manager)
    {
        m_haptic_manager->setRumbleFreqLow(freqLow);
        m_haptic_manager->setRumbleFreqHigh(freqHigh);
    }
}

void Session::setEnvelopeDecay(float decay)
{
    if (m_haptic_manager)
        m_haptic_manager->setEnvelopeDecay(decay);
}

void Session::setEnvelopeAttack(float attack)
{
    if (m_haptic_manager)
        m_haptic_manager->setEnvelopeAttack(attack);
}

void Session::setRumbleCeiling(float ceiling)
{
    if (m_haptic_manager)
        m_haptic_manager->setRumbleCeiling(ceiling);
}

void Session::setRumbleSource(akira::input::RumbleSource source)
{
    if (m_haptic_manager)
        m_haptic_manager->setRumbleSource(source);
}

void Session::SetLogger(ChiakiLog* log)
{
    this->log = log;
    if (m_audio_manager) m_audio_manager->setLogger(log);
    if (m_haptic_manager) m_haptic_manager->setLogger(log);
    if (m_input_manager) m_input_manager->setLogger(log);
    if (m_video_decoder) m_video_decoder->setLogger(log);
}

Session::Session()
{
    this->log = nullptr;

    m_audio_manager = std::make_unique<AudioManager>();
    m_haptic_manager = std::make_unique<HapticManager>();
    m_input_manager = std::make_unique<InputManager>();

    /* Rumble goes to the pad the user is playing on, which is the input
     * manager's business to know, not borealis's notion of controller zero. */
    m_haptic_manager->setInputManager(m_input_manager.get());

    /* Owned by the input manager, and outlives the haptic manager's use of it
     * because both die with this Session. */
    m_haptic_manager->setExtendedInput(&m_input_manager->extendedInput());
    m_video_decoder = std::make_unique<VideoDecoder>();
}

Session::~Session()
{
    FreeVideo();
}

void Session::SetMesaConfig()
{
}

bool Session::VideoCB(uint8_t* buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void* user)
{
    if (this->quit || !m_video_decoder)
        return false;

    if (frames_lost > 0)
        m_network_frames_lost += frames_lost;
    if (frame_recovered)
        m_frames_recovered++;

    return m_video_decoder->decode(buf, buf_size);
}

void Session::InitAudioCB(unsigned int channels, unsigned int rate)
{
    if (m_audio_manager)
    {
        m_audio_manager->init(channels, rate);
    }
}

void Session::AudioCB(int16_t* buf, size_t samples_count)
{
    if (m_audio_manager)
    {
        m_audio_manager->play(buf, samples_count);
    }

    if (m_haptic_manager && m_haptic_manager->isLocked())
    {
        CleanUpHaptic();
    }
}

bool Session::InitVideo(int video_width, int video_height)
{
    brls::Logger::info("load InitVideo");
    this->quit = false;

    if (!m_video_decoder || !m_video_decoder->initVideo(video_width, video_height))
    {
        brls::Logger::error("Failed to initialize video decoder");
        return false;
    }

    brls::Logger::info("Creating Deko3d renderer");
    m_video_renderer = std::make_unique<Deko3dRenderer>();

    if (!m_video_renderer->initialize(video_width, video_height, this->log))
    {
        brls::Logger::error("Failed to initialize video renderer");
        m_video_renderer.reset();
        return false;
    }

    m_video_decoder->setFrameReadyCallback([this](AVFrame* frame) {
        presentDecodedFrame(frame);
        av_frame_free(&frame);
    });

    return true;
}

bool Session::FreeVideo()
{
    this->quit = true;

    if (m_video_decoder)
        m_video_decoder->setFrameReadyCallback(nullptr);

    if (m_ipc_service)
        m_ipc_service->SetStreamActive(false);

    m_first_frame_received = false;
    SettingsManager::getInstance()->setStreamingActive(false);

    if (m_video_renderer)
    {
        m_video_renderer->waitIdle();
    }

    m_video_decoder.reset();
    m_video_renderer.reset();

    resetStreamStats();

    return true;
}

void Session::SetRumble(uint8_t left, uint8_t right)
{
    if (m_haptic_manager)
    {
        m_haptic_manager->setRumble(left, right);
    }
}

void Session::SetTriggerEffects(const ChiakiTriggerEffectsEvent* effects)
{
    if (effects == nullptr || !m_input_manager)
        return;

    /*
     * Nothing to send if the console says the triggers stay soft.
     *
     * The intensity byte already tells the pad, so this is not what makes it
     * work - it is what stops it costing anything. Every effect is a Bluetooth
     * write on the one thread the whole console shares, and spending those on
     * resistance nobody will feel is the wrong trade.
     */
    if (akira::input::Ds5IntensityFromWire(m_trigger_intensity)
        == akira::input::Ds5EffectIntensity::Off)
        return;

    auto* path = m_input_manager->path();
    if (!path)
        return;

    akira::input::PadPath::TriggerEffect left;
    akira::input::PadPath::TriggerEffect right;

    left.type = effects->type_left;
    std::memcpy(left.params, effects->left, sizeof(left.params));
    right.type = effects->type_right;
    std::memcpy(right.params, effects->right, sizeof(right.params));

    path->sendTriggerEffects(left, right);
}

void Session::SetEffectIntensity(uint8_t vibration, uint8_t trigger)
{
    m_trigger_intensity = trigger;

    if (!m_input_manager)
        return;

    if (auto* path = m_input_manager->path())
        path->sendEffectIntensity(vibration, trigger);

    /*
     * The haptic waveform is ours to render, so the vibration setting has to
     * reach the conversion as well as the pad. The pad's own attenuation
     * covers what the pad generates; nothing there knows about a stream of
     * samples we turn into amplitudes ourselves.
     */
    if (m_haptic_manager)
        m_haptic_manager->setConsoleVibration(vibration);
}

void Session::SetLedColor(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!m_input_manager)
        return;

    if (auto* path = m_input_manager->path())
        path->sendLightbar(red, green, blue);
}

void Session::HapticCB(uint8_t* buf, size_t buf_size)
{
    if (m_haptic_manager)
    {
        m_haptic_manager->processHapticAudio(buf, buf_size);
    }
}

void Session::CleanUpHaptic()
{
    if (m_haptic_manager)
    {
        m_haptic_manager->cleanup();
    }
}

void Session::updateActualResolution(int width, int height)
{
    if (width != m_requested_width || height != m_requested_height)
    {
        brls::Logger::info("Stream resolution changed: requested {}x{}, actual {}x{} (server downgraded)",
            m_requested_width, m_requested_height, width, height);
    }

    if (m_video_decoder)
        m_video_decoder->updateResolution(width, height);

    if (m_video_renderer)
        m_video_renderer->updateResolution(width, height);
}

bool Session::InitAVCodec(bool is_PS5, int video_width, int video_height)
{
    if (!m_video_decoder)
    {
        m_video_decoder = std::make_unique<VideoDecoder>();
    }

    return m_video_decoder->initCodec(is_PS5, video_width, video_height);
}

bool Session::InitController()
{
    return m_input_manager && m_input_manager->init();
}

bool Session::FreeController()
{
    if (m_input_manager)
        m_input_manager->cleanup();
    return true;
}

void Session::UpdateControllerState(ChiakiControllerState* state, std::map<uint32_t, int8_t>* finger_id_touch_id)
{
    if (m_input_manager)
    {
        m_input_manager->update(state, finger_id_touch_id);
    }
}

bool Session::MainLoop()
{
    if (m_video_renderer && m_video_renderer->isInitialized() && m_video_decoder)
    {
        m_video_renderer->setShowStatsOverlay(m_show_stats_overlay);
        if (m_show_stats_overlay)
        {
            m_video_renderer->setStreamStats(getStreamStats());
        }
    }

    return !this->quit;
}

void Session::presentDecodedFrame(AVFrame* frame)
{
    if (!frame || !frame->data[0] || !m_video_renderer)
        return;

    if (!m_first_frame_received)
    {
        m_first_frame_received = true;
        brls::Application::setRenderSuspended(true);
        updateActualResolution(frame->width, frame->height);
        SettingsManager::getInstance()->setStreamingActive(true);
        brls::Logger::info("First video frame received!");

        if (SettingsManager::getInstance()->getIpcStatsEnabled())
        {
            if (!m_ipc_service)
            {
                m_ipc_service = std::make_unique<IpcStatsService>(this);
                m_ipc_service->Start();
            }
            m_ipc_service->SetStreamActive(true);
        }
    }

    m_video_renderer->presentFrame(frame);
}

StreamStats Session::getStreamStats()
{
    StreamStats stats;

    stats.requested_width = m_requested_width;
    stats.requested_height = m_requested_height;
    stats.requested_fps = m_requested_fps;
    stats.requested_bitrate = m_requested_bitrate;
    stats.requested_hevc = m_requested_hevc;
    
    if (m_input_manager) {
        auto& extended = m_input_manager->extendedInput();
        stats.analog_triggers_active = extended.hasFreshAnalogState();
        stats.analog_l2 = extended.l2();
        stats.analog_r2 = extended.r2();
    }

    if (m_video_renderer)
        stats.fps = m_video_renderer->getRenderFPS();

    if (m_video_decoder)
    {
        stats.video_width = m_video_decoder->getVideoWidth();
        stats.video_height = m_video_decoder->getVideoHeight();
        stats.is_hevc = m_video_decoder->isHEVC();
        stats.is_hardware_decoder = m_video_decoder->isHardwareAccelerated();
    }

    stats.renderer_name = "Deko3d";

    if (m_session)
    {
        uint64_t received = 0, lost = 0;
        chiaki_packet_stats_get(&m_session->stream_connection.packet_stats, false, &received, &lost);
        stats.packets_received = received;
        stats.packets_lost = lost;
        if (received + lost > 0)
            stats.packet_loss_percent = static_cast<float>(lost) / static_cast<float>(received + lost) * 100.0f;
        stats.measured_bitrate_mbps = static_cast<float>(m_session->stream_connection.measured_bitrate);
    }

    stats.network_frames_lost = m_network_frames_lost;
    stats.frames_recovered = m_frames_recovered;

    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - m_stream_start_time);
    stats.stream_duration_seconds = duration.count();

    return stats;
}

void Session::setRequestedProfile(int width, int height, int fps, int bitrate, bool hevc)
{
    m_requested_width = width;
    m_requested_height = height;
    m_requested_fps = fps;
    m_requested_bitrate = bitrate;
    m_requested_hevc = hevc;
}

void Session::setVideoPaused(bool paused)
{
    if (m_video_renderer)
        m_video_renderer->setPaused(paused);
}

void Session::triggerBorderFlash()
{
    if (m_video_renderer)
        m_video_renderer->triggerBorderFlash();
}

void Session::setSession(ChiakiSession* session)
{
    m_session = session;
}

void Session::startStreamTimer()
{
    m_stream_start_time = std::chrono::steady_clock::now();
}

void Session::resetStreamStats()
{
    m_network_frames_lost = 0;
    m_frames_recovered = 0;
    m_session = nullptr;
}
