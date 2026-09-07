/*
 * The haptic conversion is where the DualSense feel is won or lost, and every
 * way of getting it wrong is silent: too quiet reads as a weak pad, no gate
 * reads as a broken one, no floor reads as a whining one. So each of those is
 * pinned separately rather than checked as one "sounds about right".
 */

#include "test_util.hpp"

#include "input/pad_output_state.hpp"
#include "input/rumble_profile.hpp"

#include <vector>

using namespace akira::input;

namespace {

/* A stereo buffer at fixed levels, where peak and mean coincide, so a case
 * that is not about the difference between them does not have to pick one. */
std::vector<std::int16_t> Stereo(std::int16_t left, std::int16_t right, std::size_t frames = 128)
{
    std::vector<std::int16_t> buf(frames * 2);
    for (std::size_t i = 0; i < frames; i++) {
        buf[i * 2]     = left;
        buf[i * 2 + 1] = right;
    }
    return buf;
}

} // namespace

TEST(rumble_haptic_reaches_full_scale)
{
    /* Full scale must convert to full scale, and nothing below it may reach
     * here - a conversion that saturates early makes every effect arrive at
     * the same blast, and one that saturates late makes the pad feel dead. */
    auto buf = Stereo(32767, 32767);
    const HapticRumble r = HapticAudioToRumble(buf.data(), 128, HapticIntensity::Normal);

    CHECK(r.emit);
    CHECK_EQ((int)(r.left >> 8), 255);
    CHECK_EQ((int)(r.right >> 8), 255);
}

TEST(rumble_haptic_follows_the_crest_not_the_average)
{
    /* An impact is a short burst in a mostly quiet buffer. Averaged, a
     * full-scale hit lasting one frame in 128 converts to 512 of 65535 and is
     * felt as nothing; the motor has to follow how hard it hit. */
    std::vector<std::int16_t> buf(128 * 2, 0);
    buf[0] = 32767;
    buf[1] = 32767;

    const HapticRumble r = HapticAudioToRumble(buf.data(), 128, HapticIntensity::Normal);

    REQUIRE(r.emit);
    CHECK_EQ((int)(r.left  >> 8), 255);
    CHECK_EQ((int)(r.right >> 8), 255);
}

TEST(rumble_haptic_keeps_the_two_channels_apart)
{
    auto buf = Stereo(32767, 4000);
    const HapticRumble r = HapticAudioToRumble(buf.data(), 128, HapticIntensity::Normal);

    CHECK(r.emit);
    CHECK(r.left > r.right);
}

TEST(rumble_haptic_floor_is_only_for_a_pad_with_a_motor_to_stall)
{
    /* Just above the noise gate and well under the stall floor: an ERM at this
     * level whines without turning, so it is lifted. A DualSense drives the
     * same coils it plays haptics through and has no stall to clear, so lifting
     * it there only makes quiet passages audible. */
    auto faint = Stereo(80, 80);

    const HapticRumble erm = HapticAudioToRumble(faint.data(), 128,
                                                 HapticIntensity::Normal, true);
    const HapticRumble coils = HapticAudioToRumble(faint.data(), 128,
                                                   HapticIntensity::Normal, false);

    CHECK(erm.emit);
    CHECK(coils.emit);
    CHECK_EQ((int)erm.left, (int)kHapticMotorFloor);
    CHECK(coils.left < kHapticMotorFloor);
    CHECK_EQ((int)coils.left, (int)coils.raw_left);
}

TEST(rumble_haptic_gates_room_tone)
{
    /* Under the floor is the residual level in the stream, not an effect.
     * Passing it through is what makes the motors hum continuously. */
    auto quiet = Stereo(20, 20);
    CHECK(!HapticAudioToRumble(quiet.data(), 128, HapticIntensity::Normal).emit);

    auto loud = Stereo(4000, 4000);
    CHECK(HapticAudioToRumble(loud.data(), 128, HapticIntensity::Normal).emit);
}

