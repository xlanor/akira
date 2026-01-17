#include "core/io.hpp"
#include "core/settings_manager.hpp"
#include <borealis.hpp>

#include "core/io/audio_manager.hpp"
#include "core/io/haptic_manager.hpp"
#include "core/io/input_manager.hpp"
#include "core/io/video_decoder.hpp"
#include "core/io/deko3d_renderer.hpp"

#include <chiaki/packetstats.h>

IO* IO::instance = nullptr;

IO* IO::GetInstance()
{
    if (instance == nullptr)
    {
        instance = new IO;
    }
    return instance;
}

int IO::getHapticBase() const
{
    return m_haptic_manager ? m_haptic_manager->hapticBase : 400;
}

void IO::setHapticBase(int base)
{
    if (m_haptic_manager)
        m_haptic_manager->hapticBase = base;
}

void IO::setRumbleStrength(float strength)
{
    if (m_haptic_manager)
        m_haptic_manager->setRumbleStrength(strength);
}

void IO::SetLogger(ChiakiLog* log)
{
    this->log = log;
    if (m_audio_manager) m_audio_manager->setLogger(log);
    if (m_haptic_manager) m_haptic_manager->setLogger(log);
    if (m_input_manager) m_input_manager->setLogger(log);
    if (m_video_decoder) m_video_decoder->setLogger(log);
}

IO::IO()
{
    this->log = nullptr;

    m_audio_manager = std::make_unique<AudioManager>();
    m_haptic_manager = std::make_unique<HapticManager>();
    m_input_manager = std::make_unique<InputManager>();
    m_video_decoder = std::make_unique<VideoDecoder>();
}

IO::~IO()
{
    FreeVideo();
}

void IO::SetMesaConfig()
{
}

bool IO::VideoCB(uint8_t* buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void* user)
{
    if (this->quit || !m_video_decoder)
        return false;

    if (frames_lost > 0)
        m_network_frames_lost += frames_lost;
    if (frame_recovered)
        m_frames_recovered++;

    return m_video_decoder->decode(buf, buf_size);
}

void IO::InitAudioCB(unsigned int channels, unsigned int rate)
{
    if (m_audio_manager)
    {
        m_audio_manager->init(channels, rate);
    }
}

void IO::AudioCB(int16_t* buf, size_t samples_count)
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

bool IO::InitVideo(int video_width, int video_height, int screen_width, int screen_height)
{
    brls::Logger::info("load InitVideo");
    this->quit = false; 
    this->screen_width = screen_width;
    this->screen_height = screen_height;

    if (!m_video_decoder || !m_video_decoder->initVideo(video_width, video_height, screen_width, screen_height))
    {
        brls::Logger::error("Failed to initialize video decoder");
        return false;
    }

    brls::Logger::info("Creating Deko3d renderer");
    m_video_renderer = std::make_unique<Deko3dRenderer>();

    if (!m_video_renderer->initialize(video_width, video_height, screen_width, screen_height, this->log))
    {
        throw Exception("Failed to initialize video renderer");
    }

    return true;
}

bool IO::FreeVideo()
{
    this->quit = true;
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

void IO::SetRumble(uint8_t left, uint8_t right)
{
    if (m_haptic_manager)
    {
        m_haptic_manager->setRumble(left, right);
    }
}

void IO::HapticCB(uint8_t* buf, size_t buf_size)
{
    if (m_haptic_manager)
    {
        m_haptic_manager->processHapticAudio(buf, buf_size);
    }
}

void IO::CleanUpHaptic()
{
    if (m_haptic_manager)
    {
        m_haptic_manager->cleanup();
    }
}

bool IO::InitAVCodec(bool is_PS5, int video_width, int video_height)
{
    if (!m_video_decoder)
    {
        m_video_decoder = std::make_unique<VideoDecoder>();
    }

    return m_video_decoder->initCodec(is_PS5, video_width, video_height);
}

bool IO::InitController()
{
    return m_input_manager && m_input_manager->init();
}

bool IO::FreeController()
{
    if (m_input_manager)
        m_input_manager->cleanup();
    return true;
}

void IO::UpdateControllerState(ChiakiControllerState* state, std::map<uint32_t, int8_t>* finger_id_touch_id)
{
    if (m_input_manager)
    {
        m_input_manager->update(state, finger_id_touch_id);
    }
}

bool IO::MainLoop()
{
    if (m_video_renderer && m_video_renderer->isInitialized() && m_video_decoder)
    {
        FrameQueue* queue = m_video_decoder->getFrameQueue();
        if (queue && queue->hasFrames())
        {
            queue->get([this](AVFrame* frame) {
                if (frame && frame->data[0])
                {
                    if (!m_first_frame_received)
                    {
                        m_first_frame_received = true;
                        SettingsManager::getInstance()->setStreamingActive(true);
                        brls::Logger::info("First video frame received!");
                    }
                    m_video_renderer->draw(frame);
                }
            });
        }

        m_video_renderer->setShowStatsOverlay(m_show_stats_overlay);
        if (m_show_stats_overlay)
        {
            m_video_renderer->setStreamStats(getStreamStats());
        }
    }

    return !this->quit;
}

StreamStats IO::getStreamStats()
{
    StreamStats stats;

    stats.requested_width = m_requested_width;
    stats.requested_height = m_requested_height;
    stats.requested_fps = m_requested_fps;
    stats.requested_bitrate = m_requested_bitrate;
    stats.requested_hevc = m_requested_hevc;

    if (m_video_decoder)
    {
        FrameQueue* queue = m_video_decoder->getFrameQueue();
        if (queue)
        {
            stats.fps = queue->getCurrentFPS();
            stats.dropped_frames = queue->getFramesDropped();
            stats.faked_frames = queue->getFakeFrameUsed();
            stats.queue_size = queue->size();
        }

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

void IO::setRequestedProfile(int width, int height, int fps, int bitrate, bool hevc)
{
    m_requested_width = width;
    m_requested_height = height;
    m_requested_fps = fps;
    m_requested_bitrate = bitrate;
    m_requested_hevc = hevc;
}

void IO::setVideoPaused(bool paused)
{
    if (m_video_renderer)
        m_video_renderer->setPaused(paused);
}

void IO::setSession(ChiakiSession* session)
{
    m_session = session;
}

void IO::startStreamTimer()
{
    m_stream_start_time = std::chrono::steady_clock::now();
}

void IO::resetStreamStats()
{
    m_network_frames_lost = 0;
    m_frames_recovered = 0;
    m_session = nullptr;
}
