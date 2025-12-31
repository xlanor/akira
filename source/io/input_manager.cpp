#include "core/io/input_manager.hpp"
#include "core/settings_manager.hpp"
#include <borealis.hpp>
#include <cmath>
#include <array>

InputManager::InputManager()
{
}

InputManager::~InputManager()
{
    cleanup();
}

bool InputManager::init()
{
    for (int i = 0; i < SDL_JOYSTICK_COUNT; i++)
    {
        m_sdl_joystick_ptr[i] = SDL_JoystickOpen(i);
        if (m_sdl_joystick_ptr[i] == nullptr)
        {
            brls::Logger::error("SDL_JoystickOpen: {}", SDL_GetError());
            return false;
        }
    }

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&m_pad);
    hidInitializeTouchScreen();

    hidGetSixAxisSensorHandles(&m_sixaxis_handles[0], 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
    hidGetSixAxisSensorHandles(&m_sixaxis_handles[1], 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey);
    hidGetSixAxisSensorHandles(&m_sixaxis_handles[2], 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual);
    hidStartSixAxisSensor(m_sixaxis_handles[0]);
    hidStartSixAxisSensor(m_sixaxis_handles[1]);
    hidStartSixAxisSensor(m_sixaxis_handles[2]);
    hidStartSixAxisSensor(m_sixaxis_handles[3]);

    return true;
}

void InputManager::cleanup()
{
    for (int i = 0; i < SDL_JOYSTICK_COUNT; i++)
    {
        if (m_sdl_joystick_ptr[i])
        {
            SDL_JoystickClose(m_sdl_joystick_ptr[i]);
            m_sdl_joystick_ptr[i] = nullptr;
        }
    }

    hidStopSixAxisSensor(m_sixaxis_handles[0]);
    hidStopSixAxisSensor(m_sixaxis_handles[1]);
    hidStopSixAxisSensor(m_sixaxis_handles[2]);
    hidStopSixAxisSensor(m_sixaxis_handles[3]);
}

void InputManager::update(ChiakiControllerState* state, std::map<uint32_t, int8_t>* finger_id_touch_id)
{
    padUpdate(&m_pad);

    u64 buttons = padGetButtons(&m_pad);

    state->buttons = 0;

    bool invertAB = SettingsManager::getInstance()->getInvertAB();
    if (invertAB) {
        if (buttons & HidNpadButton_A) state->buttons |= CHIAKI_CONTROLLER_BUTTON_CROSS;     // A -> Cross (inverted)
        if (buttons & HidNpadButton_B) state->buttons |= CHIAKI_CONTROLLER_BUTTON_MOON;      // B -> Circle (inverted)
    } else {
        if (buttons & HidNpadButton_A) state->buttons |= CHIAKI_CONTROLLER_BUTTON_MOON;      // A -> Circle
        if (buttons & HidNpadButton_B) state->buttons |= CHIAKI_CONTROLLER_BUTTON_CROSS;     // B -> Cross
    }
    if (buttons & HidNpadButton_X) state->buttons |= CHIAKI_CONTROLLER_BUTTON_PYRAMID;   // X -> Triangle
    if (buttons & HidNpadButton_Y) state->buttons |= CHIAKI_CONTROLLER_BUTTON_BOX;       // Y -> Square

    if (buttons & HidNpadButton_Left)  state->buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
    if (buttons & HidNpadButton_Right) state->buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
    if (buttons & HidNpadButton_Up)    state->buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
    if (buttons & HidNpadButton_Down)  state->buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;

    if (buttons & HidNpadButton_L) state->buttons |= CHIAKI_CONTROLLER_BUTTON_L1;
    if (buttons & HidNpadButton_R) state->buttons |= CHIAKI_CONTROLLER_BUTTON_R1;

    state->l2_state = (buttons & HidNpadButton_ZL) ? 0xff : 0x00;
    state->r2_state = (buttons & HidNpadButton_ZR) ? 0xff : 0x00;

    if (buttons & HidNpadButton_StickL) state->buttons |= CHIAKI_CONTROLLER_BUTTON_L3;
    if (buttons & HidNpadButton_StickR) state->buttons |= CHIAKI_CONTROLLER_BUTTON_R3;

    if (buttons & HidNpadButton_Plus)  state->buttons |= CHIAKI_CONTROLLER_BUTTON_OPTIONS;
    if (buttons & HidNpadButton_Minus) state->buttons |= CHIAKI_CONTROLLER_BUTTON_PS;

    HidAnalogStickState left = padGetStickPos(&m_pad, 0);
    HidAnalogStickState right = padGetStickPos(&m_pad, 1);
    state->left_x = left.x;
    state->left_y = -left.y;
    state->right_x = right.x;
    state->right_y = -right.y;

    readTouchScreen(state, finger_id_touch_id);

    // Throttle six-axis sensor reading to every 3 frames
    // hidGetSixAxisSensorStates() can block and cause video blackout
    if (++m_sixaxis_frame_counter >= 3) {
        m_sixaxis_frame_counter = 0;
        readSixAxis(state);
    }
}