TEST(rumble_haptic_says_nothing_rather_than_zero)
{
    /* Silence must not be published as an amplitude, or every effect gets its
     * tail chopped by the next quiet buffer. */
    auto quiet = Stereo(0, 0);
    const HapticRumble r = HapticAudioToRumble(quiet.data(), 128, HapticIntensity::Normal);

    CHECK(!r.emit);
    CHECK_EQ((int)r.left, 0);
    CHECK_EQ((int)r.right, 0);
}

TEST(rumble_haptic_lifts_anything_audible_over_the_stall_threshold)
{
    /* Just above the noise floor, and far below what turns a motor. */
    auto buf = Stereo(60, 60);
    const HapticRumble r = HapticAudioToRumble(buf.data(), 128, HapticIntensity::Normal);

    REQUIRE(r.emit);
    CHECK(r.left  >= kHapticMotorFloor);
    CHECK(r.right >= kHapticMotorFloor);
}

TEST(rumble_haptic_floor_clears_the_envelope_silence_threshold)
{
    /* HapticManager zeroes any envelope below 2/255. A floor at or beneath
     * that is cancelled the instant it is applied, so everything quiet enough
     * to need lifting is silently dropped instead. */
    CHECK((float)kHapticMotorFloor / 65535.0f > 2.0f / 255.0f);
}

TEST(rumble_haptic_intensity_scales_both_ways)
{
    auto buf = Stereo(4000, 4000);

    const HapticRumble weak   = HapticAudioToRumble(buf.data(), 128, HapticIntensity::Weak);
    const HapticRumble normal = HapticAudioToRumble(buf.data(), 128, HapticIntensity::Normal);
    const HapticRumble strong = HapticAudioToRumble(buf.data(), 128, HapticIntensity::Strong);

    CHECK(weak.left < normal.left);
    CHECK(strong.left > normal.left);
}

TEST(rumble_haptic_off_emits_nothing_however_loud)
{
    auto buf = Stereo(32767, 32767);
    CHECK(!HapticAudioToRumble(buf.data(), 128, HapticIntensity::Off).emit);
}

TEST(rumble_haptic_saturates_rather_than_wrapping)
{
    /* VeryStrong multiplies by five, which overflows a full-scale signal. */
    auto buf = Stereo(32767, 32767);
    const HapticRumble r = HapticAudioToRumble(buf.data(), 128, HapticIntensity::VeryStrong);

    CHECK_EQ((int)r.left, 0xffff);
    CHECK_EQ((int)r.right, 0xffff);
}

TEST(rumble_profile_keys_never_collide_between_tiers)
{
    const std::string model = RumbleKeyForModel(0x054c, 0x0ce6);
    CHECK_EQ(model, std::string("054c:0ce6"));

    const std::uint8_t mac[6] = { 0x24, 0xa6, 0xfa, 0xa1, 0xf1, 0x11 };
    const std::string unit = RumbleKeyForUnit(mac);
    CHECK_EQ(unit, std::string("24:a6:fa:a1:f1:11"));

    CHECK(RumbleKeyIsModel(model));
    CHECK(!RumbleKeyIsUnit(model));
    CHECK(RumbleKeyIsUnit(unit));
    CHECK(!RumbleKeyIsModel(unit));

    /* The reserved keys must fall through both tests, or a Joy-Con would be
     * read as a model key and looked up in the wrong tier. */
    CHECK(!RumbleKeyIsUnit(kRumbleKeySwitch));
    CHECK(!RumbleKeyIsModel(kRumbleKeySwitch));
    CHECK(!RumbleKeyIsUnit(kRumbleKeyDefault));
    CHECK(!RumbleKeyIsModel(kRumbleKeyDefault));
}

