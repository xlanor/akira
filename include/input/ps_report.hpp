#ifndef AKIRA_INPUT_PS_REPORT_HPP
#define AKIRA_INPUT_PS_REPORT_HPP

#include <cstddef>
#include <cstdint>

namespace akira::input {

/*
 * Reading a PlayStation pad's own HID report, rather than the Switch pad
 * MissionControl translates it into.
 *
 * The translation is lossy in a way that matters here. MC maps Create, Options
 * and Mute onto minus, plus and capture - and then maps a touchpad click onto
 * the same three by zone, left of 15% to minus, right of 85% to plus, the
 * middle to capture. Six distinct inputs arrive at HOS as three buttons, and
 * the touchpad click and its position are gone entirely. Nothing downstream can
 * put that back, so a PlayStation pad has to be read as one.
 *
 * Deliberately free of libnx and chiaki types so it can be tested on the host.
 */

enum PsButton : uint32_t {
    PsButton_Cross    = 1u << 0,
    PsButton_Circle   = 1u << 1,
    PsButton_Square   = 1u << 2,
    PsButton_Triangle = 1u << 3,
    PsButton_L1       = 1u << 4,
    PsButton_R1       = 1u << 5,
    PsButton_L2       = 1u << 6,  /* the digital edge; pressure is l2 */
    PsButton_R2       = 1u << 7,
    PsButton_L3       = 1u << 8,
    PsButton_R3       = 1u << 9,
    PsButton_Create   = 1u << 10, /* Share on a DualShock 4 */
    PsButton_Options  = 1u << 11,
    PsButton_Ps       = 1u << 12,
    PsButton_Touchpad = 1u << 13, /* the click, not a touch */
    PsButton_Mute     = 1u << 14, /* DualSense only */
    PsButton_Up       = 1u << 15,
    PsButton_Down     = 1u << 16,
    PsButton_Left     = 1u << 17,
    PsButton_Right    = 1u << 18,
};

struct PsTouchPoint {
    bool     down = false;
    uint8_t  id   = 0;     /* the pad's own tracking id, stable across a contact */
    uint16_t x    = 0;
    uint16_t y    = 0;
};

struct PsPadState {
    bool         valid = false;
    uint32_t     buttons = 0;
    uint8_t      l2 = 0;
    uint8_t      r2 = 0;
    PsTouchPoint touch[2];

