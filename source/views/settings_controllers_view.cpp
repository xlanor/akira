#include "views/settings_controllers_view.hpp"
#include "views/settings_pad_view.hpp"

#include "core/settings_manager.hpp"
#include "input/extended_input_manager.hpp"
#include "input/pad_path.hpp"
#include "input/ps_output.hpp"

#include <borealis/core/i18n.hpp>

using namespace brls::literals;

namespace {

/* What the row says about where this pad's settings come from, so the tier is
 * visible without opening it. */
std::string TierSummary(const akira::input::PadDescription& pad)
{
    const bool switchNative = pad.kind == akira::input::PadPathKind::JoyCon
                           || pad.kind == akira::input::PadPathKind::SwitchPro
                           ;

    auto* settings = SettingsManager::getInstance();

    /* For a pad we can drive ourselves, which of the two it is set to says far
     * more than which tier its numbers came from. */
    if (akira::input::PadTakesDirectOutput(pad.vendor_id, pad.product_id)) {
        const akira::input::RumbleProfile profile = settings->resolveRumbleProfile(
            pad.vendor_id, pad.product_id,
            pad.has_address ? pad.bt_addr : nullptr, switchNative,
            pad.kind == akira::input::PadPathKind::JoyCon);

        return profile.output_mode == akira::input::PadOutputMode::Basic
                   ? "akira/settings/rumble_output_basic"_i18n
                   : "akira/settings/rumble_output_native"_i18n;
    }

    const std::string key = settings->resolveRumbleKey(
        pad.vendor_id, pad.product_id,
        pad.has_address ? pad.bt_addr : nullptr, switchNative,
        pad.kind == akira::input::PadPathKind::JoyCon);

    if (key == akira::input::kRumbleKeySwitch)
        return "akira/settings/rumble_tier_switch"_i18n;
    if (key == akira::input::kRumbleKeyDefault)
        return "akira/settings/rumble_tier_default"_i18n;
    if (akira::input::RumbleKeyIsUnit(key))
        return "akira/settings/rumble_tier_unit"_i18n;

    return key;
}

} // namespace

SettingsControllersView::SettingsControllersView()
{
    setAxis(brls::Axis::COLUMN);
    setWidth(brls::View::AUTO);
    setHeight(brls::View::AUTO);
    setGrow(1.0f);
    setPadding(30.0f, 40.0f, 30.0f, 40.0f);
    setBackgroundColor(brls::Application::getTheme()["brls/background"]);

    auto* title = new brls::Label();
    title->setText("akira/settings/connected_controllers"_i18n);
    title->setFontSize(30.0f);
    addView(title);

    /* Said up front rather than shown as an empty list: a pad that has not
     * reported since the last subscription is invisible to the backend, and
     * "no controllers" would be a confident claim about one we simply are not
     * listening to. */
    auto* hint = new brls::Label();
    hint->setText("akira/settings/connected_controllers_hint"_i18n);
    hint->setFontSize(16.0f);
    hint->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
    hint->setMarginBottom(20.0f);
    addView(hint);

    auto* scroll = new brls::ScrollingFrame();
    scroll->setWidth(brls::View::AUTO);
    scroll->setHeight(brls::View::AUTO);
    scroll->setGrow(1.0f);

    list = new brls::Box(brls::Axis::COLUMN);
    list->setWidth(brls::View::AUTO);
    list->setHeight(brls::View::AUTO);
    list->setCornerRadius(14.0f);
    list->setBackgroundColor(brls::Application::getTheme()["color/card"]);
    scroll->setContentView(list);
    addView(scroll);

    rebuild();

    /* A bare Box pushed as an Activity gets no back handling of its own, so
     * without this the page is a dead end. The hidden flag is what puts it in
     * the footer rather than leaving the user to guess. */
    this->registerAction("akira/common/back"_i18n, brls::ControllerButton::BUTTON_B,
                         [](brls::View*) {
                             brls::Application::popActivity();
                             return true;
                         }, true);
}

void SettingsControllersView::rebuild()
{
    list->clearViews();

    auto* settings = SettingsManager::getInstance();
    const std::vector<akira::input::PadDescription> pads = akira::input::DescribePads();

    if (pads.empty()) {
        auto* empty = new brls::Label();
        empty->setText("akira/settings/connected_controllers_none"_i18n);
        empty->setFontSize(16.0f);
        empty->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        empty->setMargins(30.0f, 22.0f, 30.0f, 22.0f);
        list->addView(empty);
        return;
    }

    for (const akira::input::PadDescription& pad : pads) {
        /*
         * Seeded on sight rather than on first edit. Keying it on use would
         * have meant streaming with a controller before it could be tuned -
         * and the reason to tune it is that streaming with it felt wrong.
         */
        const bool switchNative = pad.kind == akira::input::PadPathKind::JoyCon
                               || pad.kind == akira::input::PadPathKind::SwitchPro
                               ;

        /*
         * Including the pads we write to directly. Skipping them left a
         * DualSense resolving to "default" and sharing one entry with every
         * unrecognised controller, so tuning either one moved the other.
         */
        if (!switchNative && (pad.vendor_id != 0 || pad.product_id != 0)) {
            const akira::input::RumbleProfile current = settings->resolveRumbleProfile(
                pad.vendor_id, pad.product_id,
                pad.has_address ? pad.bt_addr : nullptr, switchNative,
                pad.kind == akira::input::PadPathKind::JoyCon);
            settings->seedRumbleProfile(
                akira::input::RumbleKeyForModel(pad.vendor_id, pad.product_id), &current);
        } else
            settings->seedRumbleProfile(settings->resolveRumbleKey(
                pad.vendor_id, pad.product_id,
                pad.has_address ? pad.bt_addr : nullptr, switchNative,
                pad.kind == akira::input::PadPathKind::JoyCon));

        auto* cell = new brls::DetailCell();
        cell->setMarginLeft(15.0f);
        cell->setMarginRight(15.0f);
        cell->setText(pad.label);
        cell->setDetailText(TierSummary(pad));

        cell->registerClickAction([pad](brls::View*) {
            brls::Application::pushActivity(new brls::Activity(new SettingsPadView(pad)),
                                            brls::TransitionAnimation::NONE);
            return true;
        });

        list->addView(cell);
    }

    settings->writeFile();
}
