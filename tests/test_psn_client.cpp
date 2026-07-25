#include "test_util.hpp"

#include "psn/client.hpp"

#include <format>

using namespace psn;

namespace {

constexpr const char* BASE = "https://m.np.playstation.com/api/trophy/v1";

struct FakeApi {
    std::vector<std::string> requests;
    std::function<Error(const std::string& url, std::string& body)> handler;

    Client client()
    {
        return Client([this](const std::string& url, std::string& body) {
            requests.push_back(url);
            return handler(url, body);
        });
    }
};

std::string titleRow(int index)
{
    return std::format(R"({{"npCommunicationId": "NPWR{:05}_00", "trophyTitleName": "Game {}"}})", index, index);
}

std::string trophyRow(int id)
{
    return std::format(R"({{"trophyId": {}, "trophyName": "Trophy {}", "trophyType": "bronze"}})", id, id);
}

std::string joinRows(const std::vector<std::string>& rows)
{
    std::string joined;
    for (size_t i = 0; i < rows.size(); i++)
    {
        if (i > 0)
            joined += ",";
        joined += rows[i];
    }
    return joined;
}

std::string titlePage(int firstIndex, int count, int total, int nextOffset)
{
    std::vector<std::string> rows;
    for (int i = 0; i < count; i++)
        rows.push_back(titleRow(firstIndex + i));

    std::string body = std::format(R"({{"totalItemCount": {}, "trophyTitles": [{}]}})", total, joinRows(rows));

    if (nextOffset >= 0)
    {
        body.pop_back();
        body += std::format(R"(, "nextOffset": {}}})", nextOffset);
    }

    return body;
}

} // namespace

TEST(summary_is_a_single_unpaged_request)
{
    FakeApi api;
    api.handler = [](const std::string&, std::string& body) {
        body = R"({"accountId": "1", "trophyLevel": 328, "earnedTrophies": {"bronze": 95, "silver": 26, "gold": 6, "platinum": 0}})";
        return Error{};
    };

    TrophySummary summary;
    Error error = api.client().fetchSummary(summary);

    CHECK(error.ok());
    CHECK_EQ(api.requests.size(), size_t(1));
    CHECK_EQ(api.requests[0], std::string(BASE) + "/users/me/trophySummary");
    CHECK_EQ(summary.trophyLevel, 328);
    CHECK_EQ(summary.earnedTrophies.total(), 127);
}

TEST(titles_single_page_asks_for_limit_and_offset)
{
    FakeApi api;
    api.handler = [](const std::string&, std::string& body) {
        body = titlePage(0, 25, 25, -1);
        return Error{};
    };

    std::vector<TrophyTitle> titles;
    Error error = api.client().fetchTitles(titles);

    CHECK(error.ok());
    CHECK_EQ(api.requests.size(), size_t(1));
    CHECK_EQ(api.requests[0], std::string(BASE) + "/users/me/trophyTitles?limit=100&offset=0");
    CHECK_EQ(titles.size(), size_t(25));
    CHECK_EQ(titles[0].trophyTitleName, std::string("Game 0"));
    CHECK_EQ(titles[24].trophyTitleName, std::string("Game 24"));
}

TEST(titles_follow_next_offset_across_pages)
{
    FakeApi api;
    api.handler = [](const std::string& url, std::string& body) {
        if (url.find("offset=0") != std::string::npos)
            body = titlePage(0, 100, 250, 100);
        else if (url.find("offset=100") != std::string::npos)
            body = titlePage(100, 100, 250, 200);
        else
            body = titlePage(200, 50, 250, -1);

        return Error{};
    };

    std::vector<TrophyTitle> titles;
    Error error = api.client().fetchTitles(titles);

    CHECK(error.ok());
    CHECK_EQ(titles.size(), size_t(250));
    CHECK_EQ(api.requests.size(), size_t(3));
    CHECK_EQ(api.requests[0], std::string(BASE) + "/users/me/trophyTitles?limit=100&offset=0");
    CHECK_EQ(api.requests[1], std::string(BASE) + "/users/me/trophyTitles?limit=100&offset=100");
    CHECK_EQ(api.requests[2], std::string(BASE) + "/users/me/trophyTitles?limit=100&offset=200");
    CHECK_EQ(titles[0].trophyTitleName, std::string("Game 0"));
    CHECK_EQ(titles[249].trophyTitleName, std::string("Game 249"));
}

