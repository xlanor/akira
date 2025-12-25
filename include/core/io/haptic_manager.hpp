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

    // Set rumble strength multiplier (0.0 = disabled, 0.5 = weak, 1.0 = strong)
    void setRumbleStrength(float strength) { m_rumble_strength = strength; }

    bool isLocked() const { return m_haptic_lock; }

private:
    void setHapticRumble(uint8_t left, uint8_t right);
    void cleanupHaptic();

    ChiakiLog* m_log = nullptr;

    // Rumble strength multiplier (0.0 = disabled, 0.5 = weak, 1.0 = strong)
    float m_rumble_strength = 1.0f;

    // Haptic state
    bool m_haptic_lock = false;
    int m_haptic_val = 0;
    std::chrono::system_clock::time_point m_haptic_lock_time;
};

#endif // AKIRA_IO_HAPTIC_MANAGER_HPP
