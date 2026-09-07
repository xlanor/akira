#ifndef AKIRA_BATTERY_PIPS_HPP
#define AKIRA_BATTERY_PIPS_HPP

#include <borealis.hpp>
#include <switch.h>

/*
 * Charge level for one pad, or for both halves of a Joy-Con pair.
 *
 * Worth showing because it is real even for a pad HOS did not ship with:
 * MissionControl reads the charge out of the controller's own report and folds
 * it into the emulated Switch input report, so HOS holds a genuine level for a
 * DualSense the same as for a Pro Controller.
 */
class BatteryPips : public brls::View {
public:
    BatteryPips(HidNpadIdType npad, bool split, float width, float height);

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

private:
    void drawOne(NVGcontext* vg, float x, float y, float w, float h,
                 const HidPowerInfo& info) const;

    HidNpadIdType m_npad;
    bool          m_split;
};

#endif // AKIRA_BATTERY_PIPS_HPP
