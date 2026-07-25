#include "psn/models.hpp"

#include <cstdlib>
#include <cerrno>

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

// Trims and flattens whitespace. A live account returned a title name with a trailing
// newline, which pushed the card label off its line.
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

} // namespace psn
