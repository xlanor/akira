#include "core/io/haptic_manager.hpp"
#include "core/settings_manager.hpp"
#include <algorithm>
#include <cstring>
#include <borealis.hpp>

HapticManager::HapticManager()
{
}

HapticManager::~HapticManager()
{
}

void HapticManager::setRumble(uint8_t left, uint8_t right)
{
    if (SettingsManager::getInstance()->getDebugChiakiLog() && (left > 0 || right > 0))
        brls::Logger::info("setRumble: left={}, right={}, strength={:.2f}", left, right, m_rumble_strength);

    // Convert uint8 (0-255) to float (0.0-1.0), apply strength multiplier
    float leftAmp = ((float)left / 255.0f) * m_rumble_strength;
    float rightAmp = ((float)right / 255.0f) * m_rumble_strength;

    // Convert back to unsigned short (0-65535) for Borealis
    unsigned short lowFreqMotor = (unsigned short)(leftAmp * 65535.0f);
    unsigned short highFreqMotor = (unsigned short)(rightAmp * 65535.0f);

    auto* inputMgr = brls::Application::getPlatform()->getInputManager();
    inputMgr->sendRumble(0, lowFreqMotor, highFreqMotor);
}

void HapticManager::processHapticAudio(uint8_t* buf, size_t buf_size)
{
    int16_t amplitudel = 0, amplituder = 0;
    int32_t suml = 0, sumr = 0;
    const size_t sample_size = 2 * sizeof(int16_t);

    size_t buf_count = buf_size / sample_size;
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

void HapticManager::setHapticRumble(uint8_t left, uint8_t right)
{
    uint8_t val = left > right ? left : right;
    m_haptic_val = val;
    m_haptic_lock_time = std::chrono::high_resolution_clock::now();

    // Calculate amplitude based on haptic base frequency
    float amplitude = (float)val / (float)hapticBase;
    if (amplitude > 1.0f) amplitude = 1.0f;

    // Apply rumble strength multiplier
    amplitude *= m_rumble_strength;

    // Convert amplitude (0.0-1.0) to unsigned short (0-65535) for sendRumble API
    unsigned short motorValue = (unsigned short)(amplitude * 65535.0f);

    // Use Borealis InputManager
    auto* inputMgr = brls::Application::getPlatform()->getInputManager();
    inputMgr->sendRumble(0, motorValue, motorValue);
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
        setHapticRumble(0, 0);
        m_haptic_lock = false;
    }
}

void HapticManager::cleanup()
{
    // Stop any active vibration
    setHapticRumble(0, 0);
    m_haptic_lock = false;
    m_haptic_val = 0;
}
