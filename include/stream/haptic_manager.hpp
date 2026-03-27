#ifndef AKIRA_IO_HAPTIC_MANAGER_HPP
#define AKIRA_IO_HAPTIC_MANAGER_HPP

#include <cstdint>
#include <chrono>
#include <chiaki/log.h>

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

    void setRumbleStrength(float strength) { m_rumble_strength = strength; }
    void setRumbleFreqLow(float freq) { m_freq_low = freq; }
    void setRumbleFreqHigh(float freq) { m_freq_high = freq; }

    bool isLocked() const { return m_haptic_lock; }

private:
    void setHapticRumble(uint8_t left, uint8_t right);
    void cleanupHaptic();

    ChiakiLog* m_log = nullptr;

    float m_rumble_strength = 1.0f;
    float m_freq_low = 160.0f;
    float m_freq_high = 200.0f;

    // Haptic state
    bool m_haptic_lock = false;
    int m_haptic_val = 0;
    std::chrono::system_clock::time_point m_haptic_lock_time;
};

#endif // AKIRA_IO_HAPTIC_MANAGER_HPP
