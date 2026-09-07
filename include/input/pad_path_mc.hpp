#ifndef AKIRA_INPUT_PAD_PATH_MC_HPP
#define AKIRA_INPUT_PAD_PATH_MC_HPP

#include "input/pad_path_hos.hpp"
#include "input/ps_report.hpp"

#include <cstdint>

class ExtendedInputManager;

namespace akira::input {

/*
 * A controller MissionControl mediates, read through its Switch translation.
 *
 * Buttons, sticks and motion all come from HOS exactly as they would for a Pro
 * Controller, because that is what MC presents. The one thing added is trigger
 * pressure, which the translation throws away and the sysmodule recovers by
 * learning where the pad keeps it.
 */
class McGenericPath : public HosPadPath {
public:
    McGenericPath(HidNpadIdType npad, ExtendedInputManager& extended,
                  uint16_t vendor_id, uint16_t product_id);

    PadPathKind kind() const override { return PadPathKind::McGeneric; }
    const char* label() const override { return m_label; }

    PadCapabilities capabilities() const override;

    bool poll() override;
    void readTriggers(ChiakiControllerState* state) override;

    /*
     * The two bands really are two motors here.
     *
     * MissionControl reads the Switch's low and high band amplitudes and drives
     * the pad's two ERM motors from them - low band to the left motor, high
     * band to the right - and discards frequency entirely, because an ERM has
     * no resonance to tune. So the console's two rumble values map straight
     * across, where on a Joy-Con they genuinely do not.
     */
    void sendRumble(float left, float right, float freqLow, float freqHigh) override;

    /* Mediated by MissionControl, so HOS's own idea of the pad is not the one
     * that matters - these come from the backend, which knows what it is. */
    bool     switchNative() const override { return false; }
    uint16_t vendorId() const override { return m_vendor_id; }
    uint16_t productId() const override { return m_product_id; }

protected:
    ExtendedInputManager& m_extended;
    uint16_t              m_vendor_id;
    uint16_t              m_product_id;

    /* Rate limit for the generic-rumble line - see McGenericPath::sendRumble. */
    uint32_t              m_rumble_log_next = 0;
    const char*           m_label = "Controller";
};

/*
 * A PlayStation pad, read as a PlayStation pad.
 *
 * MissionControl folds Create, Options and Mute onto minus, plus and capture,
 * then folds a touchpad click onto the same three by zone - six inputs into
 * three buttons, with the click's position discarded. On a Switch that is a
 * reasonable compromise; for a client streaming to a PS5 it throws away exactly
 * the inputs the console is expecting, and nothing downstream can recover them.
 *
 * So buttons, triggers and touch come from the pad's own report. Sticks and
 * motion still come from HOS: MC calibrates the DualSense's gyro against the
 * pad's own calibration data before publishing it, and the sticks are eight
 * bits at source either way, so reading those raw would cost accuracy and buy
 * nothing.
 */
class McPsNativePath : public McGenericPath {
public:
    McPsNativePath(HidNpadIdType npad, ExtendedInputManager& extended,
                   const PsModel& model);

    PadPathKind kind() const override { return PadPathKind::McPsNative; }

    PadCapabilities capabilities() const override;

    bool poll() override;

    void readButtons(ChiakiControllerState* state) override;
    void readTriggers(ChiakiControllerState* state) override;
    bool readTouchpad(ChiakiControllerState* state) override;

    /* The mapping layer exists to reach PS5 buttons a Switch pad does not have.
     * This pad has all of them. */
    bool usesButtonMapping() const override { return false; }

    /*
     * Written straight to the pad when it takes its own output report, and
     * through HOS when it does not.
     *
     * These two must never both run. MissionControl composes its own output
     * report from the Switch vibration HID sends it, so a pad receiving both
     * gets two sources contending for the same two motor fields - which reads
     * as flicker rather than as anything obviously wrong.
     */
    void sendRumble(float left, float right, float freqLow, float freqHigh) override;

    /*
     * The one thing on this path with no fallback at all.
     *
     * Nothing in the Switch vibration protocol can express trigger resistance,
     * so a pad reached through MissionControl's translation cannot receive this
     * however it is asked - which is why the console has been sending it into
     * nothing since Akira first spoke to a PS5.
     */
    void sendTriggerEffects(const TriggerEffect& left, const TriggerEffect& right) override;

    /*
     * The user's own console settings, applied by the pad rather than by us.
     *
     * They arrive as two four-value settings and leave as one byte the pad
     * reads, which is the only place the trigger half can be honoured at all:
     * an effect's parameters are opaque, so there is no arithmetic here that
     * could make one gentler.
     */
    void sendEffectIntensity(uint8_t vibration, uint8_t trigger) override;

    void sendLightbar(uint8_t red, uint8_t green, uint8_t blue) override;

    /* True only once the model is one we write to directly - a PlayStation pad
     * we have no output report for still goes through the translation and
     * still wants the translation's shaping. */
    bool nativeRumble() const override { return m_direct_output; }

    /* Known only once a report has arrived, which is also the only time it
     * could be useful - an address we have never heard from is not a pad. */
    const uint8_t* address() const override { return m_have_address ? m_address : nullptr; }

    /*
     * Options, held.
     *
     * Not PS: a long press there puts the pad into Bluetooth pairing at the
     * firmware level, below anything a host can influence, and a short press
     * belongs to the console. Options held for three seconds mirrors what Minus
     * already does on a Switch pad - including that the console sees the button
     * meanwhile, which is the existing behaviour rather than a new quirk.
     */
    bool menuHeld() const override;

    /*
     * HOME is blocked for as long as this path is the chosen one.
     *
     * MissionControl wires a DualSense's PS button to the Switch HOME button
     * (dualsense_controller.cpp: m_buttons.home = buttons->ps), and HOS acts on
     * HOME before any application sees it - so pressing PS suspended Akira and
     * showed the Switch home menu, while the PS bit we forward to the console
     * arrived at a stream nobody was looking at any more.
     *
     * Blocking it is scoped to this class deliberately. A Joy-Con or a Pro
     * Controller has a real HOME button that means HOME, and taking it away
     * from those would be wrong; a PlayStation pad has a PS button that means
     * PlayStation, and its trip through HOME is MissionControl's doing rather
     * than the user's intent.
     */
    ~McPsNativePath() override;

private:
    bool m_rumble_source_logged = false;
    /* chiaki allocates a touch id per contact and expects it held for the life
     * of that contact, so the pad's own tracking id is not usable directly. */
    struct TouchSlot {
        bool    active = false;
        uint8_t pad_id = 0;
        int8_t  chiaki_id = -1;
    };

    void releaseSlot(ChiakiControllerState* state, TouchSlot& slot);

    const PsModel* m_model;
    bool           m_home_blocked      = false;
    bool           m_direct_output     = false;
    uint8_t        m_address[6]{};
    bool           m_have_address      = false;
    PsPadState     m_report;
    bool           m_report_valid = false;
    TouchSlot      m_slots[2];
};

} // namespace akira::input

#endif // AKIRA_INPUT_PAD_PATH_MC_HPP