TEST(titles_stop_when_the_page_has_no_next_offset)
{
    FakeApi api;
    api.handler = [](const std::string& url, std::string& body) {
        body = url.find("offset=0") != std::string::npos
            ? titlePage(0, 100, 140, 100)
            : titlePage(100, 40, 140, -1);
        return Error{};
    };

    std::vector<TrophyTitle> titles;
    Error error = api.client().fetchTitles(titles);

    CHECK(error.ok());
    CHECK_EQ(api.requests.size(), size_t(2));
    CHECK_EQ(titles.size(), size_t(140));
}

TEST(a_next_offset_that_does_not_advance_is_an_error_not_a_short_list)
{
    FakeApi api;
    api.handler = [](const std::string&, std::string& body) {
        body = titlePage(0, 100, 250, 0);
        return Error{};
    };

    std::vector<TrophyTitle> titles;
    Error error = api.client().fetchTitles(titles);

    CHECK(!error.ok());
    CHECK(error.status == Status::ServerError);
    CHECK(error.message.find("did not advance") != std::string::npos);
}

TEST(runaway_pagination_stops_at_the_page_cap_with_an_error)
{
    FakeApi api;
    api.handler = [](const std::string& url, std::string& body) {
        size_t marker = url.find("offset=");
        int offset = std::stoi(url.substr(marker + 7));
        body = titlePage(offset, 100, 1000000, offset + 100);
        return Error{};
    };

    std::vector<TrophyTitle> titles;
    Error error = api.client().fetchTitles(titles);

    CHECK(!error.ok());
    CHECK(error.status == Status::ServerError);
    CHECK(error.message.find("page cap") != std::string::npos);
    CHECK_EQ(api.requests.size(), size_t(50));
}

TEST(a_failure_on_a_later_page_fails_the_whole_fetch)
{
    FakeApi api;
    api.handler = [](const std::string& url, std::string& body) -> Error {
        if (url.find("offset=0") != std::string::npos)
        {
            body = titlePage(0, 100, 250, 100);
            return Error{};
        }

        return Error{Status::RateLimited, "Request budget used up"};
    };

    std::vector<TrophyTitle> titles;
    Error error = api.client().fetchTitles(titles);

    CHECK(!error.ok());
    CHECK(error.status == Status::RateLimited);
    CHECK_EQ(error.message, std::string("Request budget used up"));
}

TEST(an_unparseable_body_is_a_server_error)
{
    FakeApi api;
    api.handler = [](const std::string&, std::string& body) {
        body = "<html>502 Bad Gateway</html>";
        return Error{};
    };

    std::vector<TrophyTitle> titles;
    Error error = api.client().fetchTitles(titles);

    CHECK(!error.ok());
    CHECK(error.status == Status::ServerError);
}

TEST(rows_without_an_id_are_skipped_without_failing_the_page)
{
    FakeApi api;
    api.handler = [](const std::string&, std::string& body) {
        body = std::format(R"({{"totalItemCount": 3, "trophyTitles": [{}, {{"trophyTitleName": "Broken"}}, {}]}})",
            titleRow(0), titleRow(2));
        return Error{};
    };

    std::vector<TrophyTitle> titles;
    Error error = api.client().fetchTitles(titles);

    CHECK(error.ok());
    CHECK_EQ(titles.size(), size_t(2));
}

TEST(per_trophy_endpoints_page_and_carry_the_service_name)
{
    FakeApi api;
    api.handler = [](const std::string& url, std::string& body) {
        if (url.find("offset=0") != std::string::npos)
        {
            std::vector<std::string> rows;
            for (int i = 0; i < 100; i++)
                rows.push_back(trophyRow(i));
            body = std::format(R"({{"totalItemCount": 165, "nextOffset": 100, "trophies": [{}]}})", joinRows(rows));
        }
        else
        {
            std::vector<std::string> rows;
            for (int i = 100; i < 165; i++)
                rows.push_back(trophyRow(i));
            body = std::format(R"({{"totalItemCount": 165, "trophies": [{}]}})", joinRows(rows));
        }

        return Error{};
    };

    std::vector<Trophy> trophies;
    Error error = api.client().fetchTrophyDefinitions("NPWR12345_00", "trophy2", trophies);

    CHECK(error.ok());
    CHECK_EQ(trophies.size(), size_t(165));
    CHECK_EQ(api.requests.size(), size_t(2));
    CHECK_EQ(api.requests[0], std::string(BASE) +
        "/npCommunicationIds/NPWR12345_00/trophyGroups/all/trophies?npServiceName=trophy2&limit=100&offset=0");
    CHECK_EQ(api.requests[1], std::string(BASE) +
        "/npCommunicationIds/NPWR12345_00/trophyGroups/all/trophies?npServiceName=trophy2&limit=100&offset=100");
    CHECK_EQ(trophies[164].trophyId, 164);
}

