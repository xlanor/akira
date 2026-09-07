#ifndef AKIRA_INPUT_PAD_PATH_HOS_HPP
#define AKIRA_INPUT_PAD_PATH_HOS_HPP

#include "input/pad_path.hpp"

namespace akira::input {

void StartSixAxisSensorShared(HidSixAxisSensorHandle handle);
void StopSixAxisSensorShared(HidSixAxisSensorHandle handle);

/*
 * Shared base for every path whose buttons and sticks come from HOS.
 *
 * All four kinds read sticks from HOS, and three of the four read buttons from
 * it too, so the difference between them is not where the data comes from but
 * which npad and which style the handles are built for. Getting that wrong is
 * silent: HidVibrationDeviceHandle encodes npad_style_index, so a handle built
 * for the wrong style addresses a device that does not exist and vibration is
 * dropped with no error at all.
 */
class HosPadPath : public PadPath {
public:
public:
    HosPadPath(HidNpadIdType npad, HidNpadStyleTag style);
    ~HosPadPath() override;

    HidNpadIdType   npad()  const override { return m_npad; }
    HidNpadStyleTag style() const override { return m_style; }

    bool poll() override;
    HidAnalogStickState stickPos(int index) const override;
    void readTriggers(ChiakiControllerState* state) override;
    bool readGyro(HidSixAxisSensorState* out) override;
    void resetMotion() override;
    void sendRumble(float left, float right, float freqLow, float freqHigh) override;

    uint64_t heldButtons() const override { return m_buttons; }

protected:
    /* Six-axis handles encode the style, so they are built from the style this
     * path was constructed with rather than guessed at each call site.
     *
     * Vibration handles are deliberately NOT owned here. borealis already keeps
     * a current set for every connected pad and rebuilds them on style change,
     * and a second owner writing the same handles is how one frame of rumble
     * goes missing for reasons nobody can reproduce. */
    void acquireHandles();
    void releaseHandles();

    PadState        m_pad{};
    HidNpadIdType   m_npad;
    HidNpadStyleTag m_style;
    uint64_t        m_buttons = 0;

    /* The style set the handles were built against. Both six-axis and
     * vibration handles encode the style, so when a pad's style changes -
     * Joy-Cons coming off the rail, a pad reconnecting - handles built for the
     * old one address a device that no longer exists and fail silently. */
    uint64_t m_style_set_at_acquire = 0;

    HidSixAxisSensorHandle m_sixaxis[2]{};
    int                    m_sixaxis_count = 0;
};

} // namespace akira::input

#endif // AKIRA_INPUT_PAD_PATH_HOS_HPP
