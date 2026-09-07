#ifndef AKIRA_IO_INPUT_MANAGER_HPP
#define AKIRA_IO_INPUT_MANAGER_HPP

#include <SDL2/SDL.h>
#include <cstdint>
#include <map>
#include <chiaki/controller.h>
#include <chiaki/log.h>
#include <switch.h>

#include "input/extended_input_manager.hpp"
#include "input/pad_path.hpp"

#include <memory>
#include <vector>

#define SDL_JOYSTICK_COUNT 2

// Trackpad and touchscreen dimensions for coordinate mapping
#define DS4_TRACKPAD_MAX_X 1920
#define DS4_TRACKPAD_MAX_Y 942
#define SWITCH_TOUCHSCREEN_MAX_X 1280
#define SWITCH_TOUCHSCREEN_MAX_Y 720

struct SyntheticSwipe {
    enum class Phase { IDLE, ACTIVE };
    Phase phase = Phase::IDLE;
    int8_t touchId = -1;
    int16_t curX, curY;
    int16_t dx, dy;
    int frameCounter = 0;
    bool buttonWasPressed = false;
    static constexpr int SWIPE_FRAMES = 18;
};

struct PendingBorderTap {
    uint16_t down_x;
    uint16_t down_y;
    int frame_count;
    static constexpr int TAP_COMMIT_FRAMES = 4;
    static constexpr int TAP_MAX_MOVE = 80;
    static constexpr int TAP_BUTTON_HOLD_FRAMES = 12;
    static constexpr int TAP_BUTTON_DELAY_FRAMES = 4;
};

class InputManager
{
public:
    InputManager();
    ~InputManager();

    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    void setLogger(ChiakiLog* log) { m_log = log; }
    void setTargetPS5(bool ps5) { m_is_ps5 = ps5; }

    bool init();
    void cleanup();
    void update(ChiakiControllerState* state, std::map<uint32_t, int8_t>* finger_id_touch_id);

    /*
     * The one pad everything reads from. Swapping it is how a controller is
     * chosen; until something calls this, the default path reproduces the
     * behaviour Akira had before paths existed.
     */
    void setPath(std::unique_ptr<akira::input::PadPath> path);
    akira::input::PadPath* path() { return m_path.get(); }

    /*
     * Bind to the pad the user picked. Everything the stream reads follows from
     * this - buttons, sticks, gyro, triggers, touch and rumble - so it is the
     * one decision that has to be made explicitly rather than inferred.
     *
     * Falls back to the default path if the npad no longer resolves, which is
     * the case where the pad was switched off between picking and starting.
     */
    void selectNpad(HidNpadIdType npad);

    /* What the picker offers. Requires the backend to have been subscribed for
     * a moment, so it is worth re-reading while the picker is on screen rather
     * than once. */
    std::vector<akira::input::PadDescription> describePads();

    ExtendedInputManager& extendedInput() { return m_extended; }

private:
    /*
     * A pad the backend had not identified yet, picked up once it has.
     *
     * A DualSense is only known to be one after the sysmodule has seen a report
     * from it, and reports only flow once Akira has subscribed - which it does
     * moments before a stream starts. Start quickly enough and the pad is still
     * anonymous when the path is chosen, so it gets the generic MissionControl
     * path and keeps it for the whole session: no analog triggers, no direct
     * rumble, no touchpad. Opening the overlay first happened to warm the
     * backend, which is why it looked like the overlay was required.
     *
     * Identity only ever improves, so re-checking costs one IPC call a second
     * and stops for good the moment it upgrades or the pad turns out to be
     * something else.
     */
    void retryPathIdentification();

    /*
     * Rebuild the path when the driver changes under it.
     *
     * retryPathIdentification only ever upgrades, and stops for good once it
     * has. The overlay toggle moves in both directions and can move during a
     * stream, so the downgrade needs a check that does not retire itself.
     */
    void reconcilePathDriver();

    bool readTouchScreen(ChiakiControllerState* state, std::map<uint32_t, int8_t>* finger_id_touch_id);
    bool readSixAxis(ChiakiControllerState* state);
    void updateSyntheticSwipes(ChiakiControllerState* state, u64 buttons);

    bool m_is_ps5 = false;
    ChiakiLog* m_log = nullptr;
    SDL_Joystick* m_sdl_joystick_ptr[SDL_JOYSTICK_COUNT] = {nullptr};

    ExtendedInputManager m_extended;

    std::unique_ptr<akira::input::PadPath> m_path;

    /* kNoNpad means nothing was picked explicitly, so any pad that identifies
     * as one we drive directly is fair game. */
    static constexpr HidNpadIdType kNoNpad = (HidNpadIdType)0xff;
    HidNpadIdType m_bound_npad     = kNoNpad;
    bool          m_identify_done  = false;
    uint32_t      m_identify_next  = 0;
    uint32_t      m_identify_until = 0;
    uint32_t      m_driver_next    = 0;
    int m_sixaxis_frame_counter = 0;



    SyntheticSwipe m_swipes[4];

    std::map<uint32_t, PendingBorderTap> m_pending_border_taps;
    int m_touchpad_button_hold = 0;
    void fireDeferredRelease(ChiakiControllerState* state);

    int8_t m_deferred_release_touch_id = -1;
    int8_t m_active_click_touch_id = -1;

    int m_prev_touch_count = 0;
    uint32_t m_touch_debug_counter = 0;

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