bool InputManager::readTouchScreen(ChiakiControllerState* chiaki_state, std::map<uint32_t, int8_t>* finger_id_touch_id)
{
    HidTouchScreenState sw_state = {0};

    bool ret = false;
    hidGetTouchScreenStates(&sw_state, 1);
    chiaki_state->buttons &= ~CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;

    // Un-touch all old touches
    for (auto it = finger_id_touch_id->begin(); it != finger_id_touch_id->end();)
    {
        auto cur = it;
        it++;
        bool found = false;
        for (int i = 0; i < sw_state.count; i++)
        {
            if (sw_state.touches[i].finger_id == cur->first)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            if (cur->second >= 0)
            {
                chiaki_controller_state_stop_touch(chiaki_state, (uint8_t)cur->second);
            }
            finger_id_touch_id->erase(cur);
        }
    }

    for (int i = 0; i < sw_state.count; i++)
    {
        uint16_t x = sw_state.touches[i].x * ((float)DS4_TRACKPAD_MAX_X / (float)SWITCH_TOUCHSCREEN_MAX_X);
        uint16_t y = sw_state.touches[i].y * ((float)DS4_TRACKPAD_MAX_Y / (float)SWITCH_TOUCHSCREEN_MAX_Y);

        // Use nintendo switch border's 5% to trigger the touchpad button
        if (x <= (DS4_TRACKPAD_MAX_X * 0.05) || x >= (DS4_TRACKPAD_MAX_X * 0.95) ||
            y <= (DS4_TRACKPAD_MAX_Y * 0.05) || y >= (DS4_TRACKPAD_MAX_Y * 0.95))
        {
            chiaki_state->buttons |= CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
        }

        auto it = finger_id_touch_id->find(sw_state.touches[i].finger_id);
        if (it == finger_id_touch_id->end())
        {
            (*finger_id_touch_id)[sw_state.touches[i].finger_id] =
                chiaki_controller_state_start_touch(chiaki_state, x, y);
        }
        else if (it->second >= 0)
        {
            chiaki_controller_state_set_touch_pos(chiaki_state, (uint8_t)it->second, x, y);
        }
        ret = true;
    }
    return ret;
}

