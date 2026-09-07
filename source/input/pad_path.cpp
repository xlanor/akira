#include "input/pad_path.hpp"

#include <cstring>
#include "input/pad_names.hpp"
#include "input/pad_path_hos.hpp"
#include "input/pad_path_mc.hpp"

#include "core/settings_manager.hpp"
#include "input/extended_input_manager.hpp"

#include <borealis.hpp>

#include <algorithm>

namespace akira::input {

namespace {

class JoyConPath : public HosPadPath {
public:
    JoyConPath(HidNpadIdType npad, HidNpadStyleTag style)
        : HosPadPath(npad, style)
    {
    }

    PadPathKind kind() const override { return PadPathKind::JoyCon; }

    const char* label() const override
    {
        return m_style == HidNpadStyleTag_NpadHandheld ? "Handheld" : "Joy-Con (L/R)";
    }

    PadCapabilities capabilities() const override
    {
        PadCapabilities caps;
        caps.gyro   = true;
        caps.rumble = true;
        return caps;
    }
};

class SwitchProPath : public HosPadPath {
public:
    explicit SwitchProPath(HidNpadIdType npad)
        : HosPadPath(npad, HidNpadStyleTag_NpadFullKey)
    {
    }

    PadPathKind kind() const override { return PadPathKind::SwitchPro; }
    const char* label() const override { return "Pro Controller"; }

