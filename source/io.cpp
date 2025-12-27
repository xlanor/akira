#include "core/io.hpp"
#include "core/settings_manager.hpp"
#include <borealis.hpp>

#include "core/io/audio_manager.hpp"
#include "core/io/haptic_manager.hpp"
#include "core/io/input_manager.hpp"
#include "core/io/video_decoder.hpp"

#include "core/io/deko3d_renderer.hpp"

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

    m_audio_manager = new AudioManager();
    m_haptic_manager = new HapticManager();
    m_input_manager = new InputManager();
    m_video_decoder = new VideoDecoder();
}

IO::~IO()
{
    if (m_audio_manager)
    {
        delete m_audio_manager;
        m_audio_manager = nullptr;
    }
    if (m_haptic_manager)
    {
        delete m_haptic_manager;
        m_haptic_manager = nullptr;
    }
    if (m_input_manager)
    {
        delete m_input_manager;
        m_input_manager = nullptr;
    }
    FreeVideo();
}

void IO::SetMesaConfig()
{
}

bool IO::VideoCB(uint8_t* buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void* user)
{
    static uint32_t video_cb_count = 0;
    if (video_cb_count++ % 100 == 0)
    {
        brls::Logger::info("VideoCB: frame#{}, quit={}, decoder={}",
            video_cb_count, this->quit, m_video_decoder ? "valid" : "null");
    }

    if (this->quit || !m_video_decoder)
        return false;

    // When frames are lost, flush the decoder to clear stale reference frames
    // The decoder will skip packets until it receives an IDR frame
    if (frames_lost > 0)
    {
        brls::Logger::warning("IO: {} frames lost (recovered={})", frames_lost, frame_recovered);
        m_video_decoder->flush();
    }

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
    m_video_renderer = new Deko3dRenderer();

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

    if (m_video_decoder)
    {
        m_video_decoder->cleanup();
        delete m_video_decoder;
        m_video_decoder = nullptr;
    }

    // Now safe to destroy renderer (GPU memory no longer referenced)
    if (m_video_renderer)
    {
        m_video_renderer->cleanup();
        delete m_video_renderer;
        m_video_renderer = nullptr;
    }

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
    // Recreate decoder if it was freed (e.g., after disconnect)
    if (!m_video_decoder)
    {
        m_video_decoder = new VideoDecoder();
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
    static uint32_t mainloop_count = 0;
    if (mainloop_count++ % 100 == 0)
    {
        FrameQueue* q = m_video_decoder ? m_video_decoder->getFrameQueue() : nullptr;
        brls::Logger::info("MainLoop: #{}, hasFrames={}, queueSize={}",
            mainloop_count,
            q ? q->hasFrames() : false,
            q ? q->size() : 0);
    }

    static int render_attempt = 0;
    if (m_video_renderer && m_video_renderer->isInitialized() && m_video_decoder)
    {
        FrameQueue* queue = m_video_decoder->getFrameQueue();
        if (queue && queue->hasFrames())
        {
            queue->get([this](AVFrame* frame) {
                // Validate frame has actual data
                if (frame && frame->data[0])
                {
                    if (!m_first_frame_received)
                    {
                        m_first_frame_received = true;
                        brls::Logger::info("First video frame received!");
                    }
                    if (render_attempt++ % 100 == 0)
                    {
                        brls::Logger::info("Calling draw() #{}", render_attempt);
                    }
                    m_video_renderer->draw(frame);
                }
            });
        }

        if (m_show_stats_overlay && mainloop_count % 100 == 0)
        {
            brls::Logger::info("MainLoop: stats overlay enabled, passing to renderer");
        }
        m_video_renderer->setShowStatsOverlay(m_show_stats_overlay);
        if (m_show_stats_overlay)
        {
            m_video_renderer->setStreamStats(getStreamStats());
        }
    }
    else if (mainloop_count % 100 == 0)
    {
        brls::Logger::warning("Render skipped: renderer={}, initialized={}, decoder={}",
            m_video_renderer ? "valid" : "null",
            m_video_renderer ? m_video_renderer->isInitialized() : false,
            m_video_decoder ? "valid" : "null");
    }

    return !this->quit;
}

StreamStats IO::getStreamStats() const
{
    StreamStats stats;

    // Requested profile
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
