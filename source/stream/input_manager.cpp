#include "stream/input_manager.hpp"
#include "stream/session.hpp"
#include "stream/video_renderer.hpp"
#include "core/settings_manager.hpp"
#include "core/swipe_direction.hpp"
#include "input/ps_output.hpp"
#include <borealis.hpp>
#include <chiaki/controller.h>
#include <cmath>
#include <cstring>
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
    bool vibration_permitted = true;
    if (R_SUCCEEDED(hidIsVibrationPermitted(&vibration_permitted)) && !vibration_permitted) {
        brls::Logger::warning("Controller Vibration is off in System Settings - "
                              "nothing sent through HOS will be felt");
    }

    for (int i = 0; i < SDL_JOYSTICK_COUNT; i++)
    {
        m_sdl_joystick_ptr[i] = SDL_JoystickOpen(i);
        if (m_sdl_joystick_ptr[i] == nullptr)
        {
            brls::Logger::error("SDL_JoystickOpen: {}", SDL_GetError());
            return false;
        }
    }

    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    hidInitializeTouchScreen();

    m_path = akira::input::DefaultPadPath(m_extended);

    m_extended.initializeOptional();

    if (m_bound_npad != kNoNpad)
    {
        HidNpadIdType wanted = m_bound_npad;
        m_bound_npad = kNoNpad;
        selectNpad(wanted);
    }

    return true;
}

void InputManager::cleanup()
{
    m_extended.shutdown();

    for (int i = 0; i < SDL_JOYSTICK_COUNT; i++)
    {
        if (m_sdl_joystick_ptr[i])
        {
            SDL_JoystickClose(m_sdl_joystick_ptr[i]);
            m_sdl_joystick_ptr[i] = nullptr;
        }
    }

    m_path.reset();
    m_overlay_drag_finger = -1;
}

void InputManager::setPath(std::unique_ptr<akira::input::PadPath> path)
{
    if (!path)
        return;

    if (m_path)
        m_path->sendRumble(0.0f, 0.0f, 0.0f, 0.0f);
    m_path = std::move(path);

    brls::Logger::info("InputManager: input path is now {}", m_path->label());
}

std::vector<akira::input::PadDescription> InputManager::describePads()
{
    return akira::input::DescribePads(m_extended);
}

void InputManager::selectNpad(HidNpadIdType npad)
{
    for (const auto& desc : describePads()) {
        if (desc.npad != npad)
            continue;

        auto path = akira::input::MakePath(desc, m_extended);
        if (!path)
            break;

        m_bound_npad = npad;

        brls::Logger::info("InputManager: using {} on npad {}", path->label(), (int)npad);
        setPath(std::move(path));
        return;
    }

    brls::Logger::warning("InputManager: npad {} no longer present, falling back to default", (int)npad);
    setPath(akira::input::DefaultPadPath(m_extended));
}

void InputManager::retryPathIdentification()
{
    if (m_identify_done)
        return;

    const uint32_t now = (uint32_t)(armTicksToNs(armGetSystemTick()) / 1000000ull);

    if (m_identify_until == 0) {
        m_identify_until = now + 10000;
        m_identify_next  = now;
    }

    if (akira::input::PadTakesDirectOutput(m_path->vendorId(), m_path->productId())) {
        m_identify_done = true;
        return;
    }

    if (now > m_identify_until) {
        m_identify_done = true;
        return;
    }

    if (m_arrival_pending || m_couch_claim_active) {
        m_identify_until = now + 10000;
        m_identify_next  = now;
        return;
    }

    if (now < m_identify_next)
        return;
    m_identify_next = now + 1000;

    for (const auto& desc : describePads()) {
        if (desc.kind != akira::input::PadPathKind::McPsNative)
            continue;
        if (m_couch_roster.find(desc.npad) != m_couch_roster.end())
            continue;
        if (m_bound_npad != kNoNpad && desc.npad != m_bound_npad)
            continue;

        auto path = akira::input::MakePath(desc, m_extended);
        if (!path)
            continue;

        brls::Logger::info("InputManager: {} identified on npad {} after the path was chosen"
                           " - upgrading from {}",
                           path->label(), (int)desc.npad, m_path->label());

        m_bound_npad    = desc.npad;
        m_identify_done = true;
        setPath(std::move(path));
        return;
    }
}