TEST(rumble_profile_switch_defaults_preserve_todays_joycon_feel)
{
    const RumbleProfile s = SwitchRumbleProfile();

    /* 255/400 - the cap that lived in hapticBase. If this drifts, every
     * existing user's Joy-Cons change feel on upgrade. */
    CHECK(s.ceiling > 0.62f && s.ceiling < 0.64f);
    CHECK(!s.per_motor);

    const RumbleProfile d = DefaultRumbleProfile();
    CHECK_EQ((int)(d.ceiling * 100.0f), 100);
    CHECK(d.per_motor);
}

/*
 * The category tier.
 *
 * A MAC and a vid/pid are told apart by length; these are words, so they
 * collide with neither and are matched by name.
 */
TEST(rumble_profile_category_keys_are_their_own_tier)
{
    for (const char* key : { kRumbleKeyDualSense, kRumbleKeyJoyCon,
                             kRumbleKeySwitchPro, kRumbleKeyGeneric }) {
        CHECK(RumbleKeyIsCategory(key));
        CHECK(!RumbleKeyIsUnit(key));
        CHECK(!RumbleKeyIsModel(key));
    }

    CHECK(!RumbleKeyIsCategory(kRumbleKeyDefault));
    CHECK(!RumbleKeyIsCategory(kRumbleKeySwitch));
    CHECK(!RumbleKeyIsCategory("054c:0ce6"));
    CHECK(!RumbleKeyIsCategory("24:a6:fa:a1:f1:11"));
}

TEST(rumble_profile_category_covers_both_dualsense_models)
{
    /* The Edge is a DualSense for anything that matters here - same output
     * report, same actuators - so it shares the category rather than needing
     * its own copy of every setting. */
    CHECK(PadCategoryFor(0x054c, 0x0ce6, false, false) == PadCategory::DualSense);
    CHECK(PadCategoryFor(0x054c, 0x0df2, false, false) == PadCategory::DualSense);

    /* A DualShock 4 takes a different output report and must not land here. */
    CHECK(PadCategoryFor(0x054c, 0x09cc, false, false) == PadCategory::Generic);
    CHECK(PadCategoryFor(0x0000, 0x0000, false, false) == PadCategory::Generic);
}

TEST(rumble_profile_switch_pads_split_by_style_not_by_id)
{
    /* HOS gives no vendor id for a pad it drives itself, so the only thing
     * separating a Joy-Con from a Pro Controller is the npad style the caller
     * passes in. Both ignore whatever ids happen to be present. */
    CHECK(PadCategoryFor(0, 0, true, true)  == PadCategory::JoyCon);
    CHECK(PadCategoryFor(0, 0, true, false) == PadCategory::SwitchPro);

    CHECK(PadCategoryFor(0x054c, 0x0ce6, true, true) == PadCategory::JoyCon);
}

TEST(rumble_profile_defaults_to_full_output_and_is_gated_by_identity)
{
    /*
     * Full by default, because a profile that predates these fields - or one
     * seedRumbleProfile made from plain defaults - must not silently turn off
     * a pad's triggers, rumble and haptics.
     *
     * What keeps a DualSense frame away from a pad that cannot read one is
     * PadTakesDirectOutput, checked before the direct path is ever reached, not
     * the value of this field.
     */
    const RumbleProfile d = DefaultRumbleProfile();
    CHECK(d.output_mode == PadOutputMode::Native);

    /* A pad derives its rumble from the haptic track unless it is told
     * otherwise, and a pad with coils is never told otherwise. */
    CHECK(d.rumble_source == RumbleSource::Derived);

    CHECK(PadTakesDirectOutput(0x054c, 0x0ce6));
    CHECK(!PadTakesDirectOutput(0x054c, 0x09cc));
    CHECK(!PadTakesDirectOutput(0x0000, 0x0000));
}