    PadCapabilities capabilities() const override
    {
        PadCapabilities caps;
        caps.gyro   = true;
        caps.rumble = true;
        return caps;
    }
};

/*
 * What Akira did before any of this existed: one merged handheld/player-one
 * pad, six-axis handles for all three styles dispatched on the live style set,
 * and rumble through borealis at controller zero.
 *
 * It is reproduced rather than improved on purpose. This is what runs when the
 * user has not picked a controller, which is every user who never installs the
 * sysmodule, so it is the one path where "no observable change" is the whole
 * requirement. The merge it performs is a known defect - with Joy-Cons on the
 * rail and a Pro Controller connected, both style bits are set and handheld
 * wins - but fixing that is what picking a pad is for, not something to change
 * underneath someone who has not picked.
 */
class LegacyPadPath : public PadPath {
public:
    explicit LegacyPadPath(ExtendedInputManager& extended)
        : m_extended(extended)
    {
        padInitializeDefault(&m_pad);
        padUpdate(&m_pad);

        hidGetSixAxisSensorHandles(&m_sixaxis[0], 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
        hidGetSixAxisSensorHandles(&m_sixaxis[1], 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey);
        hidGetSixAxisSensorHandles(&m_sixaxis[2], 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual);
        for (int i = 0; i < 4; i++)
            StartSixAxisSensorShared(m_sixaxis[i]);
    }

    ~LegacyPadPath() override
    {
        for (int i = 0; i < 4; i++)
            StopSixAxisSensorShared(m_sixaxis[i]);
    }

    PadPathKind kind() const override { return PadPathKind::JoyCon; }
    const char* label() const override { return "Default"; }

    HidNpadIdType   npad()  const override { return HidNpadIdType_No1; }
    HidNpadStyleTag style() const override { return (HidNpadStyleTag)padGetStyleSet(&m_pad); }

    PadCapabilities capabilities() const override
    {
        PadCapabilities caps;
        caps.gyro   = true;
        caps.rumble = true;
        return caps;
    }

    bool poll() override
    {
        padUpdate(&m_pad);
        m_buttons = padGetButtons(&m_pad);
        return true;
    }

    HidAnalogStickState stickPos(int index) const override
    {
        return padGetStickPos(&m_pad, index);
    }

    void readTriggers(ChiakiControllerState* state) override
    {
        if (!m_extended.hasFreshAnalogState())
            return;

        state->l2_state = m_extended.l2();
        state->r2_state = m_extended.r2();
    }

    bool readGyro(HidSixAxisSensorState* out) override
    {
        uint64_t styleSet = padGetStyleSet(&m_pad);

        if (styleSet & HidNpadStyleTag_NpadHandheld) {
            hidGetSixAxisSensorStates(m_sixaxis[0], out, 1);
            return true;
        }

        if (styleSet & HidNpadStyleTag_NpadFullKey) {
            hidGetSixAxisSensorStates(m_sixaxis[1], out, 1);
            return true;
        }

        if (styleSet & HidNpadStyleTag_NpadJoyDual) {
            uint64_t attrib = padGetAttributes(&m_pad);
            bool leftConnected  = attrib & HidNpadAttribute_IsLeftConnected;
            bool rightConnected = attrib & HidNpadAttribute_IsRightConnected;

            bool useLeft  = false;
            bool useRight = false;

            switch (SettingsManager::getInstance()->getGyroSource()) {
                case GyroSource::Left:  useLeft  = leftConnected;  break;
                case GyroSource::Right: useRight = rightConnected; break;
                case GyroSource::Auto:
                default:
                    if (leftConnected)       useLeft  = true;
                    else if (rightConnected) useRight = true;
                    break;
            }

            if (useLeft)       hidGetSixAxisSensorStates(m_sixaxis[2], out, 1);
            else if (useRight) hidGetSixAxisSensorStates(m_sixaxis[3], out, 1);
            else               return false;

            return true;
        }

        return false;
    }

    void resetMotion() override
    {
        for (int i = 0; i < 4; i++)
            hidResetSixAxisSensorFusionParameters(m_sixaxis[i]);
    }

    void sendRumble(float left, float right, float freqLow, float freqHigh) override
    {
        /* Controller zero, with borealis's handheld shift applied - which is
         * what Akira did before paths existed, positional guess included. A
         * path that knows its pad addresses it by npad instead. */
        float amp = std::clamp(std::max(left, right), 0.0f, 1.0f);

        auto* inputMgr = brls::Application::getPlatform()->getInputManager();
        inputMgr->sendRumbleRaw(0, freqLow, freqHigh, amp, amp);
    }

    uint64_t heldButtons() const override { return m_buttons; }

private:
    ExtendedInputManager&  m_extended;
    PadState               m_pad{};
    uint64_t               m_buttons = 0;
    HidSixAxisSensorHandle m_sixaxis[4]{};
};

/* Which npads to look at. Handheld first so it reads the way the console does. */
constexpr HidNpadIdType kScanOrder[] = {
    HidNpadIdType_Handheld,
    HidNpadIdType_No1,
    HidNpadIdType_No2,
    HidNpadIdType_No3,
    HidNpadIdType_No4,
};

} // namespace

namespace {

std::vector<PadDescription> DescribeWith(const AkiraInputDeviceList& devices, bool haveDevices);

} // namespace

std::vector<PadDescription> DescribePads(ExtendedInputManager& extended)
{
    /*
     * A genuine Pro Controller and a DualSense through MissionControl both
     * report NpadFullKey, and nothing HOS exposes tells them apart. The
     * backend's device list is not an optimisation here - it is the only
     * discriminator there is.
     */
    AkiraInputDeviceList devices{};
    const bool haveDevices = extended.listDevices(&devices);

    return DescribeWith(devices, haveDevices);
}

std::vector<PadDescription> DescribePads()
{
    AkiraInputDeviceList devices{};
    const bool haveDevices = ExtendedInputManager::listDevicesOnce(&devices);

    return DescribeWith(devices, haveDevices);
}

namespace {

std::vector<PadDescription> DescribeWith(const AkiraInputDeviceList& devices, bool haveDevices)
{
    std::vector<PadDescription> out;

    bool taken[AKIRA_INPUT_MAX_LISTED_DEVICES]{};

    for (HidNpadIdType npad : kScanOrder) {
        PadState probe{};
        padInitialize(&probe, npad);
        padUpdate(&probe);

        if (!padIsConnected(&probe))
            continue;

        const uint64_t styleSet = padGetStyleSet(&probe);

        const AkiraInputDeviceInfo* mc = nullptr;
        if (haveDevices && (styleSet & HidNpadStyleTag_NpadFullKey) != 0) {
            for (uint8_t i = 0; i < devices.count && i < AKIRA_INPUT_MAX_LISTED_DEVICES; i++) {
                if (taken[i]) {
                    continue;
                }
                const AkiraInputDeviceInfo& d = devices.devices[i];
                if ((d.flags & AkiraInputDevice_Identified) == 0) {
                    continue;
                }
                mc       = &d;
                taken[i] = true;
                break;
            }
        }

        PadDescription desc;
        desc.npad = npad;

        if (mc != nullptr) {
            const PsModel* model = FindPsModel(mc->vendor_id, mc->product_id);

            desc.style       = HidNpadStyleTag_NpadFullKey;
            desc.vendor_id   = mc->vendor_id;
            desc.product_id  = mc->product_id;
            std::memcpy(desc.bt_addr, mc->bt_addr, sizeof(desc.bt_addr));
            desc.has_address = true;
            desc.caps.gyro   = true;
            desc.caps.rumble = true;
            /* Analog pressure comes from a model we recognise, not from a
             * training run - so a pad the backend cannot name simply does not
             * have it, and there is no longer anything a user could do about
             * that from here. */
            desc.caps.analog_triggers = false;
            desc.caps.battery = PadReportsBattery(mc->vendor_id, mc->product_id);

            if (model != nullptr) {
                desc.kind          = PadPathKind::McPsNative;
                desc.label         = model->name;
                desc.caps.touchpad = model->has_touchpad;

                /* Read natively, so pressure comes from the pad's own report. */
                desc.caps.analog_triggers = true;
            } else {
                desc.kind = PadPathKind::McGeneric;

                /* Named from the id where the id is unambiguous. Where it is
                 * not - and several of MissionControl's are shared or junk -
                 * the generic label is the truthful answer. */
                const char* named = FindPadName(mc->vendor_id, mc->product_id);
                desc.label = named != nullptr ? named : "Controller";
            }

            out.push_back(desc);
            continue;
        }

        desc.caps.gyro   = true;
        desc.caps.rumble = true;

        /* No MissionControl in the way, so whatever HOS reports for this
         * pad is the pad's own. An unidentified FullKey pad is the one
         * case we cannot vouch for - it may be a MissionControl pad from
         * a family that never fills the field in - so it is left off. */
        desc.caps.battery = (styleSet & HidNpadStyleTag_NpadFullKey) == 0;

        if (styleSet & HidNpadStyleTag_NpadHandheld) {
            desc.kind  = PadPathKind::JoyCon;
            desc.style = HidNpadStyleTag_NpadHandheld;
            desc.label = "Handheld";
        } else if (styleSet & HidNpadStyleTag_NpadFullKey) {
            /*
             * HOS reports a MissionControl-mediated pad as NpadFullKey exactly
             * as it reports a genuine Pro Controller, and there is no style,
             * device type or npad field that separates them. So this branch
             * knows the shape and not the model, and says so - calling an
             * unidentified pad "Pro Controller" was asserting the one thing
             * that cannot be checked from here.
             *
             * The name is only claimed once the backend has actually
             * enumerated something: a list that came back empty proves
             * nothing, because nothing reports until a subscription is
             * live. A pad missing from a populated list, on the other
             * hand, really is not one MissionControl is carrying.
             */
            desc.kind  = PadPathKind::SwitchPro;
            desc.style = HidNpadStyleTag_NpadFullKey;
            const bool backendEnumerated = haveDevices && devices.count > 0;
            desc.label = backendEnumerated ? "Pro Controller" : "Controller";
        } else if (styleSet & HidNpadStyleTag_NpadJoyDual) {
            desc.kind  = PadPathKind::JoyCon;
            desc.style = HidNpadStyleTag_NpadJoyDual;
            desc.label = "Joy-Con (L/R)";
        } else {
            continue;
        }

        out.push_back(desc);
    }

    return out;
}

} // namespace

/*
 * Gather what the resolver needs and let it decide.
 *
 * The precedence used to be written out here as well, which meant two copies
 * that could disagree - and did. This asks the same question the pump asks,
 * with the fields a screen can answer left at values that do not narrow it:
 * a page is not a stream, so wanted and owns_output are not its business.
 */
PadDriverState ResolvePadDriverState(const PadDescription& desc,
                                     ExtendedInputManager& extended)
{
    PadOutputInputs in;
    in.supported_pad = desc.kind == PadPathKind::McPsNative
                    && FindPsModel(desc.vendor_id, desc.product_id) != nullptr;

    if (in.supported_pad) {
        const RumbleProfile profile =
            SettingsManager::getInstance()->resolveRumbleProfile(
                desc.vendor_id, desc.product_id,
                desc.has_address ? desc.bt_addr : nullptr, false, false);
        in.profile_native = profile.output_mode == PadOutputMode::Native;
    }

    in.backend_enabled   = !extended.backendReleased();
    in.ownership_refused = ExtendedInputManager::outputOwnershipRefused();

    /* The two the pump owns and a listing cannot answer. Left true so they do
     * not turn every described pad into "nothing is streaming to it". */
    in.wanted        = true;
    in.address_valid = true;
    in.owns_output   = !in.ownership_refused;

    const PadOutputState state = ResolvePadOutput(in);
    return { state.driver, state.reason };
}

PadDriver ResolvePadDriver(const PadDescription& desc, ExtendedInputManager& extended)
{
    return ResolvePadDriverState(desc, extended).driver;
}

std::unique_ptr<PadPath> MakePath(const PadDescription& desc, ExtendedInputManager& extended)
{
    switch (desc.kind) {
        case PadPathKind::McPsNative: {
            const PsModel* model = FindPsModel(desc.vendor_id, desc.product_id);
            if (model == nullptr)
                return nullptr;

            if (ResolvePadDriver(desc, extended) == PadDriver::MissionControl) {
                brls::Logger::info("pad {:04x}:{:04x} driven by MissionControl"
                                   " - using the generic path",
                                   desc.vendor_id, desc.product_id);
                return std::make_unique<McGenericPath>(desc.npad, extended, desc.vendor_id,
                                                       desc.product_id);
            }

            return std::make_unique<McPsNativePath>(desc.npad, extended, *model);
        }
        case PadPathKind::McGeneric:
            return std::make_unique<McGenericPath>(desc.npad, extended, desc.vendor_id,
                                                   desc.product_id);
        case PadPathKind::SwitchPro:
            return std::make_unique<SwitchProPath>(desc.npad);
        case PadPathKind::JoyCon:
        default:
            return std::make_unique<JoyConPath>(desc.npad, desc.style);
    }
}

std::unique_ptr<PadPath> DefaultPadPath(ExtendedInputManager& extended)
{
    return std::make_unique<LegacyPadPath>(extended);
}

} // namespace akira::input