TEST(earned_trophies_use_the_users_me_path)
{
    FakeApi api;
    api.handler = [](const std::string&, std::string& body) {
        body = R"({"totalItemCount": 1, "trophies": [{"trophyId": 1, "earned": true, "trophyRare": 0, "trophyEarnedRate": "0.4", "trophyType": "platinum"}]})";
        return Error{};
    };

    std::vector<Trophy> trophies;
    Error error = api.client().fetchTrophyProgress("NPWR12345_00", "trophy2", trophies);

    CHECK(error.ok());
    CHECK_EQ(api.requests[0], std::string(BASE) +
        "/users/me/npCommunicationIds/NPWR12345_00/trophyGroups/all/trophies?npServiceName=trophy2&limit=100&offset=0");
    CHECK_EQ(trophies.size(), size_t(1));
    CHECK_EQ(trophies[0].earned, true);
    CHECK(trophies[0].trophyEarnedRate > 0.39 && trophies[0].trophyEarnedRate < 0.41);
}

TEST(group_listings_are_not_paged)
{
    FakeApi api;
    api.handler = [](const std::string&, std::string& body) {
        body = R"({"trophyGroups": [
            {"trophyGroupId": "default", "trophyGroupName": "Base", "definedTrophies": {"bronze": 30, "silver": 8, "gold": 3, "platinum": 1}},
            {"trophyGroupId": "001", "trophyGroupName": "DLC", "definedTrophies": {"bronze": 5, "silver": 2, "gold": 1, "platinum": 0}}
        ]})";
        return Error{};
    };

    std::vector<TrophyGroup> groups;
    Error error = api.client().fetchGroupDefinitions("NPWR12345_00", "trophy2", groups);

    CHECK(error.ok());
    CHECK_EQ(api.requests.size(), size_t(1));
    CHECK_EQ(api.requests[0], std::string(BASE) +
        "/npCommunicationIds/NPWR12345_00/trophyGroups?npServiceName=trophy2");
    CHECK(api.requests[0].find("limit=") == std::string::npos);
    CHECK_EQ(groups.size(), size_t(2));
    CHECK_EQ(groups[1].trophyGroupName, std::string("DLC"));
}

TEST(group_progress_uses_the_users_me_path)
{
    FakeApi api;
    api.handler = [](const std::string&, std::string& body) {
        body = R"({"trophyGroups": [{"trophyGroupId": "default", "earnedTrophies": {"bronze": 2, "silver": 0, "gold": 0, "platinum": 0}, "progress": 2}]})";
        return Error{};
    };

    std::vector<TrophyGroup> groups;
    Error error = api.client().fetchGroupProgress("NPWR12345_00", "", groups);

    CHECK(error.ok());
    CHECK_EQ(api.requests[0], std::string(BASE) + "/users/me/npCommunicationIds/NPWR12345_00/trophyGroups");
    CHECK_EQ(groups.size(), size_t(1));
    CHECK_EQ(groups[0].progress, 2);
}

TEST(a_missing_array_key_is_a_server_error)
{
    FakeApi api;
    api.handler = [](const std::string&, std::string& body) {
        body = R"({"totalItemCount": 0})";
        return Error{};
    };

    std::vector<TrophyGroup> groups;
    Error error = api.client().fetchGroupDefinitions("NPWR12345_00", "trophy2", groups);

    CHECK(!error.ok());
    CHECK(error.status == Status::ServerError);
}

TEST(an_empty_paged_result_succeeds_with_no_rows)
{
    FakeApi api;
    api.handler = [](const std::string&, std::string& body) {
        body = R"({"totalItemCount": 0, "trophyTitles": []})";
        return Error{};
    };

    std::vector<TrophyTitle> titles;
    Error error = api.client().fetchTitles(titles);

    CHECK(error.ok());
    CHECK(titles.empty());
    CHECK_EQ(api.requests.size(), size_t(1));
}