void InputManager::reconcilePathDriver()
{
    if (m_bound_npad == kNoNpad)
        return;

    const uint32_t now = (uint32_t)(armTicksToNs(armGetSystemTick()) / 1000000ull);
    if (now < m_driver_next)
        return;
    m_driver_next = now + 1000;

    for (const auto& desc : describePads()) {
        if (desc.npad != m_bound_npad)
            continue;

        const akira::input::PadDriver driver =
            akira::input::ResolvePadDriver(desc, m_extended);
        const bool native_now = m_path->nativeRumble();

        if ((driver == akira::input::PadDriver::Akira) == native_now)
            return;

        auto path = akira::input::MakePath(desc, m_extended);
        if (!path)
            return;

        brls::Logger::info("InputManager: pad on npad {} is now driven by {}"
                           " - {} to {}",
                           (int)desc.npad, akira::input::PadDriverName(driver),
                           m_path->label(), path->label());
        setPath(std::move(path));
        return;
    }
}

void InputManager::applyPadInput(akira::input::PadPath* path, ChiakiControllerState* state)
{
    state->buttons = 0;
    state->l2_state = 0x00;
    state->r2_state = 0x00;

    const bool mapped = path->usesButtonMapping();

    u64 buttons = mapped ? path->heldButtons() : 0;
    u64 consumedButtons = 0;

    if (!mapped) {
        path->readButtons(state);
        path->readTriggers(state);
    } else {
    const ButtonMapping& mapping = SettingsManager::getInstance()->getButtonMapping();
    for (const auto& [chiakiBtn, combo] : mapping) {
        if (combo.size() <= 1) continue;
        if (!SettingsManager::getInstance()->isButtonEnabled(chiakiBtn))
            continue;
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
        if (!SettingsManager::getInstance()->isButtonEnabled(swipeConstants[i]))
            continue;
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
        if (!SettingsManager::getInstance()->isButtonEnabled(chiakiBtn))
            continue;

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

    path->readTriggers(state);
    }

    HidAnalogStickState left = path->stickPos(0);
    HidAnalogStickState right = path->stickPos(1);

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
}

uint8_t InputManager::couchJoin(HidNpadIdType npad)
{
    if (npad == m_bound_npad)
        return kNoCouchSlot;
    auto existing = m_couch_roster.find(npad);
    if (existing != m_couch_roster.end())
        return existing->second;
    for (uint8_t slot = 1; slot < CHIAKI_COUCH_MAX_PADS; slot++)
    {
        bool taken = false;
        for (const auto& entry : m_couch_roster)
        {
            if (entry.second == slot)
            {
                taken = true;
                break;
            }
        }
        if (!taken)
        {
            m_couch_roster[npad] = slot;
            return slot;
        }
    }
    return kNoCouchSlot;
}

uint64_t InputManager::couchNpadMask() const
{
    uint64_t mask = 0;
    for (const auto& entry : m_couch_roster)
        mask |= 1UL << (uint32_t)entry.first;
    return mask;
}

void InputManager::couchLeave(HidNpadIdType npad)
{
    m_couch_roster.erase(npad);
    m_secondary_paths.erase(npad);
    m_secondary_missing_frames.erase(npad);
}

uint8_t InputManager::couchPadCount() const
{
    uint8_t count = 1;
    for (const auto& entry : m_couch_roster)
    {
        if (entry.second + 1 > count)
            count = (uint8_t)(entry.second + 1);
    }
    return count;
}

uint8_t InputManager::CouchPadIndexForController(HidNpadIdType npad)
{
    if (npad == m_bound_npad)
        return 0;
    auto it = m_couch_roster.find(npad);
    if (it != m_couch_roster.end())
        return it->second;
    return kNoCouchSlot;
}

bool InputManager::couchSlotOutputTarget(uint8_t slot, uint8_t out_addr[6], uint16_t* vendor_id,
                                         uint16_t* product_id)
{
    if (slot == 0 || slot == kNoCouchSlot)
        return false;

    HidNpadIdType npad = kNoNpad;
    for (const auto& entry : m_couch_roster) {
        if (entry.second == slot) {
            npad = entry.first;
            break;
        }
    }
    if (npad == kNoNpad)
        return false;

    for (const auto& desc : describePads()) {
        if (desc.npad != npad)
            continue;
        if (!desc.has_address)
            return false;
        if (out_addr)
            std::memcpy(out_addr, desc.bt_addr, 6);
        if (vendor_id)
            *vendor_id = desc.vendor_id;
        if (product_id)
            *product_id = desc.product_id;
        return true;
    }
    return false;
}

void InputManager::readSecondaryPads(ChiakiControllerState* states, uint8_t max_pads, uint8_t& count)
{
    auto pads = describePads();
    std::vector<std::pair<HidNpadIdType, uint8_t>> disconnected;

    for (const auto& entry : m_couch_roster)
    {
        HidNpadIdType npad = entry.first;
        uint8_t slot = entry.second;
        if (slot == 0 || slot >= max_pads)
            continue;

        // Never retain input from a controller that is no longer readable.
        chiaki_controller_state_set_idle(&states[slot]);
        if (slot + 1 > count)
            count = (uint8_t)(slot + 1);

        const akira::input::PadDescription* found = nullptr;
        for (const auto& desc : pads)
        {
            if (desc.npad == npad)
            {
                found = &desc;
                break;
            }
        }
        if (!found)
        {
            uint8_t& misses = m_secondary_missing_frames[npad];
            if (misses < kDisconnectGraceFrames)
                misses++;
            if (misses == 1)
                brls::Logger::warning("Couch: slot {} npad {} disappeared; sending idle input",
                                      (int)slot, (int)npad);
            if (misses >= kDisconnectGraceFrames)
                disconnected.emplace_back(npad, slot);
            continue;
        }

        m_secondary_missing_frames.erase(npad);

        auto& cached = m_secondary_paths[npad];
        if (!cached)
            cached = akira::input::MakePath(*found, m_extended);
        if (!cached)
            continue;
        cached->poll();
        applyPadInput(cached.get(), &states[slot]);
    }

    for (const auto& entry : disconnected)
    {
        brls::Logger::warning("Couch: removing disconnected slot {} npad {}",
                              (int)entry.second, (int)entry.first);
        couchLeave(entry.first);
        if (m_on_couch_disconnected)
            m_on_couch_disconnected(entry.first, entry.second);
    }

    count = couchPadCount();

    for (auto it = m_secondary_paths.begin(); it != m_secondary_paths.end(); )
    {
        if (m_couch_roster.find(it->first) == m_couch_roster.end())
            it = m_secondary_paths.erase(it);
        else
            ++it;
    }
}

float InputManager::couchClaimProgress() const
{
    if (!m_couch_claim_active)
        return 0.0f;

    int best = 0;
    for (const auto& entry : m_claim_hold)
    {
        if (entry.second > best)
            best = entry.second;
    }
    if (best <= 0)
        return 0.0f;
    if (best >= kClaimHoldFrames)
        return 1.0f;
    return (float)best / (float)kClaimHoldFrames;
}

void InputManager::beginCouchJoin()
{
    m_couch_claim_active = true;
    m_claim_hold.clear();
}

void InputManager::cancelCouchJoin()
{
    m_couch_claim_active = false;
    m_arrival_pending = false;
    m_claim_hold.clear();
}

void InputManager::promptControllerSetup()
{
    HidLaControllerSupportArg arg;
    hidLaCreateControllerSupportArg(&arg);
    arg.hdr.player_count_max = CHIAKI_COUCH_MAX_PADS;
    arg.hdr.enable_single_mode = false;
    HidLaControllerSupportResultInfo result;
    hidLaShowControllerSupport(&result, &arg);
}

bool InputManager::isClaimableNpad(HidNpadIdType npad) const
{
    if (npad == m_bound_npad)
        return false;
    if ((uint32_t)npad >= 8)
        return false;
    return true;
}

void InputManager::pollPadArrivals()
{
    if ((m_arrival_tick++ % kArrivalPollFrames) != 0)
        return;

    static constexpr HidNpadIdType kArrivalScan[] = {
        HidNpadIdType_No1,
        HidNpadIdType_No2,
        HidNpadIdType_No3,
        HidNpadIdType_No4,
    };

    uint32_t mask = 0;
    for (HidNpadIdType npad : kArrivalScan)
    {
        PadState probe{};
        padInitializeWithMask(&probe, 1UL << (uint32_t)npad);
        padUpdate(&probe);
        if (padIsConnected(&probe))
            mask |= 1UL << (uint32_t)npad;
    }

    const uint32_t previous = m_connected_mask;
    m_connected_mask = mask;
    m_announced_mask &= mask;

    if (!m_arrival_primed)
    {
        m_arrival_primed = true;
        m_announced_mask = mask;
        return;
    }

    if (!m_on_pad_arrived)
        return;

    const uint32_t arrived = mask & ~previous & ~m_announced_mask;
    if (!arrived)
        return;

    for (HidNpadIdType npad : kArrivalScan)
    {
        const uint32_t bit = 1UL << (uint32_t)npad;
        if (!(arrived & bit))
            continue;
        if (npad == m_bound_npad)
            continue;
        if (m_couch_roster.find(npad) != m_couch_roster.end())
            continue;

        m_announced_mask |= bit;
        m_arrival_pending = true;
        brls::Logger::info("Couch: controller appeared on npad {}", (int)npad);
        m_on_pad_arrived(npad);
        return;
    }
}

void InputManager::pollCouchClaim()
{
    if (!m_couch_claim_active)
        return;

    auto pads = describePads();
    for (const auto& desc : pads)
    {
        HidNpadIdType npad = desc.npad;
        if (!isClaimableNpad(npad))
            continue;
        if (m_couch_roster.find(npad) != m_couch_roster.end())
            continue;

        PadState pad;
        padInitializeWithMask(&pad, 1UL << (uint32_t)npad);
        padUpdate(&pad);
        u64 btns = padGetButtons(&pad);
        bool combo = (btns & HidNpadButton_ZL) && (btns & HidNpadButton_ZR);

        int& hold = m_claim_hold[npad];
        if (combo)
        {
            hold++;
            if (hold >= kClaimHoldFrames)
            {
                uint8_t slot = couchJoin(npad);
                m_claim_hold.clear();
                m_couch_claim_active = false;
                if (slot != kNoCouchSlot && m_on_couch_joined)
                    m_on_couch_joined(npad, slot);
                return;
            }
        }
        else
        {
            hold = 0;
        }
    }
}

void InputManager::update(ChiakiControllerState* state, std::map<uint32_t, int8_t>* finger_id_touch_id)
{
    pollPadArrivals();
    pollCouchClaim();

    if (!m_path)
    {
        chiaki_controller_state_set_idle(state);
        return;
    }

    retryPathIdentification();
    reconcilePathDriver();

    m_path->setExcludedNpads(couchNpadMask());

    m_path->poll();

    applyPadInput(m_path.get(), state);

    u64 buttons = m_path->usesButtonMapping() ? m_path->heldButtons() : 0;

    if (!m_path->readTouchpad(state))
    {
        readTouchScreen(state, finger_id_touch_id);
        updateSyntheticSwipes(state, buttons);

        if (m_touchpad_button_hold < 0)
        {
            m_touchpad_button_hold++;
            if (m_touchpad_button_hold == 0)
                m_touchpad_button_hold = PendingBorderTap::TAP_BUTTON_HOLD_FRAMES;
        }
        else if (m_touchpad_button_hold > 0)
        {
            state->buttons |= CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
            m_touchpad_button_hold--;
        }

        if (m_touchpad_button_hold == 0)
            fireDeferredRelease(state);
    }

    if (++m_sixaxis_frame_counter >= 3) {
        m_sixaxis_frame_counter = 0;
        readSixAxis(state);
    }
}

void InputManager::fireDeferredRelease(ChiakiControllerState* state)
{
    if (m_deferred_release_touch_id < 0)
        return;
    brls::Logger::info("Touch: deferred stop_touch={} fires now", m_deferred_release_touch_id);
    chiaki_controller_state_stop_touch(state, (uint8_t)m_deferred_release_touch_id);
    m_deferred_release_touch_id = -1;
    m_active_click_touch_id = -1;
}

bool InputManager::readTouchScreen(ChiakiControllerState* chiaki_state, std::map<uint32_t, int8_t>* finger_id_touch_id)
{
    HidTouchScreenState sw_state = {0};

    size_t got = hidGetTouchScreenStates(&sw_state, 1);
    if (got == 0)
    {
        if (m_touch_debug_counter % 300 == 0 && !finger_id_touch_id->empty())
            brls::Logger::warning("Touch: hidGetTouchScreenStates returned 0, preserving {} active touches", finger_id_touch_id->size());
        m_touch_debug_counter++;
        return !finger_id_touch_id->empty();
    }

    if (sw_state.count > 1)
    {
        s32 target = -1;
        for (s32 i = 0; i < sw_state.count; i++)
        {
            uint32_t fid = sw_state.touches[i].finger_id;
            if (finger_id_touch_id->count(fid) || m_pending_border_taps.count(fid))
            {
                target = i;
                break;
            }
        }
        if (target < 0)
            target = 0;
        if (target != 0)
            sw_state.touches[0] = sw_state.touches[target];
        sw_state.count = 1;
    }

    if (sw_state.count > 0 && m_prev_touch_count == 0)
        brls::Logger::info("Touch: started, {} point(s), finger_id={}, pos=({},{})",
            sw_state.count, sw_state.touches[0].finger_id, sw_state.touches[0].x, sw_state.touches[0].y);
    else if (sw_state.count == 0 && m_prev_touch_count > 0)
        brls::Logger::info("Touch: released, had {} tracked finger(s)", finger_id_touch_id->size());
    m_prev_touch_count = sw_state.count;

    bool ret = false;

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
                if (m_touchpad_button_hold != 0)
                {
                    m_deferred_release_touch_id = cur->second;
                    brls::Logger::debug("Touch: defer stop_touch={} until button hold completes", cur->second);
                }
                else
                {
                    brls::Logger::debug("Touch: stop touch_id={} for finger_id={}", cur->second, cur->first);
                    chiaki_controller_state_stop_touch(chiaki_state, (uint8_t)cur->second);
                }
            }
            finger_id_touch_id->erase(cur);
        }
    }

    for (auto pt = m_pending_border_taps.begin(); pt != m_pending_border_taps.end();)
    {
        bool found = false;
        for (int i = 0; i < sw_state.count; i++)
        {
            if (sw_state.touches[i].finger_id == pt->first)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            brls::Logger::info("Touch: pending border tap for finger_id={} lifted before commit", pt->first);
            pt = m_pending_border_taps.erase(pt);
        }
        else
        {
            ++pt;
        }
    }

    const float trackpadMaxX = m_is_ps5 ? 1919.0f : 1920.0f;
    const float trackpadMaxY = m_is_ps5 ? 1079.0f : 942.0f;

    IVideoRenderer* renderer = Session::GetInstance()->getVideoRenderer();
    if (m_overlay_drag_finger >= 0)
    {
        bool still_down = false;
        for (int i = 0; i < sw_state.count; i++)
        {
            if ((int64_t)sw_state.touches[i].finger_id == m_overlay_drag_finger)
            {
                still_down = true;
                break;
            }
        }
        if (!still_down)
        {
            if (renderer)
                renderer->overlayTouchEnd();
            m_overlay_drag_finger = -1;
        }
    }

    const float screenScaleX = (float)brls::Application::windowWidth / (float)SWITCH_TOUCHSCREEN_MAX_X;
    const float screenScaleY = (float)brls::Application::windowHeight / (float)SWITCH_TOUCHSCREEN_MAX_Y;

    for (int i = 0; i < sw_state.count; i++)
    {
        uint16_t x = sw_state.touches[i].x * (trackpadMaxX / (float)SWITCH_TOUCHSCREEN_MAX_X);
        uint16_t y = sw_state.touches[i].y * (trackpadMaxY / (float)SWITCH_TOUCHSCREEN_MAX_Y);

        uint32_t rawX = sw_state.touches[i].x;
        uint32_t rawY = sw_state.touches[i].y;
        bool isBorder = rawX <= 64 || rawX >= (SWITCH_TOUCHSCREEN_MAX_X - 64) ||
                        rawY <= 64 || rawY >= (SWITCH_TOUCHSCREEN_MAX_Y - 64);

        auto it = finger_id_touch_id->find(sw_state.touches[i].finger_id);
        bool isTracked = it != finger_id_touch_id->end();

        auto pt = m_pending_border_taps.find(sw_state.touches[i].finger_id);
        bool isPending = pt != m_pending_border_taps.end();

        if ((int64_t)sw_state.touches[i].finger_id == m_overlay_drag_finger)
        {
            if (renderer)
                renderer->overlayTouchMove(rawX * screenScaleX, rawY * screenScaleY);
            ret = true;
            continue;
        }

        if (!isTracked && !isPending)
        {
            if (renderer && renderer->overlayTouchBegin(rawX * screenScaleX, rawY * screenScaleY))
            {
                m_overlay_drag_finger = (int64_t)sw_state.touches[i].finger_id;
                ret = true;
                continue;
            }

            if (isBorder)
            {
                m_pending_border_taps[sw_state.touches[i].finger_id] =
                    {(uint16_t)rawX, (uint16_t)rawY, 0};
                brls::Logger::info("Touch: pending border tap for finger_id={} raw=({},{})",
                    sw_state.touches[i].finger_id, rawX, rawY);
            }
            else
            {
                fireDeferredRelease(chiaki_state);
                int8_t touch_id = chiaki_controller_state_start_touch(chiaki_state, x, y);
                (*finger_id_touch_id)[sw_state.touches[i].finger_id] = touch_id;
                brls::Logger::info("Touch: new finger_id={} -> touch_id={}, raw=({},{}) mapped=({},{})",
                    sw_state.touches[i].finger_id, touch_id,
                    sw_state.touches[i].x, sw_state.touches[i].y, x, y);
                if (touch_id < 0)
                    brls::Logger::warning("Touch: no free touch slots (max {})", CHIAKI_CONTROLLER_TOUCHES_MAX);
            }
        }
        else if (isPending)
        {
            int dx = (int)rawX - (int)pt->second.down_x;
            int dy = (int)rawY - (int)pt->second.down_y;
            bool moved = dx*dx + dy*dy > PendingBorderTap::TAP_MAX_MOVE * PendingBorderTap::TAP_MAX_MOVE;

            if (moved)
            {
                fireDeferredRelease(chiaki_state);
                int8_t touch_id = chiaki_controller_state_start_touch(chiaki_state, x, y);
                (*finger_id_touch_id)[sw_state.touches[i].finger_id] = touch_id;
                brls::Logger::info("Touch: border swipe committed finger_id={} -> touch_id={} at mapped=({},{})",
                    sw_state.touches[i].finger_id, touch_id, x, y);
                m_pending_border_taps.erase(pt);
            }
            else if (++pt->second.frame_count >= PendingBorderTap::TAP_COMMIT_FRAMES)
            {
                fireDeferredRelease(chiaki_state);
                int8_t touch_id = chiaki_controller_state_start_touch(chiaki_state, x, y);
                (*finger_id_touch_id)[sw_state.touches[i].finger_id] = touch_id;
                m_touchpad_button_hold = -PendingBorderTap::TAP_BUTTON_DELAY_FRAMES;
                m_active_click_touch_id = touch_id;
                Session::GetInstance()->triggerBorderFlash();
                brls::Logger::info("Touch: border tap committed (touch first, button in {} frames, pos frozen) finger_id={} -> touch_id={} at mapped=({},{})",
                    PendingBorderTap::TAP_BUTTON_DELAY_FRAMES, sw_state.touches[i].finger_id, touch_id, x, y);
                m_pending_border_taps.erase(pt);
            }
        }
        else if (it->second >= 0)
        {
            if (it->second != m_active_click_touch_id)
            {
                chiaki_controller_state_set_touch_pos(chiaki_state, (uint8_t)it->second, x, y);
                brls::Logger::debug("Touch: move touch_id={} mapped=({},{})", it->second, x, y);
            }
        }
        ret = true;
    }
    return ret;
}

