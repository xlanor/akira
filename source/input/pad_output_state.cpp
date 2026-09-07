#include "input/pad_output_state.hpp"

namespace akira::input {

const char* PadDriverName(PadDriver driver)
{
    return driver == PadDriver::Akira ? "akira" : "MissionControl";
}

const char* PadDriverReasonName(PadDriverReason reason)
{
    switch (reason) {
        case PadDriverReason::Native:          return "driving it";
        case PadDriverReason::Released:        return "native output is off";
        case PadDriverReason::Unsupported:     return "no output ownership";
        case PadDriverReason::Basic:           return "set to basic";
        case PadDriverReason::NoAddress:       return "address unknown";
        case PadDriverReason::NotWanted:       return "nothing streaming";
        case PadDriverReason::NotSupportedPad:
        default:                               return "not a pad we can drive";
    }
}

/*
 * Precedence, written once and in one direction.
 *
 * Released is asked before refused because both produce a refusal and only one
 * of them is something the user did - reporting "this MissionControl cannot do
 * it" when the answer is "you switched it off" sends someone looking for a
 * build problem they do not have.
 */
PadOutputState ResolvePadOutput(const PadOutputInputs& in)
{
    PadOutputState out;

    if (!in.supported_pad) {
        out.reason = PadDriverReason::NotSupportedPad;
        return out;
    }

    if (!in.backend_enabled) {
        out.reason = PadDriverReason::Released;
        return out;
    }

    /*
     * Basic means no claim, and a claim is now the only route to a report.
     *
     * These used to be separable: reports came from a console-wide redirection
     * that cost nothing to leave on, so a Basic profile still got analog
     * triggers while MissionControl kept driving the pad's output. The backend
     * merged the two, because a pad whose reports you can see but cannot answer
     * is not useful and a pad you answer without seeing is guesswork.
     *
     * So Basic now means digital triggers as well. That is worth being plain
     * about rather than letting it read as a fault: taking the reports means
     * taking the output, and the whole point of Basic is not taking the output.
     */
    if (!in.profile_native) {
        out.reason = PadDriverReason::Basic;
        return out;
    }

    /*
     * A refusal is not the end of the question.
     *
     * We still want the claim, and asking for it again is the only thing that
     * can notice the backend changing its mind - the switch releases every pad
     * immediately and tells nobody. So want_claim stays true through a refusal
     * while the driver reads as MissionControl, which is what lets the pump
     * keep asking and the path stay honest at the same time.
     */
    if (in.ownership_refused) {
        out.reason     = PadDriverReason::Unsupported;
        out.want_claim = in.wanted && in.address_valid;
        return out;
    }

    if (!in.address_valid) {
        out.reason = PadDriverReason::NoAddress;
        return out;
    }

    if (!in.wanted) {
        out.reason = PadDriverReason::NotWanted;
        return out;
    }

    out.want_claim = true;

    /*
     * Owning it is what makes writing safe, not wanting it. Between asking and
     * being granted, MissionControl is still the one writing to this pad, and a
     * second writer is the state every fault this evening came back to.
     */
    if (!in.owns_output) {
        out.reason = PadDriverReason::Unsupported;
        return out;
    }

    out.driver         = PadDriver::Akira;
    out.reason         = PadDriverReason::Native;
    out.write_state    = true;
    out.stream_haptics = in.haptics_wanted && in.haptics_landing;

    return out;
}

} // namespace akira::input
