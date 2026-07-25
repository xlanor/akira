#ifndef AKIRA_PSN_LOG_HPP
#define AKIRA_PSN_LOG_HPP

#include <format>
#include <string>
#include <utility>

namespace psn {

enum class LogLevel {
    Info,
    Warning,
    Error
};

// The client and its models are plain C++ over json-c so they can be built and tested on a
// host, which borealis cannot. Logging therefore goes through a sink the app installs once
// rather than a direct brls::Logger call.
using LogSink = void (*)(LogLevel level, const std::string& message);

void setLogSink(LogSink sink);
void logMessage(LogLevel level, const std::string& message);

template <typename... Args>
void logInfo(std::format_string<Args...> fmt, Args&&... args)
{
    logMessage(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void logWarning(std::format_string<Args...> fmt, Args&&... args)
{
    logMessage(LogLevel::Warning, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void logError(std::format_string<Args...> fmt, Args&&... args)
{
    logMessage(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace psn

#endif // AKIRA_PSN_LOG_HPP