bool InputManager::readSixAxis(ChiakiControllerState* state)
{
    HidSixAxisSensorState sixaxis = {0};
    m_path->readGyro(&sixaxis);

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
    if (!m_path)
        return;

    m_accel_zero_x = m_raw_accel_x;
    m_accel_zero_y = m_raw_accel_y - 1.0f;
    m_accel_zero_z = m_raw_accel_z;

    m_path->resetMotion();

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

    HidAnalogStickState leftStick = m_path->stickPos(0);
    HidAnalogStickState rightStick = m_path->stickPos(1);

    static const char* swipeNames[4] = { "UP", "DOWN", "LEFT", "RIGHT" };

    for (int i = 0; i < 4; i++) {
        SyntheticSwipe& swipe = m_swipes[i];

        if (!SettingsManager::getInstance()->isButtonEnabled(swipeConstants[i])) {
            swipe.buttonWasPressed = false;
            if (swipe.phase == SyntheticSwipe::Phase::ACTIVE) {
                brls::Logger::info("Swipe {}: disabled mid-swipe, stopping touch_id={}", swipeNames[i], swipe.touchId);
                chiaki_controller_state_stop_touch(state, (uint8_t)swipe.touchId);
                swipe.phase = SyntheticSwipe::Phase::IDLE;
                swipe.touchId = -1;
            }
            continue;
        }

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

        if (comboHeld && !stickDirectionValid)
            brls::Logger::debug("Swipe {}: combo held but stick direction invalid (L={},{} R={},{})",
                swipeNames[i], leftStick.x, leftStick.y, rightStick.x, rightStick.y);

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
                    brls::Logger::info("Swipe {}: started touch_id={}, start=({},{}) step=({},{})",
                        swipeNames[i], swipe.touchId, swipe.curX, swipe.curY, swipe.dx, swipe.dy);
                } else {
                    brls::Logger::warning("Swipe {}: no free touch slots", swipeNames[i]);
                }
            }
            swipe.buttonWasPressed = comboHeld && stickDirectionValid;
        } else {
            swipe.frameCounter++;
            if (swipe.frameCounter >= SyntheticSwipe::SWIPE_FRAMES) {
                brls::Logger::info("Swipe {}: completed, final pos=({},{}), touch_id={}",
                    swipeNames[i], swipe.curX, swipe.curY, swipe.touchId);
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
