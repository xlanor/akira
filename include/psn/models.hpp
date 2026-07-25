#ifndef AKIRA_PSN_MODELS_HPP
#define AKIRA_PSN_MODELS_HPP

#include <cstdint>
#include <string>
#include <vector>

struct json_object;

namespace psn {

// Owns a parsed document so an early return cannot leak it. json-c's ownership is manual
// and the paged fetches below have several exits per page.
class Json {
public:
    Json() = default;
    explicit Json(const std::string& text);
    ~Json();

    Json(const Json&) = delete;
    Json& operator=(const Json&) = delete;
    Json(Json&& other) noexcept;
    Json& operator=(Json&& other) noexcept;

    json_object* get() const { return root; }
    explicit operator bool() const { return root != nullptr; }

private:
    json_object* root = nullptr;
};

bool jsonField(json_object* parent, const char* key, json_object** out);
std::string jsonString(json_object* parent, const char* key);
bool jsonBool(json_object* parent, const char* key);

// PSN returns several numeric fields as strings — trophyEarnedRate, progress and
// trophyProgressTargetValue among them. These read either representation.
int jsonInt(json_object* parent, const char* key);
int64_t jsonInt64(json_object* parent, const char* key);
double jsonDouble(json_object* parent, const char* key);

struct TrophyCounts {
    int bronze = 0;
    int silver = 0;
    int gold = 0;
    int platinum = 0;

    int total() const { return bronze + silver + gold + platinum; }
};

struct TrophySummary {
    std::string accountId;
    int trophyLevel = 0;
    int tier = 0;
    int progress = 0;
    int trophyPoint = 0;
    int trophyLevelBasePoint = 0;
    int trophyLevelNextPoint = 0;
    TrophyCounts earnedTrophies;
};

struct TrophyTitle {
    std::string npCommunicationId;
    std::string npServiceName;
    std::string trophyTitleName;
    std::string trophyTitleDetail;
    std::string trophyTitleIconUrl;
    std::string trophyTitlePlatform;
    std::string trophySetVersion;
    bool hasTrophyGroups = false;
    int trophyGroupCount = 0;
    TrophyCounts definedTrophies;
    TrophyCounts earnedTrophies;
    int progress = 0;
    bool hiddenFlag = false;
    std::string lastUpdatedDateTime;
};

struct TrophyGroup {
    std::string trophyGroupId;
    std::string trophyGroupName;
    std::string trophyGroupDetail;
    std::string trophyGroupIconUrl;
    TrophyCounts definedTrophies;
    TrophyCounts earnedTrophies;
    int progress = 0;
    std::string lastUpdatedDateTime;
};

enum class TrophyRarity {
    UltraRare,
    VeryRare,
    Rare,
    Common
};

TrophyRarity rarityOf(int trophyRare);

struct Trophy {
    int trophyId = 0;
    std::string trophyName;
    std::string trophyDetail;
    std::string trophyIconUrl;
    std::string trophyType;
    std::string trophyGroupId;
    bool trophyHidden = false;

    bool earned = false;
    std::string earnedDateTime;
    int trophyRare = -1;
    double trophyEarnedRate = 0.0;

    bool hasProgress = false;
    int64_t progress = 0;
    int64_t progressTarget = 0;
    int progressRate = 0;
    std::string progressedDateTime;
};

// The bare and users/me forms of each endpoint carry disjoint fields, so a row is parsed
// by which endpoint it came from rather than by guessing which keys are present.
bool parseSummary(json_object* obj, TrophySummary& out);
bool parseTitle(json_object* obj, TrophyTitle& out);
bool parseGroupDefinition(json_object* obj, TrophyGroup& out);
bool parseGroupProgress(json_object* obj, TrophyGroup& out);
bool parseTrophyDefinition(json_object* obj, Trophy& out);
bool parseTrophyProgress(json_object* obj, Trophy& out);

// The disk cache stores the API's own field names, so these round-trip through the
// parsers above rather than needing a second field list to keep in step.
json_object* toJson(const TrophySummary& summary);
json_object* toJson(const TrophyTitle& title);

} // namespace psn

#endif // AKIRA_PSN_MODELS_HPP
