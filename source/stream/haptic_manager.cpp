#include "stream/haptic_manager.hpp"
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

    if (left > 160) left = 160;
    if (right > 160) right = 160;

    float leftAmp = (left > 0) ? ((float)left / 255.0f) * m_rumble_strength : 0.0f;
    float rightAmp = (right > 0) ? ((float)right / 255.0f) * m_rumble_strength : 0.0f;
    float amp = leftAmp > rightAmp ? leftAmp : rightAmp;

    auto* inputMgr = brls::Application::getPlatform()->getInputManager();
    inputMgr->sendRumbleRaw(0, m_freq_low, m_freq_high, amp, amp);
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

    float amplitude = (float)val / (float)hapticBase;
    if (amplitude > 1.0f) amplitude = 1.0f;
    amplitude *= m_rumble_strength;

    if (amplitude >= m_envelope)
        m_envelope = m_envelope * (1.0f - m_envelope_attack) + amplitude * m_envelope_attack;
    else
        m_envelope = m_envelope * m_envelope_decay + amplitude * (1.0f - m_envelope_decay);

    auto* inputMgr = brls::Application::getPlatform()->getInputManager();
    inputMgr->sendRumbleRaw(0, m_freq_low, m_freq_high, m_envelope, m_envelope);
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
        auto* inputMgr = brls::Application::getPlatform()->getInputManager();
        inputMgr->sendRumbleRaw(0, 0.0f, 0.0f, 0.0f, 0.0f);
    }
}

void HapticManager::cleanup()
{
    m_envelope = 0.0f;
    m_haptic_lock = false;
    m_haptic_val = 0;
    auto* inputMgr = brls::Application::getPlatform()->getInputManager();
    inputMgr->sendRumbleRaw(0, 0.0f, 0.0f, 0.0f, 0.0f);
}