    /*
     * Whether the touch points in this packet are new.
     *
     * A DualShock 4 batches touch samples and says how many are live in
     * num_reports; zero means it sent no new ones, and the bytes still sitting
     * in the slots belong to an earlier packet. Reading them anyway replays an
     * old contact. A DualSense has no such counter and always reports its
     * current state, so this is simply true there.
     */
    bool         touch_fresh = false;
};

/*
 * Touch surfaces differ by model and the difference is not cosmetic: a
 * DualShock 4 is 1920x942 and a DualSense is 1920x1080, so treating one as the
 * other squashes or stretches every Y coordinate. Carried as data rather than
 * a constant for that reason.
 */
struct PsTouchSurface {
    uint16_t width  = 0;
    uint16_t height = 0;
};

/* What a model looks like on the wire. */
struct PsModel {
    uint16_t       vendor_id  = 0;
    uint16_t       product_id = 0;
    uint8_t        report_id  = 0;
    bool           has_touchpad = false;
    PsTouchSurface surface;
    const char*    name = "";
};

namespace detail {

/* Bit 7 of `contact` is set while the slot is empty; the low bits are the
 * pad's own tracking id for the current contact. */
inline PsTouchPoint ParseTouchPoint(const uint8_t* p)
{
    PsTouchPoint out;
    out.down = (p[0] & 0x80) == 0;
    out.id   = (uint8_t)(p[0] & 0x7f);
    out.x    = (uint16_t)(p[1] | ((p[2] & 0x0f) << 8));
    out.y    = (uint16_t)((p[2] >> 4) | (p[3] << 4));
    return out;
}

/* 0 is north and it runs clockwise; 8 means nothing is held. */
inline uint32_t ParseDpad(uint8_t dpad)
{
    switch (dpad) {
        case 0: return PsButton_Up;
        case 1: return PsButton_Up | PsButton_Right;
        case 2: return PsButton_Right;
        case 3: return PsButton_Down | PsButton_Right;
        case 4: return PsButton_Down;
        case 5: return PsButton_Down | PsButton_Left;
        case 6: return PsButton_Left;
        case 7: return PsButton_Up | PsButton_Left;
        default: return 0;
    }
}

} // namespace detail

/*
 * DualSense report 0x31, the one it sends over Bluetooth once it has been
 * asked for the full format. Offsets are from data[0], which is the report id.
 *
 *   6,7    L2, R2 pressure
 *   9      dpad:4 square cross circle triangle
 *   10     L1 R1 L2 R2 create options L3 R3
 *   11     ps touchpad mute
 *   34,38  the two touch points
 */
inline bool ParseDualSense31(const uint8_t* data, size_t len, PsPadState* out)
{
    /* The last byte we read is 41, so anything shorter is not this report
     * however much of it happens to look right. */
    if (data == nullptr || out == nullptr || len < 42)
        return false;
    if (data[0] != 0x31)
        return false;

    PsPadState s;
    s.valid = true;

    s.l2 = data[6];
    s.r2 = data[7];

    const uint8_t b0 = data[9];
    const uint8_t b1 = data[10];
    const uint8_t b2 = data[11];

    s.buttons |= detail::ParseDpad((uint8_t)(b0 & 0x0f));

    if (b0 & 0x10) s.buttons |= PsButton_Square;
    if (b0 & 0x20) s.buttons |= PsButton_Cross;
    if (b0 & 0x40) s.buttons |= PsButton_Circle;
    if (b0 & 0x80) s.buttons |= PsButton_Triangle;

    if (b1 & 0x01) s.buttons |= PsButton_L1;
    if (b1 & 0x02) s.buttons |= PsButton_R1;
    if (b1 & 0x04) s.buttons |= PsButton_L2;
    if (b1 & 0x08) s.buttons |= PsButton_R2;
    if (b1 & 0x10) s.buttons |= PsButton_Create;
    if (b1 & 0x20) s.buttons |= PsButton_Options;
    if (b1 & 0x40) s.buttons |= PsButton_L3;
    if (b1 & 0x80) s.buttons |= PsButton_R3;

    if (b2 & 0x01) s.buttons |= PsButton_Ps;
    if (b2 & 0x02) s.buttons |= PsButton_Touchpad;
    if (b2 & 0x04) s.buttons |= PsButton_Mute;

    s.touch[0] = detail::ParseTouchPoint(data + 34);
    s.touch[1] = detail::ParseTouchPoint(data + 38);
    s.touch_fresh = true;

    *out = s;
    return true;
}

/*
 * DualShock 4 report 0x11, its Bluetooth full-format report. Offsets are from
 * data[0], the report id.
 *
 *   7      dpad:4 square cross circle triangle
 *   8      L1 R1 L2 R2 share options L3 R3
 *   9      ps touchpad counter:6      <- no mute; bit 2 belongs to the counter
 *   10,11  L2, R2 pressure
 *   35     how many touch reports this packet carries
 *   37,41  the two points of the first touch report
 *
 * Note the button bytes come *before* the triggers here and after them on a
 * DualSense, which is why these are separate functions rather than one with a
 * table of offsets - the orders genuinely differ.
 */
inline bool ParseDualShock4_11(const uint8_t* data, size_t len, PsPadState* out)
{
    /* The last touch byte read is 44. */
    if (data == nullptr || out == nullptr || len < 45)
        return false;
    if (data[0] != 0x11)
        return false;

    PsPadState s;
    s.valid = true;

    s.l2 = data[10];
    s.r2 = data[11];

    const uint8_t b0 = data[7];
    const uint8_t b1 = data[8];
    const uint8_t b2 = data[9];

    s.buttons |= detail::ParseDpad((uint8_t)(b0 & 0x0f));

    if (b0 & 0x10) s.buttons |= PsButton_Square;
    if (b0 & 0x20) s.buttons |= PsButton_Cross;
    if (b0 & 0x40) s.buttons |= PsButton_Circle;
    if (b0 & 0x80) s.buttons |= PsButton_Triangle;

    if (b1 & 0x01) s.buttons |= PsButton_L1;
    if (b1 & 0x02) s.buttons |= PsButton_R1;
    if (b1 & 0x04) s.buttons |= PsButton_L2;
    if (b1 & 0x08) s.buttons |= PsButton_R2;
    if (b1 & 0x10) s.buttons |= PsButton_Create;   /* Share */
    if (b1 & 0x20) s.buttons |= PsButton_Options;
    if (b1 & 0x40) s.buttons |= PsButton_L3;
    if (b1 & 0x80) s.buttons |= PsButton_R3;

    if (b2 & 0x01) s.buttons |= PsButton_Ps;
    if (b2 & 0x02) s.buttons |= PsButton_Touchpad;
    /* bit 2 upward is the report counter, not a mute button. */

    if (data[35] > 0) {
        s.touch[0] = detail::ParseTouchPoint(data + 37);
        s.touch[1] = detail::ParseTouchPoint(data + 41);
        s.touch_fresh = true;
    }

    *out = s;
    return true;
}

/*
 * Models read natively, with the surface each one actually has - a DualShock 4
 * is 1920x942 where a DualSense is 1920x1080, and treating one as the other
 * stretches every Y coordinate.
 *
 * A pad belongs here only once its report layout is known from something better
 * than inference, because a wrong offset produces confidently wrong input
 * rather than no input.
 */
inline const PsModel* FindPsModel(uint16_t vendor_id, uint16_t product_id)
{
    static const PsModel kModels[] = {
        { 0x054c, 0x0ce6, 0x31, true, { 1920, 1080 }, "DualSense" },
        { 0x054c, 0x0df2, 0x31, true, { 1920, 1080 }, "DualSense Edge" },
        { 0x054c, 0x09cc, 0x11, true, { 1920,  942 }, "DualShock 4" },
        { 0x054c, 0x05c4, 0x11, true, { 1920,  942 }, "DualShock 4 v1" },
    };

    for (const PsModel& m : kModels) {
        if (m.vendor_id == vendor_id && m.product_id == product_id)
            return &m;
    }
    return nullptr;
}

inline bool ParsePsReport(const PsModel& model, const uint8_t* data, size_t len, PsPadState* out)
{
    if (model.report_id == 0x31)
        return ParseDualSense31(data, len, out);
    if (model.report_id == 0x11)
        return ParseDualShock4_11(data, len, out);
    return false;
}

} // namespace akira::input

#endif // AKIRA_INPUT_PS_REPORT_HPP
