#ifndef AKIRA_LIGHTBAR_PICKER_HPP
#define AKIRA_LIGHTBAR_PICKER_HPP

#include <borealis.hpp>
#include <cstdint>
#include <functional>

/*
 * A colour, chosen the way colours are chosen.
 *
 * Three RGB sliders needed the user to understand mixing: "a bit more orange"
 * meant moving two of them in opposite directions and one of them backwards.
 * A hue strip and a saturation/value field is the arrangement every picker
 * settles on because it separates "which colour" from "how much of it", and the
 * first of those is one movement.
 *
 * Hue is a strip because a stick sweeping one axis is exactly what a strip
 * wants. Saturation and value share a square, driven by the left stick, with
 * the d-pad left as a coarse step for anyone who would rather nudge.
 */
class LightbarPicker : public brls::Box {
public:
    LightbarPicker(std::uint8_t r, std::uint8_t g, std::uint8_t b);

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    brls::View* getDefaultFocus() override { return this; }

    /* Called as the selection moves, so the pad can follow the cursor rather
     * than waiting for the page to close. */
    void setChangeListener(std::function<void(std::uint8_t, std::uint8_t, std::uint8_t)> fn)
    {
        m_changed = std::move(fn);
    }

private:
    void emit();
    void moveHue(float delta);
    void moveField(float ds, float dv);

    /* Stored as HSV and converted on the way out: round-tripping through RGB
     * every frame loses the hue of anything desaturated, so dragging value to
     * zero and back would come out grey. */
    float m_h = 0.0f;   /* degrees */
    float m_s = 1.0f;
    float m_v = 1.0f;

    /* Which of the two the stick is driving. The strip and the square want the
     * same axes, so they take turns rather than fighting. */
    bool m_on_hue = false;

    std::function<void(std::uint8_t, std::uint8_t, std::uint8_t)> m_changed;
};

#endif // AKIRA_LIGHTBAR_PICKER_HPP
