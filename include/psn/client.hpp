#ifndef AKIRA_PSN_CLIENT_HPP
#define AKIRA_PSN_CLIENT_HPP

#include <functional>
#include <string>
#include <vector>

#include "psn/models.hpp"

namespace psn {

enum class Status {
    Ok,
    NotLinked,
    SessionExpired,
    Offline,
    RateLimited,
    ServerError
};

const char* statusName(Status status);

struct Error {
    Status status = Status::Ok;
    std::string message;

    bool ok() const { return status == Status::Ok; }
};

// Knows the trophy API: base URL, endpoint paths, query parameters, envelopes and paging.
// It knows nothing about tokens, rate limits or caching — those reach it through the Fetch
// callback, which is expected to arrive already governed.
//
// Every list endpoint that PSN paginates is paged here rather than at the call site, so a
// caller receives a complete vector or an error. A short first page is indistinguishable
// from a complete response, which is why this cannot be left to remember per endpoint.
class Client {
public:
    // Performs one GET of an absolute URL and returns its body. The implementation owns
    // authentication, the rate limiter, the circuit breaker and retries.
    using Fetch = std::function<Error(const std::string& url, std::string& outBody)>;

    explicit Client(Fetch fetch);

    Error fetchSummary(TrophySummary& out) const;
    Error fetchTitles(std::vector<TrophyTitle>& out) const;

    // Group listings are small (one entry per DLC pack) and PSN does not paginate them.
    Error fetchGroupDefinitions(const std::string& npCommunicationId,
        const std::string& npServiceName, std::vector<TrophyGroup>& out) const;
    Error fetchGroupProgress(const std::string& npCommunicationId,
        const std::string& npServiceName, std::vector<TrophyGroup>& out) const;

    Error fetchTrophyDefinitions(const std::string& npCommunicationId,
        const std::string& npServiceName, std::vector<Trophy>& out) const;
    Error fetchTrophyProgress(const std::string& npCommunicationId,
        const std::string& npServiceName, std::vector<Trophy>& out) const;

private:
    using RowSink = std::function<bool(json_object* row)>;

    static constexpr const char* API_BASE = "https://m.np.playstation.com/api/trophy/v1";
    static constexpr int PAGE_SIZE = 100;
    static constexpr int PAGE_CAP = 50;

    Error fetchDocument(const std::string& path, Json& out) const;
    Error fetchList(const std::string& path, const char* arrayKey, const RowSink& onRow) const;
    Error fetchPaged(const std::string& path, const char* arrayKey, const RowSink& onRow) const;

    static std::string titlePath(const std::string& prefix, const std::string& npCommunicationId,
        const std::string& suffix, const std::string& npServiceName);

    Fetch fetch;
};

} // namespace psn

#endif // AKIRA_PSN_CLIENT_HPP
