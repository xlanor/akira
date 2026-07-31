#include "views/connection_stage.hpp"
#include "ui/theme.hpp"

#include <borealis.hpp>

#include <cctype>
#include <cmath>

ConnectionStage matchConnectionStage(const std::string& logLine, ConnectionStage current)
{
    std::string s;
    s.reserve(logLine.size());
    for (char c : logLine)
        s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    auto has = [&](const char* sub) { return s.find(sub) != std::string::npos; };

    ConnectionStage cand = ConnectionStage::Idle;
    if (has("chiaki_event_connected") || has("first video frame"))
        cand = ConnectionStage::Connected;
    else if (has("senkusha"))
        cand = ConnectionStage::Tuning;
    else if (has("streamconnection") || has("takion connected") || has("received bang") ||
             has("streaminfo") || has("switch to stream"))
        cand = ConnectionStage::Starting;
    else if (has("session request") || has("starting ctrl") || has("ctrl connected") ||
             has("valid session id"))
        cand = ConnectionStage::Authenticating;
    else if (has("punching ctrl hole") || has("hole punched") || has("connection established"))
        cand = ConnectionStage::Linking;
    else if (has("upnp discovery") || has("holepunch session") || has("ctrl offer"))
        cand = ConnectionStage::Finding;
    else if (has("waking") || has("wakeup"))
        cand = ConnectionStage::Waking;

    return static_cast<int>(cand) > static_cast<int>(current) ? cand : current;
}

const char* connectionStageLabelKey(ConnectionStage stage)
{
    switch (stage) {
        case ConnectionStage::Waking: return "akira/connection/stage_waking";
        case ConnectionStage::Finding: return "akira/connection/stage_finding";
        case ConnectionStage::Linking: return "akira/connection/stage_linking";
        case ConnectionStage::Authenticating: return "akira/connection/stage_authenticating";
        case ConnectionStage::Tuning: return "akira/connection/stage_tuning";
        case ConnectionStage::Starting: return "akira/connection/stage_starting";
        case ConnectionStage::Connected: return "akira/connection/stage_starting";
        default: return "akira/connection/stage_connecting";
    }
}

const char* connectionFailureKeyForStage(ConnectionStage stage)
{
    switch (stage) {
        case ConnectionStage::Waking: return "akira/connection/fail_wake";
        case ConnectionStage::Finding: return "akira/connection/fail_find";
        case ConnectionStage::Linking: return "akira/connection/fail_link";
        case ConnectionStage::Authenticating: return "akira/connection/fail_auth";
        case ConnectionStage::Tuning:
        case ConnectionStage::Starting: return "akira/connection/fail_stream";
        default: return "akira/connection/fail_generic";
    }
}

const char* connectionFailureKeyForReason(ChiakiQuitReason reason)
{
    switch (reason) {
        case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE:
            return "akira/connection/fail_in_use";
        case CHIAKI_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED:
        case CHIAKI_QUIT_REASON_CTRL_CONNECT_FAILED:
        case CHIAKI_QUIT_REASON_CTRL_CONNECTION_REFUSED:
            return "akira/connection/fail_unreachable";
        case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_VERSION_MISMATCH:
            return "akira/connection/fail_version";
        case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_CRASH:
            return "akira/connection/fail_crash";
        case CHIAKI_QUIT_REASON_PSN_REGIST_FAILED:
            return "akira/connection/fail_regist";
        case CHIAKI_QUIT_REASON_STREAM_CONNECTION_UNKNOWN:
        case CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_DISCONNECTED:
        case CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN:
            return "akira/connection/fail_stream";
        default:
            return "akira/connection/fail_generic";
    }
}

void drawConnectionFailure(NVGcontext* vg, float cx, float cy, const std::string& message)
{
    NVGcolor danger = akira::ui::active().danger;

    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, 14.0f);
    nvgFillColor(vg, danger);
    nvgFill(vg);

    nvgFontSize(vg, 22.0f);
    nvgFillColor(vg, akira::ui::active().text);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgText(vg, cx, cy + 54.0f, message.c_str(), nullptr);
}

void drawConnectionPulse(NVGcontext* vg, float cx, float cy, const std::string& label)
{
    NVGcolor accent = akira::ui::active().accent;

    float t = static_cast<float>(brls::getCPUTimeUsec() % 1600000) / 1600000.0f;

    NVGcolor ringCol = accent;
    ringCol.a = (1.0f - t) * 0.55f;
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, 20.0f + t * 40.0f);
    nvgStrokeWidth(vg, 2.0f);
    nvgStrokeColor(vg, ringCol);
    nvgStroke(vg);

    float breathe = 0.5f + 0.5f * std::sin(t * 2.0f * NVG_PI);
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, 13.0f + 2.0f * breathe);
    nvgFillColor(vg, accent);
    nvgFill(vg);

    nvgFontSize(vg, 22.0f);
    nvgFillColor(vg, akira::ui::active().text);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgText(vg, cx, cy + 54.0f, label.c_str(), nullptr);
}
