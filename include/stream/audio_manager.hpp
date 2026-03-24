#ifndef AKIRA_IO_AUDIO_MANAGER_HPP
#define AKIRA_IO_AUDIO_MANAGER_HPP

#include <SDL2/SDL.h>
#include <cstdint>
#include <chiaki/log.h>

class AudioManager
{
public:
    AudioManager();
    ~AudioManager();

    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    void setLogger(ChiakiLog* log) { m_log = log; }

    void init(unsigned int channels, unsigned int rate);
    void play(int16_t* buf, size_t samples_count);
    void cleanup();

    bool isInitialized() const { return m_device_id > 0; }

private:
    ChiakiLog* m_log = nullptr;
    SDL_AudioDeviceID m_device_id = 0;
};

#endif // AKIRA_IO_AUDIO_MANAGER_HPP
