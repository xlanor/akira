#include "psn/client.hpp"
#include "psn/log.hpp"

#include <format>

#include <json-c/json.h>

namespace psn {

Client::Client(Fetch fetch)
    : fetch(std::move(fetch))
{
}

std::string Client::titlePath(const std::string& prefix, const std::string& npCommunicationId,
    const std::string& suffix, const std::string& npServiceName)
{
    std::string path = prefix + "/npCommunicationIds/" + npCommunicationId + suffix;

    if (!npServiceName.empty())
        path += "?npServiceName=" + npServiceName;

    return path;
}

Error Client::fetchDocument(const std::string& path, Json& out) const
{
    std::string body;
    Error error = fetch(std::string(API_BASE) + path, body);
    if (!error.ok())
        return error;

    out = Json(body);
    if (!out)
    {
        Error parseError{Status::ServerError, std::format("Could not parse the response to {}", path)};
        logError("PSN: {}", parseError.message);
        return parseError;
    }

    return {};
}

Error Client::fetchList(const std::string& path, const char* arrayKey, const RowSink& onRow) const
{
    Json doc;
    Error error = fetchDocument(path, doc);
    if (!error.ok())
        return error;

    json_object* array = nullptr;
    if (!jsonField(doc.get(), arrayKey, &array) || !json_object_is_type(array, json_type_array))
    {
        Error missing{Status::ServerError, std::format("Response to {} has no {} array", path, arrayKey)};
        logError("PSN: {}", missing.message);
        return missing;
    }

    size_t count = json_object_array_length(array);
    int rows = 0;

    for (size_t i = 0; i < count; i++)
    {
        if (onRow(json_object_array_get_idx(array, i)))
            rows++;
    }

    logInfo("PSN: read {} {} row(s)", rows, arrayKey);
    return {};
}

Error Client::fetchPaged(const std::string& path, const char* arrayKey, const RowSink& onRow) const
{
    const char* separator = path.find('?') == std::string::npos ? "?" : "&";

    int offset = 0;
    int page = 0;
    int rows = 0;
    int totalItemCount = -1;

    while (true)
    {
        page++;

        Json doc;
        Error error = fetchDocument(
            std::format("{}{}limit={}&offset={}", path, separator, PAGE_SIZE, offset), doc);
        if (!error.ok())
            return error;

        if (totalItemCount < 0)
            totalItemCount = jsonInt(doc.get(), "totalItemCount");

        json_object* array = nullptr;
        int pageCount = 0;

        if (jsonField(doc.get(), arrayKey, &array) && json_object_is_type(array, json_type_array))
        {
            pageCount = static_cast<int>(json_object_array_length(array));

            for (int i = 0; i < pageCount; i++)
            {
                if (onRow(json_object_array_get_idx(array, i)))
                    rows++;
            }
        }

        json_object* nextField = nullptr;
        bool hasNext = jsonField(doc.get(), "nextOffset", &nextField);
        int nextOffset = hasNext ? jsonInt(doc.get(), "nextOffset") : 0;

        if (pageCount == 0 || !hasNext)
            break;

        if (nextOffset <= offset)
        {
            Error stalled{Status::ServerError,
                std::format("{} paging stalled: nextOffset {} did not advance past {}",
                    arrayKey, nextOffset, offset)};
            logError("PSN: {}", stalled.message);
            return stalled;
        }

        if (page >= PAGE_CAP)
        {
            Error capped{Status::ServerError,
                std::format("{} paging hit the {}-page cap with {} row(s) read", arrayKey, PAGE_CAP, rows)};
            logError("PSN: {}", capped.message);
            return capped;
        }

        offset = nextOffset;
    }

    if (totalItemCount >= 0 && rows != totalItemCount)
    {
        logWarning("PSN: read {} {} row(s) over {} page(s) but totalItemCount says {}",
            rows, arrayKey, page, totalItemCount);
    }
    else
    {
        logInfo("PSN: read {} {} row(s) over {} page(s)", rows, arrayKey, page);
    }

    return {};
}

Error Client::fetchSummary(TrophySummary& out) const
{
    Json doc;
    Error error = fetchDocument("/users/me/trophySummary", doc);
    if (!error.ok())
        return error;

    if (!parseSummary(doc.get(), out))
        return {Status::ServerError, "trophySummary response was empty"};

    return {};
}

Error Client::fetchTitles(std::vector<TrophyTitle>& out) const
{
    return fetchPaged("/users/me/trophyTitles", "trophyTitles", [&out](json_object* row) {
        TrophyTitle title;
        if (!parseTitle(row, title))
        {
            logWarning("PSN: skipping a trophyTitles row with no npCommunicationId");
            return false;
        }

        out.push_back(std::move(title));
        return true;
    });
}

Error Client::fetchGroupDefinitions(const std::string& npCommunicationId,
    const std::string& npServiceName, std::vector<TrophyGroup>& out) const
{
    return fetchList(titlePath("", npCommunicationId, "/trophyGroups", npServiceName),
        "trophyGroups", [&out](json_object* row) {
            TrophyGroup group;
            if (!parseGroupDefinition(row, group))
                return false;

            out.push_back(std::move(group));
            return true;
        });
}

Error Client::fetchGroupProgress(const std::string& npCommunicationId,
    const std::string& npServiceName, std::vector<TrophyGroup>& out) const
{
    return fetchList(titlePath("/users/me", npCommunicationId, "/trophyGroups", npServiceName),
        "trophyGroups", [&out](json_object* row) {
            TrophyGroup group;
            if (!parseGroupProgress(row, group))
                return false;

            out.push_back(std::move(group));
            return true;
        });
}

Error Client::fetchTrophyDefinitions(const std::string& npCommunicationId,
    const std::string& npServiceName, std::vector<Trophy>& out) const
{
    return fetchPaged(titlePath("", npCommunicationId, "/trophyGroups/all/trophies", npServiceName),
        "trophies", [&out](json_object* row) {
            Trophy trophy;
            if (!parseTrophyDefinition(row, trophy))
                return false;

            out.push_back(std::move(trophy));
            return true;
        });
}

Error Client::fetchTrophyProgress(const std::string& npCommunicationId,
    const std::string& npServiceName, std::vector<Trophy>& out) const
{
    return fetchPaged(titlePath("/users/me", npCommunicationId, "/trophyGroups/all/trophies", npServiceName),
        "trophies", [&out](json_object* row) {
            Trophy trophy;
            if (!parseTrophyProgress(row, trophy))
                return false;

            out.push_back(std::move(trophy));
            return true;
        });
}

} // namespace psn