TEST(haptic_base_puts_normal_where_the_old_strong_preset_was)
{
    /*
     * The legacy motor path had two divisors reached through Weak and Strong,
     * 128 and 50. Normal has to land on 50 or every existing Joy-Con config
     * that read Strong quietly changes strength on upgrade.
     */
    CHECK(HapticBaseForIntensity(HapticIntensity::Normal) == 50);

    /* Lower intensity is a larger divisor, and the order must hold across the
     * whole scale - it is the same gain curve the actuators use. */
    CHECK(HapticBaseForIntensity(HapticIntensity::VeryWeak) >
          HapticBaseForIntensity(HapticIntensity::Weak));
    CHECK(HapticBaseForIntensity(HapticIntensity::Weak) >
          HapticBaseForIntensity(HapticIntensity::Normal));
    CHECK(HapticBaseForIntensity(HapticIntensity::Normal) >
          HapticBaseForIntensity(HapticIntensity::Strong));
    CHECK(HapticBaseForIntensity(HapticIntensity::Strong) >
          HapticBaseForIntensity(HapticIntensity::VeryStrong));

    /* Off would divide by zero. */
    CHECK(HapticBaseForIntensity(HapticIntensity::Off) > 0);
}

/* ------------------------------------------------------- pad output state */

static PadOutputInputs DrivingInputs()
{
    PadOutputInputs in;
    in.supported_pad    = true;
    in.profile_native   = true;
    in.backend_enabled  = true;
    in.owns_output      = true;
    in.wanted           = true;
    in.address_valid    = true;
    in.haptics_wanted   = true;
    in.haptics_landing  = true;
    return in;
}

TEST(pad_output_drives_when_everything_agrees)
{
    const PadOutputState s = ResolvePadOutput(DrivingInputs());
    CHECK(s.driver == PadDriver::Akira);
    CHECK(s.reason == PadDriverReason::Native);
    CHECK(s.want_claim);
    CHECK(s.write_state);
    CHECK(s.stream_haptics);
}

TEST(pad_output_released_beats_refused)
{
    /* Both produce a refusal and only one is something the user did. Reporting
     * the wrong one sends someone hunting a build problem they do not have. */
    PadOutputInputs in = DrivingInputs();
    in.backend_enabled   = false;
    in.ownership_refused = true;

    const PadOutputState s = ResolvePadOutput(in);
    CHECK(s.driver == PadDriver::MissionControl);
    CHECK(s.reason == PadDriverReason::Released);
}

TEST(pad_output_keeps_asking_after_a_refusal)
{
    /* The backend releases every pad without telling anyone, so asking again is
     * the only thing that can notice it changing its mind back. */
    PadOutputInputs in = DrivingInputs();
    in.ownership_refused = true;
    in.owns_output       = false;

    const PadOutputState s = ResolvePadOutput(in);
    CHECK(s.driver == PadDriver::MissionControl);
    CHECK(s.want_claim);
    CHECK(!s.write_state);
    CHECK(!s.stream_haptics);
}

TEST(pad_output_never_writes_to_a_pad_it_does_not_own)
{
    /* Between asking and being granted, MissionControl is still writing to this
     * pad. A second writer is what every fault came back to. */
    PadOutputInputs in = DrivingInputs();
    in.owns_output = false;

    const PadOutputState s = ResolvePadOutput(in);
    CHECK(s.want_claim);
    CHECK(!s.write_state);
    CHECK(!s.stream_haptics);
}

TEST(pad_output_separates_haptics_from_writing)
{
    PadOutputInputs in = DrivingInputs();
    in.haptics_wanted = false;

    const PadOutputState s = ResolvePadOutput(in);
    CHECK(s.driver == PadDriver::Akira);
    CHECK(s.write_state);
    CHECK(!s.stream_haptics);
}

TEST(pad_output_basic_is_not_reported_as_a_backend_problem)
{
    PadOutputInputs in = DrivingInputs();
    in.profile_native = false;

    const PadOutputState s = ResolvePadOutput(in);
    CHECK(s.driver == PadDriver::MissionControl);
    CHECK(s.reason == PadDriverReason::Basic);
    CHECK(!s.want_claim);
}
