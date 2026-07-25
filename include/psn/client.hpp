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

class Client {
public:
    using Fetch = std::function<Error(const std::string& url, std::string& outBody)>;

    explicit Client(Fetch fetch);

    Error fetchSummary(TrophySummary& out) const;
    Error fetchTitles(std::vector<TrophyTitle>& out) const;

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
