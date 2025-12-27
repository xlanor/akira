#ifndef AKIRA_IO_INPUT_MANAGER_HPP
#define AKIRA_IO_INPUT_MANAGER_HPP

#include <SDL2/SDL.h>
#include <cstdint>
#include <map>
#include <chiaki/controller.h>
#include <chiaki/log.h>
#include <switch.h>

#define SDL_JOYSTICK_COUNT 2

// Trackpad and touchscreen dimensions for coordinate mapping
#define DS4_TRACKPAD_MAX_X 1920
#define DS4_TRACKPAD_MAX_Y 942
#define SWITCH_TOUCHSCREEN_MAX_X 1280
#define SWITCH_TOUCHSCREEN_MAX_Y 720

class InputManager
{
public:
    InputManager();
    ~InputManager();

    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    void setLogger(ChiakiLog* log) { m_log = log; }

    bool init();
    void cleanup();
    void update(ChiakiControllerState* state, std::map<uint32_t, int8_t>* finger_id_touch_id);

    PadState* getPad() { return &m_pad; }

private:
    bool readTouchScreen(ChiakiControllerState* state, std::map<uint32_t, int8_t>* finger_id_touch_id);
    bool readSixAxis(ChiakiControllerState* state);

    ChiakiLog* m_log = nullptr;
    SDL_Joystick* m_sdl_joystick_ptr[SDL_JOYSTICK_COUNT] = {nullptr};

    PadState m_pad;
    HidSixAxisSensorHandle m_sixaxis_handles[4];
    int m_sixaxis_frame_counter = 0;

    // Accelerometer zero offset for gyro reset
    float m_accel_zero_x = 0.0f;
    float m_accel_zero_y = 0.0f;
    float m_accel_zero_z = 0.0f;

    // Current raw accel values (before offset applied)
    float m_raw_accel_x = 0.0f;
    float m_raw_accel_y = 0.0f;
    float m_raw_accel_z = 0.0f;

public:
    void resetMotionControls();
};

#endif // AKIRA_IO_INPUT_MANAGER_HPP