bool InputManager::readSixAxis(ChiakiControllerState* state)
{
    HidSixAxisSensorState sixaxis = {0};
    uint64_t style_set = padGetStyleSet(&m_pad);

    if (style_set & HidNpadStyleTag_NpadHandheld)
    {
        hidGetSixAxisSensorStates(m_sixaxis_handles[0], &sixaxis, 1);
    }
    else if (style_set & HidNpadStyleTag_NpadFullKey)
    {
        hidGetSixAxisSensorStates(m_sixaxis_handles[1], &sixaxis, 1);
    }
    else if (style_set & HidNpadStyleTag_NpadJoyDual)
    {
        u64 attrib = padGetAttributes(&m_pad);
        GyroSource gyroSource = SettingsManager::getInstance()->getGyroSource();

        bool leftConnected = attrib & HidNpadAttribute_IsLeftConnected;
        bool rightConnected = attrib & HidNpadAttribute_IsRightConnected;

        bool useLeft = false;
        bool useRight = false;

        switch (gyroSource) {
            case GyroSource::Left:
                useLeft = leftConnected;
                break;
            case GyroSource::Right:
                useRight = rightConnected;
                break;
            case GyroSource::Auto:
            default:
                if (leftConnected)
                    useLeft = true;
                else if (rightConnected)
                    useRight = true;
                break;
        }

        if (useLeft)
            hidGetSixAxisSensorStates(m_sixaxis_handles[2], &sixaxis, 1);
        else if (useRight)
            hidGetSixAxisSensorStates(m_sixaxis_handles[3], &sixaxis, 1);
    }

    state->gyro_x = sixaxis.angular_velocity.x * 2.0f * M_PI;
    state->gyro_y = sixaxis.angular_velocity.z * 2.0f * M_PI;
    state->gyro_z = -sixaxis.angular_velocity.y * 2.0f * M_PI;

    m_raw_accel_x = -sixaxis.acceleration.x;
    m_raw_accel_y = -sixaxis.acceleration.z;
    m_raw_accel_z = sixaxis.acceleration.y;

    state->accel_x = m_raw_accel_x - m_accel_zero_x;
    state->accel_y = m_raw_accel_y - m_accel_zero_y;
    state->accel_z = m_raw_accel_z - m_accel_zero_z;

    // Convert rotation matrix to quaternion
    float (*dm)[3] = sixaxis.direction.direction;
    float m[3][3] = {
        {dm[0][0], dm[2][0], dm[1][0]},
        {dm[0][2], dm[2][2], dm[1][2]},
        {dm[0][1], dm[2][1], dm[1][1]}
    };
    std::array<float, 4> q;
    float t;
    if (m[2][2] < 0)
    {
        if (m[0][0] > m[1][1])
        {
            t = 1 + m[0][0] - m[1][1] - m[2][2];
            q = {t, m[0][1] + m[1][0], m[2][0] + m[0][2], m[1][2] - m[2][1]};
        }
        else
        {
            t = 1 - m[0][0] + m[1][1] - m[2][2];
            q = {m[0][1] + m[1][0], t, m[1][2] + m[2][1], m[2][0] - m[0][2]};
        }
    }
    else
    {
        if (m[0][0] < -m[1][1])
        {
            t = 1 - m[0][0] - m[1][1] + m[2][2];
            q = {m[2][0] + m[0][2], m[1][2] + m[2][1], t, m[0][1] - m[1][0]};
        }
        else
        {
            t = 1 + m[0][0] + m[1][1] + m[2][2];
            q = {m[1][2] - m[2][1], m[2][0] - m[0][2], m[0][1] - m[1][0], t};
        }
    }
    float fac = 0.5f / sqrt(t);
    state->orient_x = q[0] * fac;
    state->orient_y = q[1] * fac;
    state->orient_z = -q[2] * fac;
    state->orient_w = q[3] * fac;
    return true;
}

void InputManager::resetMotionControls()
{
    m_accel_zero_x = m_raw_accel_x;
    m_accel_zero_y = m_raw_accel_y - 1.0f;
    m_accel_zero_z = m_raw_accel_z;

    for (int i = 0; i < 4; i++) {
        hidResetSixAxisSensorFusionParameters(m_sixaxis_handles[i]);
    }

    brls::Logger::info("Motion controls reset: zero offset = ({}, {}, {})",
        m_accel_zero_x, m_accel_zero_y, m_accel_zero_z);
}
