#ifndef AKIRA_CONNECTION_STAGE_HPP
#define AKIRA_CONNECTION_STAGE_HPP

#include <chiaki/session.h>

#include <string>

struct NVGcontext;

enum class ConnectionStage : int {
    Idle = 0,
    Waking,
    Finding,
    Linking,
    Authenticating,
    Tuning,
    Starting,
    Connected
};

ConnectionStage matchConnectionStage(const std::string& logLine, ConnectionStage current);
const char* connectionStageLabelKey(ConnectionStage stage);
const char* connectionFailureKeyForStage(ConnectionStage stage);
const char* connectionFailureKeyForReason(ChiakiQuitReason reason);
void drawConnectionRing(NVGcontext* vg, float cx, float cy, const std::string& label, int stageIndex, int stageTotal);
void drawConnectionFailure(NVGcontext* vg, float cx, float cy, const std::string& message);

#endif // AKIRA_CONNECTION_STAGE_HPP
