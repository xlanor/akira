#include "psn/log.hpp"

namespace psn {

static LogSink sink = nullptr;

void setLogSink(LogSink newSink)
{
    sink = newSink;
}

void logMessage(LogLevel level, const std::string& message)
{
    if (sink)
        sink(level, message);
}

} // namespace psn
