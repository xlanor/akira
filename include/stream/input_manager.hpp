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
#include <functional>

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

    void readSecondaryPads(ChiakiControllerState* states, uint8_t max_pads, uint8_t& count);

    static constexpr uint8_t kNoCouchSlot = 0xff;

    uint8_t couchJoin(HidNpadIdType npad);
    void couchLeave(HidNpadIdType npad);
    const std::map<HidNpadIdType, uint8_t>& couchRoster() const { return m_couch_roster; }
    uint8_t couchPadCount() const;

    uint8_t CouchPadIndexForController(HidNpadIdType npad);

    bool couchSlotOutputTarget(uint8_t slot, uint8_t out_addr[6], uint16_t* vendor_id, uint16_t* product_id);

    void beginCouchJoin();
    void cancelCouchJoin();
    void tickCouchClaim() { pollCouchClaim(); }
    bool couchClaimActive() const { return m_couch_claim_active; }
    float couchClaimProgress() const;
    void setOnCouchJoined(std::function<void(HidNpadIdType, uint8_t)> cb) { m_on_couch_joined = std::move(cb); }
    void setOnCouchDisconnected(std::function<void(HidNpadIdType, uint8_t)> cb) { m_on_couch_disconnected = std::move(cb); }
    void setOnPadArrived(std::function<void(HidNpadIdType)> cb) { m_on_pad_arrived = std::move(cb); }
    void promptControllerSetup();
    bool isClaimableNpad(HidNpadIdType npad) const;
    HidNpadIdType boundNpad() const { return m_bound_npad; }
    void resolvePadArrival() { m_arrival_pending = false; }

    void setPath(std::unique_ptr<akira::input::PadPath> path);
    akira::input::PadPath* path() { return m_path.get(); }

    void selectNpad(HidNpadIdType npad);

    std::vector<akira::input::PadDescription> describePads();

    ExtendedInputManager& extendedInput() { return m_extended; }

private:
    void retryPathIdentification();

    void reconcilePathDriver();

    void pollCouchClaim();

    void pollPadArrivals();

    uint64_t couchNpadMask() const;

    void applyPadInput(akira::input::PadPath* path, ChiakiControllerState* state);

    bool readTouchScreen(ChiakiControllerState* state, std::map<uint32_t, int8_t>* finger_id_touch_id);
    bool readSixAxis(ChiakiControllerState* state);
    void updateSyntheticSwipes(ChiakiControllerState* state, u64 buttons);

    bool m_is_ps5 = false;
    ChiakiLog* m_log = nullptr;
    SDL_Joystick* m_sdl_joystick_ptr[SDL_JOYSTICK_COUNT] = {nullptr};

    ExtendedInputManager m_extended;

    std::unique_ptr<akira::input::PadPath> m_path;

    static constexpr HidNpadIdType kNoNpad = (HidNpadIdType)0xff;
    HidNpadIdType m_bound_npad     = kNoNpad;
    std::map<HidNpadIdType, uint8_t> m_couch_roster;
    std::map<HidNpadIdType, std::unique_ptr<akira::input::PadPath>> m_secondary_paths;
    std::map<HidNpadIdType, uint8_t> m_secondary_missing_frames;
    bool m_couch_claim_active = false;
    std::map<HidNpadIdType, int> m_claim_hold;
    std::function<void(HidNpadIdType, uint8_t)> m_on_couch_joined;
    std::function<void(HidNpadIdType, uint8_t)> m_on_couch_disconnected;
    std::function<void(HidNpadIdType)> m_on_pad_arrived;
    uint32_t m_arrival_tick = 0;
    uint32_t m_connected_mask = 0;
    uint32_t m_announced_mask = 0;
    bool     m_arrival_primed = false;
    bool     m_arrival_pending = false;
    static constexpr uint32_t kArrivalPollFrames = 30;
    static constexpr int kClaimHoldFrames = 40;
    static constexpr uint8_t kDisconnectGraceFrames = 30;
    bool          m_identify_done  = false;
    uint32_t      m_identify_next  = 0;
    uint32_t      m_identify_until = 0;
    uint32_t      m_driver_next    = 0;
    int m_sixaxis_frame_counter = 0;



    SyntheticSwipe m_swipes[4];

    std::map<uint32_t, PendingBorderTap> m_pending_border_taps;
    int m_touchpad_button_hold = 0;
    void fireDeferredRelease(ChiakiControllerState* state);

    int64_t m_overlay_drag_finger = -1;
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
