#include "core/io/input_manager.hpp"
#include "core/settings_manager.hpp"
#include "core/swipe_direction.hpp"
#include <borealis.hpp>
#include <chiaki/controller.h>
#include <cmath>
#include <algorithm>
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
    state->l2_state = 0x00;
    state->r2_state = 0x00;

    const ButtonMapping& mapping = SettingsManager::getInstance()->getButtonMapping();

    u64 consumedButtons = 0;
    for (const auto& [chiakiBtn, combo] : mapping) {
        if (combo.size() <= 1) continue;
        bool allHeld = true;
        for (uint64_t hidBtn : combo) {
            if (!(buttons & hidBtn)) { allHeld = false; break; }
        }
        if (allHeld) {
            for (uint64_t hidBtn : combo)
                consumedButtons |= hidBtn;
        }
    }
    static constexpr uint32_t swipeConstants[4] = {
        SWIPE_TOUCHPAD_UP, SWIPE_TOUCHPAD_DOWN,
        SWIPE_TOUCHPAD_LEFT, SWIPE_TOUCHPAD_RIGHT
    };
    for (int i = 0; i < 4; i++) {
        if (m_swipes[i].phase == SyntheticSwipe::Phase::ACTIVE) {
            auto it = mapping.find(swipeConstants[i]);
            if (it != mapping.end()) {
                for (uint64_t hidBtn : it->second)
                    consumedButtons |= hidBtn;
            }
        }
    }

    u64 dpadButtons = buttons & ~consumedButtons;
    if (dpadButtons & HidNpadButton_Left)  state->buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
    if (dpadButtons & HidNpadButton_Right) state->buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
    if (dpadButtons & HidNpadButton_Up)    state->buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
    if (dpadButtons & HidNpadButton_Down)  state->buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;

    for (const auto& [chiakiBtn, combo] : mapping) {
        if (combo.empty()) continue;
        if (chiakiBtn & 0xFF000000) continue;

        if (combo.size() == 1 && (consumedButtons & combo[0]))
            continue;

        bool allHeld = true;
        for (uint64_t hidBtn : combo) {
            if (!(buttons & hidBtn)) { allHeld = false; break; }
        }

        if (allHeld) {
            if (chiakiBtn == CHIAKI_CONTROLLER_ANALOG_BUTTON_L2) {
                state->l2_state = 0xff;
            } else if (chiakiBtn == CHIAKI_CONTROLLER_ANALOG_BUTTON_R2) {
                state->r2_state = 0xff;
            } else {
                state->buttons |= chiakiBtn;
            }
        }
    }


    HidAnalogStickState left = padGetStickPos(&m_pad, 0);
    HidAnalogStickState right = padGetStickPos(&m_pad, 1);

    static constexpr u64 leftStickDirs = HidNpadButton_StickLUp | HidNpadButton_StickLDown
                                       | HidNpadButton_StickLLeft | HidNpadButton_StickLRight;
    static constexpr u64 rightStickDirs = HidNpadButton_StickRUp | HidNpadButton_StickRDown
                                        | HidNpadButton_StickRLeft | HidNpadButton_StickRRight;

    if (consumedButtons & leftStickDirs) {
        state->left_x = 0;
        state->left_y = 0;
    } else {
        state->left_x = left.x;
        state->left_y = -left.y;
    }

    if (consumedButtons & rightStickDirs) {
        state->right_x = 0;
        state->right_y = 0;
    } else {
        state->right_x = right.x;
        state->right_y = -right.y;
    }

    readTouchScreen(state, finger_id_touch_id);
    updateSyntheticSwipes(state, buttons);

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

    const float trackpadMaxX = m_is_ps5 ? 1919.0f : 1920.0f;
    const float trackpadMaxY = m_is_ps5 ? 1079.0f : 942.0f;

    for (int i = 0; i < sw_state.count; i++)
    {
        uint16_t x = sw_state.touches[i].x * (trackpadMaxX / (float)SWITCH_TOUCHSCREEN_MAX_X);
        uint16_t y = sw_state.touches[i].y * (trackpadMaxY / (float)SWITCH_TOUCHSCREEN_MAX_Y);

        // Use nintendo switch border's 5% to trigger the touchpad button
        if (x <= (trackpadMaxX * 0.05f) || x >= (trackpadMaxX * 0.95f) ||
            y <= (trackpadMaxY * 0.05f) || y >= (trackpadMaxY * 0.95f))
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

void InputManager::updateSyntheticSwipes(ChiakiControllerState* state, u64 buttons)
{
    static constexpr uint32_t swipeConstants[4] = {
        SWIPE_TOUCHPAD_UP, SWIPE_TOUCHPAD_DOWN,
        SWIPE_TOUCHPAD_LEFT, SWIPE_TOUCHPAD_RIGHT
    };

    const int16_t padMaxX = m_is_ps5 ? 1919 : 1920;
    const int16_t padMaxY = m_is_ps5 ? 1079 : 942;

    const int16_t dyStep = (padMaxY + SyntheticSwipe::SWIPE_FRAMES - 1) / SyntheticSwipe::SWIPE_FRAMES;
    const int16_t dxStep = (padMaxX + SyntheticSwipe::SWIPE_FRAMES - 1) / SyntheticSwipe::SWIPE_FRAMES;

    struct SwipeConfig {
        int16_t startX, startY;
        int16_t dx, dy;
    };
    SwipeConfig configs[4] = {
        {(int16_t)(padMaxX / 2), padMaxY,  0, (int16_t)-dyStep},
        {(int16_t)(padMaxX / 2), 0,        0, dyStep},
        {padMaxX, (int16_t)(padMaxY / 2), (int16_t)-dxStep, 0},
        {0, (int16_t)(padMaxY / 2),        dxStep, 0},
    };

    const ButtonMapping& mapping = SettingsManager::getInstance()->getButtonMapping();

    HidAnalogStickState leftStick = padGetStickPos(&m_pad, 0);
    HidAnalogStickState rightStick = padGetStickPos(&m_pad, 1);

    for (int i = 0; i < 4; i++) {
        SyntheticSwipe& swipe = m_swipes[i];
        auto it = mapping.find(swipeConstants[i]);
        if (it == mapping.end() || it->second.empty()) {
            swipe.buttonWasPressed = false;
            continue;
        }

        bool comboHeld = true;
        for (uint64_t hidBtn : it->second) {
            if (!(buttons & hidBtn)) {
                comboHeld = false;
                break;
            }
        }

        bool stickDirectionValid = true;
        if (comboHeld) {
            for (uint64_t hidBtn : it->second) {
                if (hidBtn == HidNpadButton_StickRUp || hidBtn == HidNpadButton_StickRDown) {
                    if (abs(rightStick.x) > abs(rightStick.y)) { stickDirectionValid = false; break; }
                } else if (hidBtn == HidNpadButton_StickRLeft || hidBtn == HidNpadButton_StickRRight) {
                    if (abs(rightStick.y) > abs(rightStick.x)) { stickDirectionValid = false; break; }
                } else if (hidBtn == HidNpadButton_StickLUp || hidBtn == HidNpadButton_StickLDown) {
                    if (abs(leftStick.x) > abs(leftStick.y)) { stickDirectionValid = false; break; }
                } else if (hidBtn == HidNpadButton_StickLLeft || hidBtn == HidNpadButton_StickLRight) {
                    if (abs(leftStick.y) > abs(leftStick.x)) { stickDirectionValid = false; break; }
                }
            }
        }

        if (swipe.phase == SyntheticSwipe::Phase::IDLE) {
            if (comboHeld && stickDirectionValid && !swipe.buttonWasPressed) {
                swipe.curX = configs[i].startX;
                swipe.curY = configs[i].startY;
                swipe.dx = configs[i].dx;
                swipe.dy = configs[i].dy;
                swipe.touchId = chiaki_controller_state_start_touch(state, swipe.curX, swipe.curY);
                if (swipe.touchId >= 0) {
                    swipe.phase = SyntheticSwipe::Phase::ACTIVE;
                    swipe.frameCounter = 0;
                }
            }
            swipe.buttonWasPressed = comboHeld && stickDirectionValid;
        } else {
            swipe.frameCounter++;
            if (swipe.frameCounter >= SyntheticSwipe::SWIPE_FRAMES) {
                chiaki_controller_state_stop_touch(state, (uint8_t)swipe.touchId);
                swipe.phase = SyntheticSwipe::Phase::IDLE;
                swipe.touchId = -1;
                swipe.buttonWasPressed = comboHeld && stickDirectionValid;
            } else {
                swipe.curX = (int16_t)std::clamp((int)(swipe.curX + swipe.dx), 0, (int)padMaxX);
                swipe.curY = (int16_t)std::clamp((int)(swipe.curY + swipe.dy), 0, (int)padMaxY);
                chiaki_controller_state_set_touch_pos(state, (uint8_t)swipe.touchId, swipe.curX, swipe.curY);
            }
        }
    }
}
