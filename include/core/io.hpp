#ifndef AKIRA_IO_HPP
#define AKIRA_IO_HPP

#include <cstdint>
#include <map>

#include <chiaki/session.h>
#include <chiaki/controller.h>
#include <chiaki/log.h>
#include <switch.h>

#include "exception.hpp"
#include "core/io/stream_stats.hpp"

// Forward declarations - full headers included in io.cpp only
class AudioManager;
class HapticManager;
class InputManager;
class VideoDecoder;
class IVideoRenderer;

class IO
{
protected:
    IO();
    static IO* instance;

private:
    ChiakiLog* log;
    bool quit = false;

    // Default Nintendo Switch resolution
    int screen_width = 1280;
    int screen_height = 720;

    // Managers
    AudioManager* m_audio_manager = nullptr;
    HapticManager* m_haptic_manager = nullptr;
    InputManager* m_input_manager = nullptr;
    VideoDecoder* m_video_decoder = nullptr;
    IVideoRenderer* m_video_renderer = nullptr;

    // Stats overlay
    bool m_show_stats_overlay = false;

    // Requested stream profile
    int m_requested_width = 0;
    int m_requested_height = 0;
    int m_requested_fps = 0;
    int m_requested_bitrate = 0;
    bool m_requested_hevc = false;

public:
    // Singleton configuration
    IO(const IO&) = delete;
    void operator=(const IO&) = delete;
    static IO* GetInstance();

    // Access haptic settings through HapticManager (implemented in io.cpp)
    int getHapticBase() const;
    void setHapticBase(int base);
    void setRumbleStrength(float strength);

    ~IO();

    void SetMesaConfig();
    bool VideoCB(uint8_t* buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void* user);
    void InitAudioCB(unsigned int channels, unsigned int rate);
    void AudioCB(int16_t* buf, size_t samples_count);
    bool InitVideo(int video_width, int video_height, int screen_width, int screen_height);
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

    // Stats overlay
    StreamStats getStreamStats() const;
    bool getShowStatsOverlay() const { return m_show_stats_overlay; }
    void setShowStatsOverlay(bool show) { m_show_stats_overlay = show; }
    void setRequestedProfile(int width, int height, int fps, int bitrate, bool hevc);
};

#endif // AKIRA_IO_HPP
