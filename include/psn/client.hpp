#ifndef AKIRA_PSN_CLIENT_HPP
#define AKIRA_PSN_CLIENT_HPP

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "psn/models.hpp"
#include "psn/status.hpp"

namespace psn {

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

    Error fetchPlayedGames(std::vector<PlayedGame>& out) const;

    static constexpr size_t TITLE_MAP_BATCH = 5;

    Error fetchTitleMapping(const std::vector<std::string>& titleIds,
        std::vector<std::pair<std::string, std::string>>& out) const;

private:
    using RowSink = std::function<bool(json_object* row)>;

    static constexpr const char* API_BASE = "https://m.np.playstation.com/api/trophy/v1";
    static constexpr const char* GAMELIST_BASE = "https://m.np.playstation.com/api/gamelist/v2/users";
    static constexpr int PAGE_SIZE = 100;
    static constexpr int PAGE_CAP = 50;

    Error fetchDocument(const char* base, const std::string& path, Json& out) const;
    Error fetchList(const char* base, const std::string& path, const char* arrayKey, const RowSink& onRow) const;
    Error fetchPaged(const char* base, const std::string& path, const char* arrayKey, const RowSink& onRow) const;

    static std::string titlePath(const std::string& prefix, const std::string& npCommunicationId,
        const std::string& suffix, const std::string& npServiceName);

    Fetch fetch;
};

} // namespace psn

#endif // AKIRA_PSN_CLIENT_HPP
