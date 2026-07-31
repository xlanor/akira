#include "psn/models.hpp"

#include <cstdlib>
#include <cerrno>
#include <unordered_map>

#include <json-c/json.h>

namespace psn {

Json::Json(const std::string& text)
{
    if (!text.empty())
        root = json_tokener_parse(text.c_str());
}

Json::~Json()
{
    if (root)
        json_object_put(root);
}

Json::Json(Json&& other) noexcept
    : root(other.root)
{
    other.root = nullptr;
}

Json& Json::operator=(Json&& other) noexcept
{
    if (this != &other)
    {
        if (root)
            json_object_put(root);

        root = other.root;
        other.root = nullptr;
    }

    return *this;
}

bool jsonField(json_object* parent, const char* key, json_object** out)
{
    return parent && json_object_object_get_ex(parent, key, out) && *out &&
        !json_object_is_type(*out, json_type_null);
}

static std::string sanitizeApiText(const char* raw)
{
    if (!raw)
        return std::string();

    std::string value(raw);

    for (char& c : value)
    {
        if (c == '\r' || c == '\n' || c == '\t')
            c = ' ';
    }

    size_t begin = value.find_first_not_of(' ');
    if (begin == std::string::npos)
        return std::string();

    size_t end = value.find_last_not_of(' ');
    return value.substr(begin, end - begin + 1);
}

std::string jsonString(json_object* parent, const char* key)
{
    json_object* field = nullptr;
    if (!jsonField(parent, key, &field))
        return std::string();

    return sanitizeApiText(json_object_get_string(field));
}

bool jsonBool(json_object* parent, const char* key)
{
    json_object* field = nullptr;
    if (!jsonField(parent, key, &field))
        return false;

    return json_object_get_boolean(field);
}

int64_t jsonInt64(json_object* parent, const char* key)
{
    json_object* field = nullptr;
    if (!jsonField(parent, key, &field))
        return 0;

    if (json_object_is_type(field, json_type_string))
    {
        const char* value = json_object_get_string(field);
        if (!value)
            return 0;

        errno = 0;
        char* end = nullptr;
        long long parsed = strtoll(value, &end, 10);

        if (errno != 0 || end == value)
            return 0;

        return static_cast<int64_t>(parsed);
    }

    return json_object_get_int64(field);
}

int jsonInt(json_object* parent, const char* key)
{
    return static_cast<int>(jsonInt64(parent, key));
}

double jsonDouble(json_object* parent, const char* key)
{
    json_object* field = nullptr;
    if (!jsonField(parent, key, &field))
        return 0.0;

    if (json_object_is_type(field, json_type_string))
    {
        const char* value = json_object_get_string(field);
        if (!value)
            return 0.0;

        errno = 0;
        char* end = nullptr;
        double parsed = strtod(value, &end);

        if (errno != 0 || end == value)
            return 0.0;

        return parsed;
    }

    return json_object_get_double(field);
}

static TrophyCounts jsonCounts(json_object* parent, const char* key)
{
    TrophyCounts counts;

    json_object* field = nullptr;
    if (!jsonField(parent, key, &field))
        return counts;

    counts.bronze = jsonInt(field, "bronze");
    counts.silver = jsonInt(field, "silver");
    counts.gold = jsonInt(field, "gold");
    counts.platinum = jsonInt(field, "platinum");
    return counts;
}

static json_object* countsToJson(const TrophyCounts& counts)
{
    json_object* obj = json_object_new_object();
    json_object_object_add(obj, "bronze", json_object_new_int(counts.bronze));
    json_object_object_add(obj, "silver", json_object_new_int(counts.silver));
    json_object_object_add(obj, "gold", json_object_new_int(counts.gold));
    json_object_object_add(obj, "platinum", json_object_new_int(counts.platinum));
    return obj;
}

static void addString(json_object* parent, const char* key, const std::string& value)
{
    json_object_object_add(parent, key, json_object_new_string(value.c_str()));
}

TrophyRarity rarityOf(int trophyRare)
{
    switch (trophyRare)
    {
        case 0: return TrophyRarity::UltraRare;
        case 1: return TrophyRarity::VeryRare;
        case 2: return TrophyRarity::Rare;
        default: return TrophyRarity::Common;
    }
}

int64_t parseIso8601Duration(const std::string& value)
{
    if (value.size() < 2 || value[0] != 'P')
        return 0;

    int64_t total = 0;
    int64_t number = 0;
    bool haveNumber = false;
    bool inTime = false;

    for (size_t i = 1; i < value.size(); i++)
    {
        char c = value[i];

        if (c == 'T')
        {
            inTime = true;
            number = 0;
            haveNumber = false;
            continue;
        }

        if (c >= '0' && c <= '9')
        {
            number = number * 10 + (c - '0');
            haveNumber = true;
            continue;
        }

        if (!haveNumber)
            return 0;

        switch (c)
        {
            case 'D': total += number * 86400; break;
            case 'W': total += number * 604800; break;
            case 'H': total += inTime ? number * 3600 : 0; break;
            case 'M': total += inTime ? number * 60 : number * 2592000; break;
            case 'S': total += inTime ? number : 0; break;
            default: return 0;
        }

        number = 0;
        haveNumber = false;
    }

    return total;
}

static int64_t daysFromCivil(int64_t y, int64_t m, int64_t d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int64_t parseIso8601Timestamp(const std::string& value)
{
    if (value.size() < 19 || value[4] != '-' || value[7] != '-' || value[10] != 'T')
        return 0;

    auto number = [&value](size_t offset, size_t length) -> int64_t {
        int64_t result = 0;
        for (size_t i = offset; i < offset + length; i++)
        {
            if (value[i] < '0' || value[i] > '9')
                return -1;
            result = result * 10 + (value[i] - '0');
        }
        return result;
    };

    int64_t year = number(0, 4);
    int64_t month = number(5, 2);
    int64_t day = number(8, 2);
    int64_t hour = number(11, 2);
    int64_t minute = number(14, 2);
    int64_t second = number(17, 2);

    if (year <= 0 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60)
        return 0;

    return daysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
}

bool parsePlayedGame(json_object* obj, PlayedGame& out)
{
    if (!obj)
        return false;

    out.titleId = jsonString(obj, "titleId");
    if (out.titleId.empty())
        return false;

    out.name = jsonString(obj, "localizedName");
    if (out.name.empty())
        out.name = jsonString(obj, "name");

    out.imageUrl = jsonString(obj, "localizedImageUrl");
    if (out.imageUrl.empty())
        out.imageUrl = jsonString(obj, "imageUrl");

    out.category = jsonString(obj, "category");
    out.playCount = jsonInt(obj, "playCount");
    out.playDurationSeconds = parseIso8601Duration(jsonString(obj, "playDuration"));
    out.firstPlayedDateTime = jsonString(obj, "firstPlayedDateTime");
    out.lastPlayedDateTime = jsonString(obj, "lastPlayedDateTime");
    return true;
}

bool parseSummary(json_object* obj, TrophySummary& out)
{
    if (!obj)
        return false;

    out.accountId = jsonString(obj, "accountId");
    out.trophyLevel = jsonInt(obj, "trophyLevel");
    out.tier = jsonInt(obj, "tier");
    out.progress = jsonInt(obj, "progress");
    out.trophyPoint = jsonInt(obj, "trophyPoint");
    out.trophyLevelBasePoint = jsonInt(obj, "trophyLevelBasePoint");
    out.trophyLevelNextPoint = jsonInt(obj, "trophyLevelNextPoint");
    out.earnedTrophies = jsonCounts(obj, "earnedTrophies");
    return true;
}

bool parseTitle(json_object* obj, TrophyTitle& out)
{
    if (!obj)
        return false;

    out.npCommunicationId = jsonString(obj, "npCommunicationId");
    if (out.npCommunicationId.empty())
        return false;

    out.npServiceName = jsonString(obj, "npServiceName");
    out.trophyTitleName = jsonString(obj, "trophyTitleName");
    out.trophyTitleDetail = jsonString(obj, "trophyTitleDetail");
    out.trophyTitleIconUrl = jsonString(obj, "trophyTitleIconUrl");
    out.trophyTitlePlatform = jsonString(obj, "trophyTitlePlatform");
    out.trophySetVersion = jsonString(obj, "trophySetVersion");
    out.hasTrophyGroups = jsonBool(obj, "hasTrophyGroups");
    out.trophyGroupCount = jsonInt(obj, "trophyGroupCount");
    out.definedTrophies = jsonCounts(obj, "definedTrophies");
    out.earnedTrophies = jsonCounts(obj, "earnedTrophies");
    out.progress = jsonInt(obj, "progress");
    out.hiddenFlag = jsonBool(obj, "hiddenFlag");
    out.lastUpdatedDateTime = jsonString(obj, "lastUpdatedDateTime");
    return true;
}

bool parseGroupDefinition(json_object* obj, TrophyGroup& out)
{
    if (!obj)
        return false;

    out.trophyGroupId = jsonString(obj, "trophyGroupId");
    if (out.trophyGroupId.empty())
        return false;

    out.trophyGroupName = jsonString(obj, "trophyGroupName");
    out.trophyGroupDetail = jsonString(obj, "trophyGroupDetail");
    out.trophyGroupIconUrl = jsonString(obj, "trophyGroupIconUrl");
    out.definedTrophies = jsonCounts(obj, "definedTrophies");
    return true;
}

bool parseGroupProgress(json_object* obj, TrophyGroup& out)
{
    if (!obj)
        return false;

    out.trophyGroupId = jsonString(obj, "trophyGroupId");
    if (out.trophyGroupId.empty())
        return false;

    out.earnedTrophies = jsonCounts(obj, "earnedTrophies");
    out.progress = jsonInt(obj, "progress");
    out.lastUpdatedDateTime = jsonString(obj, "lastUpdatedDateTime");
    return true;
}

bool parseTrophyDefinition(json_object* obj, Trophy& out)
{
    if (!obj)
        return false;

    json_object* id = nullptr;
    if (!jsonField(obj, "trophyId", &id))
        return false;

    out.trophyId = jsonInt(obj, "trophyId");
    out.trophyName = jsonString(obj, "trophyName");
    out.trophyDetail = jsonString(obj, "trophyDetail");
    out.trophyIconUrl = jsonString(obj, "trophyIconUrl");
    out.trophyType = jsonString(obj, "trophyType");
    out.trophyGroupId = jsonString(obj, "trophyGroupId");
    out.trophyHidden = jsonBool(obj, "trophyHidden");

    json_object* target = nullptr;
    if (jsonField(obj, "trophyProgressTargetValue", &target))
    {
        out.hasProgress = true;
        out.progressTarget = jsonInt64(obj, "trophyProgressTargetValue");
    }

    return true;
}

bool parseTrophyProgress(json_object* obj, Trophy& out)
{
    if (!obj)
        return false;

    json_object* id = nullptr;
    if (!jsonField(obj, "trophyId", &id))
        return false;

    out.trophyId = jsonInt(obj, "trophyId");
    out.trophyType = jsonString(obj, "trophyType");
    out.trophyHidden = jsonBool(obj, "trophyHidden");
    out.earned = jsonBool(obj, "earned");
    out.earnedDateTime = jsonString(obj, "earnedDateTime");
    out.trophyRare = jsonInt(obj, "trophyRare");
    out.trophyEarnedRate = jsonDouble(obj, "trophyEarnedRate");
    out.progressedDateTime = jsonString(obj, "progressedDateTime");

    json_object* progress = nullptr;
    if (jsonField(obj, "progress", &progress))
    {
        out.hasProgress = true;
        out.progress = jsonInt64(obj, "progress");
        out.progressRate = jsonInt(obj, "progressRate");
    }

    return true;
}

void mergeTrophies(std::vector<Trophy>& definitions, const std::vector<Trophy>& progress)
{
    std::unordered_map<int, const Trophy*> byId;
    byId.reserve(progress.size());

    for (const Trophy& row : progress)
        byId[row.trophyId] = &row;

    for (Trophy& target : definitions)
    {
        auto found = byId.find(target.trophyId);
        if (found == byId.end())
            continue;

        const Trophy& state = *found->second;

        target.earned = state.earned;
        target.earnedDateTime = state.earnedDateTime;
        target.trophyRare = state.trophyRare;
        target.trophyEarnedRate = state.trophyEarnedRate;

        if (state.hasProgress)
        {
            target.hasProgress = true;
            target.progress = state.progress;
            target.progressRate = state.progressRate;
            target.progressedDateTime = state.progressedDateTime;
        }
    }
}

void mergeGroups(std::vector<TrophyGroup>& definitions, const std::vector<TrophyGroup>& progress)
{
    std::unordered_map<std::string, const TrophyGroup*> byId;
    byId.reserve(progress.size());

    for (const TrophyGroup& row : progress)
        byId[row.trophyGroupId] = &row;

    for (TrophyGroup& target : definitions)
    {
        auto found = byId.find(target.trophyGroupId);
        if (found == byId.end())
            continue;

        const TrophyGroup& state = *found->second;

        target.earnedTrophies = state.earnedTrophies;
        target.progress = state.progress;
        target.lastUpdatedDateTime = state.lastUpdatedDateTime;
    }
}

void tallyGroupEarned(std::vector<TrophyGroup>& groups, const std::vector<Trophy>& trophies)
{
    std::unordered_map<std::string, TrophyCounts> counts;

    for (const Trophy& trophy : trophies)
    {
        if (!trophy.earned)
            continue;

        TrophyCounts& bucket = counts[trophy.trophyGroupId];

        if (trophy.trophyType == "bronze")
            bucket.bronze++;
        else if (trophy.trophyType == "silver")
            bucket.silver++;
        else if (trophy.trophyType == "gold")
            bucket.gold++;
        else if (trophy.trophyType == "platinum")
            bucket.platinum++;
    }

    for (TrophyGroup& group : groups)
    {
        auto found = counts.find(group.trophyGroupId);
        group.earnedTrophies = found == counts.end() ? TrophyCounts{} : found->second;
    }
}

json_object* toJson(const TrophyGroup& group)
{
    json_object* obj = json_object_new_object();
    addString(obj, "trophyGroupId", group.trophyGroupId);
    addString(obj, "trophyGroupName", group.trophyGroupName);
    addString(obj, "trophyGroupDetail", group.trophyGroupDetail);
    addString(obj, "trophyGroupIconUrl", group.trophyGroupIconUrl);
    json_object_object_add(obj, "definedTrophies", countsToJson(group.definedTrophies));
    json_object_object_add(obj, "earnedTrophies", countsToJson(group.earnedTrophies));
    json_object_object_add(obj, "progress", json_object_new_int(group.progress));
    addString(obj, "lastUpdatedDateTime", group.lastUpdatedDateTime);
    return obj;
}

json_object* toJson(const Trophy& trophy)
{
    json_object* obj = json_object_new_object();
    json_object_object_add(obj, "trophyId", json_object_new_int(trophy.trophyId));
    addString(obj, "trophyName", trophy.trophyName);
    addString(obj, "trophyDetail", trophy.trophyDetail);
    addString(obj, "trophyIconUrl", trophy.trophyIconUrl);
    addString(obj, "trophyType", trophy.trophyType);
    addString(obj, "trophyGroupId", trophy.trophyGroupId);
    json_object_object_add(obj, "trophyHidden", json_object_new_boolean(trophy.trophyHidden));
    json_object_object_add(obj, "earned", json_object_new_boolean(trophy.earned));
    addString(obj, "earnedDateTime", trophy.earnedDateTime);
    json_object_object_add(obj, "trophyRare", json_object_new_int(trophy.trophyRare));
    json_object_object_add(obj, "trophyEarnedRate", json_object_new_double(trophy.trophyEarnedRate));
    json_object_object_add(obj, "hasProgress", json_object_new_boolean(trophy.hasProgress));
    json_object_object_add(obj, "progress", json_object_new_int64(trophy.progress));
    json_object_object_add(obj, "progressTarget", json_object_new_int64(trophy.progressTarget));
    json_object_object_add(obj, "progressRate", json_object_new_int(trophy.progressRate));
    addString(obj, "progressedDateTime", trophy.progressedDateTime);
    return obj;
}

bool parseCachedGroup(json_object* obj, TrophyGroup& out)
{
    if (!parseGroupDefinition(obj, out))
        return false;

    out.earnedTrophies = jsonCounts(obj, "earnedTrophies");
    out.progress = jsonInt(obj, "progress");
    out.lastUpdatedDateTime = jsonString(obj, "lastUpdatedDateTime");
    return true;
}

bool parseCachedTrophy(json_object* obj, Trophy& out)
{
    if (!parseTrophyDefinition(obj, out))
        return false;

    out.earned = jsonBool(obj, "earned");
    out.earnedDateTime = jsonString(obj, "earnedDateTime");
    out.trophyRare = jsonInt(obj, "trophyRare");
    out.trophyEarnedRate = jsonDouble(obj, "trophyEarnedRate");
    out.progressedDateTime = jsonString(obj, "progressedDateTime");

    if (jsonBool(obj, "hasProgress"))
    {
        out.hasProgress = true;
        out.progress = jsonInt64(obj, "progress");
        out.progressTarget = jsonInt64(obj, "progressTarget");
        out.progressRate = jsonInt(obj, "progressRate");
    }

    return true;
}

json_object* toJson(const TrophySummary& summary)
{
    json_object* obj = json_object_new_object();
    addString(obj, "accountId", summary.accountId);
    json_object_object_add(obj, "trophyLevel", json_object_new_int(summary.trophyLevel));
    json_object_object_add(obj, "tier", json_object_new_int(summary.tier));
    json_object_object_add(obj, "progress", json_object_new_int(summary.progress));
    json_object_object_add(obj, "trophyPoint", json_object_new_int(summary.trophyPoint));
    json_object_object_add(obj, "trophyLevelBasePoint", json_object_new_int(summary.trophyLevelBasePoint));
    json_object_object_add(obj, "trophyLevelNextPoint", json_object_new_int(summary.trophyLevelNextPoint));
    json_object_object_add(obj, "earnedTrophies", countsToJson(summary.earnedTrophies));
    return obj;
}

json_object* toJson(const TrophyTitle& title)
{
    json_object* obj = json_object_new_object();
    addString(obj, "npCommunicationId", title.npCommunicationId);
    addString(obj, "npServiceName", title.npServiceName);
    addString(obj, "trophyTitleName", title.trophyTitleName);
    addString(obj, "trophyTitleDetail", title.trophyTitleDetail);
    addString(obj, "trophyTitleIconUrl", title.trophyTitleIconUrl);
    addString(obj, "trophyTitlePlatform", title.trophyTitlePlatform);
    addString(obj, "trophySetVersion", title.trophySetVersion);
    json_object_object_add(obj, "hasTrophyGroups", json_object_new_boolean(title.hasTrophyGroups));
    json_object_object_add(obj, "trophyGroupCount", json_object_new_int(title.trophyGroupCount));
    json_object_object_add(obj, "definedTrophies", countsToJson(title.definedTrophies));
    json_object_object_add(obj, "earnedTrophies", countsToJson(title.earnedTrophies));
    json_object_object_add(obj, "progress", json_object_new_int(title.progress));
    json_object_object_add(obj, "hiddenFlag", json_object_new_boolean(title.hiddenFlag));
    addString(obj, "lastUpdatedDateTime", title.lastUpdatedDateTime);
    return obj;
}

bool parseProfile(json_object* obj, PsnProfile& out)
{
    if (!obj)
        return false;

    out.onlineId = jsonString(obj, "onlineId");
    out.aboutMe = jsonString(obj, "aboutMe");
    out.isPlus = jsonBool(obj, "isPlus");
    out.isOfficiallyVerified = jsonBool(obj, "isOfficiallyVerified");

    json_object* avatars = nullptr;
    if (jsonField(obj, "avatars", &avatars) && json_object_is_type(avatars, json_type_array))
    {
        size_t count = json_object_array_length(avatars);
        for (size_t i = 0; i < count; i++)
        {
            json_object* entry = json_object_array_get_idx(avatars, i);
            PsnAvatar avatar;
            avatar.size = jsonString(entry, "size");
            avatar.url = jsonString(entry, "url");
            if (!avatar.url.empty())
                out.avatars.push_back(std::move(avatar));
        }
    }

    return !out.onlineId.empty();
}

} // namespace psn
