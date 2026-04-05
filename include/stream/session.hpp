#ifndef AKIRA_SESSION_HPP
#define AKIRA_SESSION_HPP

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>

#include <chiaki/session.h>
#include <chiaki/controller.h>
#include <chiaki/log.h>
#include <switch.h>
#include <chrono>

#include "core/exception.hpp"
#include "stream/stream_stats.hpp"

// Forward declarations - full headers included in session.cpp only
class AudioManager;
class HapticManager;
class InputManager;
class VideoDecoder;
class IVideoRenderer;

class Session
{
protected:
    Session();

private:
    ChiakiLog* log;
    std::atomic<bool> quit = false;

    std::unique_ptr<AudioManager> m_audio_manager;
    std::unique_ptr<HapticManager> m_haptic_manager;
    std::unique_ptr<InputManager> m_input_manager;
    std::unique_ptr<VideoDecoder> m_video_decoder;
    std::unique_ptr<IVideoRenderer> m_video_renderer;

    bool m_show_stats_overlay = false;

    bool m_first_frame_received = false;

    int m_requested_width = 0;
    int m_requested_height = 0;
    int m_requested_fps = 0;
    int m_requested_bitrate = 0;
    bool m_requested_hevc = false;

    ChiakiSession* m_session = nullptr;
    std::chrono::steady_clock::time_point m_stream_start_time;
    std::atomic<size_t> m_network_frames_lost = 0;
    std::atomic<size_t> m_frames_recovered = 0;

public:
    Session(const Session&) = delete;
    void operator=(const Session&) = delete;
    static Session* GetInstance();

    int getHapticBase() const;
    void setHapticBase(int base);
    void setRumbleStrength(float strength);
    void setRumbleFreqs(float freqLow, float freqHigh);
    void setEnvelopeDecay(float decay);
    void setEnvelopeAttack(float attack);

    ~Session();

    void SetMesaConfig();
    bool VideoCB(uint8_t* buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void* user);
    void InitAudioCB(unsigned int channels, unsigned int rate);
    void AudioCB(int16_t* buf, size_t samples_count);
    bool InitVideo(int video_width, int video_height);
    bool InitAVCodec(bool is_PS5, int video_width = 0, int video_height = 0);
    bool FreeVideo();
    bool InitController();
    bool FreeController();
    bool MainLoop();
    void UpdateControllerState(ChiakiControllerState* state, std::map<uint32_t, int8_t>* finger_id_touch_id);
    void SetRumble(uint8_t left, uint8_t right);
    void HapticCB(uint8_t* buf, size_t buf_size);
    void CleanUpHaptic();

    void SetLogger(ChiakiLog* log);
    ChiakiLog* GetLogger() { return this->log; }

    StreamStats getStreamStats();
    bool getShowStatsOverlay() const { return m_show_stats_overlay; }
    void setShowStatsOverlay(bool show) { m_show_stats_overlay = show; }
    void setVideoPaused(bool paused);
    void triggerBorderFlash();
    void setRequestedProfile(int width, int height, int fps, int bitrate, bool hevc);

    bool hasReceivedFirstFrame() const { return m_first_frame_received; }
    void updateActualResolution(int width, int height);

    InputManager* getInputManager() { return m_input_manager.get(); }
    IVideoRenderer* getVideoRenderer() { return m_video_renderer.get(); }

    void setSession(ChiakiSession* session);
    void startStreamTimer();
    void resetStreamStats();
};

#endif // AKIRA_SESSION_HPP
