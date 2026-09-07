#ifndef AKIRA_SETTINGS_PAD_VIEW_HPP
#define AKIRA_SETTINGS_PAD_VIEW_HPP

#include <borealis.hpp>
#include <borealis/views/cells/cell_bool.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/cells/cell_selector.hpp>
#include <borealis/views/cells/cell_slider.hpp>

#include <cstdint>
#include <memory>
#include <string>

#include "core/settings_manager.hpp"
#include "input/pad_path.hpp"

/*
 * Everything about one controller, on one page.
 *
 * Built rather than inflated, because what belongs here depends on the pad: a
 * DualSense chooses between its own features and plain rumble, an ERM pad has
 * no resonance so the frequency rows would be two numbers thrown away
 * downstream, and only a Joy-Con has two gyros to pick between. An XML layout
 * would have to contain every row and then hide most of them, which is the
 * same thing said less clearly.
 */
class SettingsPadView : public brls::Box {
public:
    /* Named by what the user is holding; keyed by whichever tier it resolved
     * to, which the header states so the two are never confused. */
    SettingsPadView(const akira::input::PadDescription& pad);

    /* previewBuzz releases the motors through brls::delay, so a callback can
     * outlive the screen. Cleared here rather than trusting the timer to have
     * fired first. */
    ~SettingsPadView() override { *m_alive = false; }

private:
    void buildHeader();
    void buildOutputRows();
    void buildSourceRows();
    void buildEnvelopeRows();
    void buildIntensityRow();
    void buildStrengthRows();
    void buildFrequencyRows();
    void buildLightbarRows();
    void buildGyroRow();
    void buildActionRows();

    /* True for a pad we read and write ourselves - which is what decides
     * whether the page offers features or shaping. */
    bool takesDirectOutput() const;

    void store();
    void reopen();
    void previewBuzz();
    void previewDirectBuzz();

    brls::Box* content = nullptr;

    SettingsManager*            settings = nullptr;
    akira::input::PadDescription m_pad;
    akira::input::RumbleProfile  m_profile;

    /* Who drives this pad and why, resolved once when the page is built so
     * every row is built from the same answer. */
    akira::input::PadDriverState m_driver;

    /* The swatch, kept so the sliders can repaint it as they move. */
    brls::Box* m_lightbar_swatch = nullptr;
    std::string                  m_key;

    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

#endif // AKIRA_SETTINGS_PAD_VIEW_HPP
