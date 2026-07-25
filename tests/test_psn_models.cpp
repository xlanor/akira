#include "test_util.hpp"

#include "psn/models.hpp"

#include <json-c/json.h>

using namespace psn;

namespace {

struct Doc {
    explicit Doc(const char* text)
        : json(text)
    {
    }

    json_object* get() const { return json.get(); }

    Json json;
};

} // namespace

TEST(json_reads_numbers_given_as_strings)
{
    Doc doc(R"({"asInt": 165, "asString": "165", "big": "4294967296", "rate": "0.4", "rateNum": 12.5})");

    CHECK_EQ(jsonInt(doc.get(), "asInt"), 165);
    CHECK_EQ(jsonInt(doc.get(), "asString"), 165);
    CHECK_EQ(jsonInt64(doc.get(), "big"), int64_t(4294967296LL));
    CHECK(jsonDouble(doc.get(), "rate") > 0.39 && jsonDouble(doc.get(), "rate") < 0.41);
    CHECK(jsonDouble(doc.get(), "rateNum") > 12.4 && jsonDouble(doc.get(), "rateNum") < 12.6);
}

TEST(json_missing_and_null_fields_take_defaults)
{
    Doc doc(R"({"present": 1, "nulled": null, "notANumber": "abc"})");

    CHECK_EQ(jsonInt(doc.get(), "absent"), 0);
    CHECK_EQ(jsonInt(doc.get(), "nulled"), 0);
    CHECK_EQ(jsonInt(doc.get(), "notANumber"), 0);
    CHECK_EQ(jsonString(doc.get(), "absent"), std::string());
    CHECK_EQ(jsonBool(doc.get(), "absent"), false);
    CHECK_EQ(jsonDouble(doc.get(), "absent"), 0.0);

    json_object* field = nullptr;
    CHECK(!jsonField(doc.get(), "nulled", &field));
    CHECK(jsonField(doc.get(), "present", &field));
}

TEST(json_strings_are_trimmed_and_flattened)
{
    Doc doc(R"({"name": "Overcooked! All You Can Eat\n", "wrapped": "  two\nlines  ", "blank": "   "})");

    CHECK_EQ(jsonString(doc.get(), "name"), std::string("Overcooked! All You Can Eat"));
    CHECK_EQ(jsonString(doc.get(), "wrapped"), std::string("two lines"));
    CHECK_EQ(jsonString(doc.get(), "blank"), std::string());
}

TEST(parse_title_reads_a_library_row)
{
    Doc doc(R"({
        "npCommunicationId": "NPWR12345_00",
        "npServiceName": "trophy2",
        "trophyTitleName": "Some Game",
        "trophyTitleDetail": "A game",
        "trophyTitleIconUrl": "https://image.api.playstation.com/x.png",
        "trophyTitlePlatform": "PS5,PSPC",
        "trophySetVersion": "01.03",
        "hasTrophyGroups": true,
        "trophyGroupCount": 4,
        "definedTrophies": {"bronze": 30, "silver": 8, "gold": 3, "platinum": 1},
        "earnedTrophies": {"bronze": 12, "silver": 2, "gold": 0, "platinum": 0},
        "progress": 27,
        "hiddenFlag": false,
        "lastUpdatedDateTime": "2026-01-02T03:04:05Z"
    })");

    TrophyTitle title;
    CHECK(parseTitle(doc.get(), title));
    CHECK_EQ(title.npCommunicationId, std::string("NPWR12345_00"));
    CHECK_EQ(title.npServiceName, std::string("trophy2"));
    CHECK_EQ(title.trophyTitlePlatform, std::string("PS5,PSPC"));
    CHECK_EQ(title.hasTrophyGroups, true);
    CHECK_EQ(title.trophyGroupCount, 4);
    CHECK_EQ(title.definedTrophies.total(), 42);
    CHECK_EQ(title.earnedTrophies.total(), 14);
    CHECK_EQ(title.progress, 27);
}

TEST(parse_title_rejects_a_row_without_an_id)
{
    Doc doc(R"({"trophyTitleName": "Nameless"})");

    TrophyTitle title;
    CHECK(!parseTitle(doc.get(), title));
}

TEST(parse_summary_reads_the_account_row)
{
    Doc doc(R"({
        "accountId": "1234567890",
        "trophyLevel": 328,
        "tier": 4,
        "progress": 41,
        "trophyPoint": 25130,
        "trophyLevelBasePoint": 24000,
        "trophyLevelNextPoint": 27000,
        "earnedTrophies": {"bronze": 95, "silver": 26, "gold": 6, "platinum": 0}
    })");

    TrophySummary summary;
    CHECK(parseSummary(doc.get(), summary));
    CHECK_EQ(summary.trophyLevel, 328);
    CHECK_EQ(summary.earnedTrophies.total(), 127);
    CHECK_EQ(summary.earnedTrophies.bronze, 95);
}

TEST(parse_trophy_definition_reads_a_string_progress_target)
{
    Doc doc(R"({
        "trophyId": 12,
        "trophyName": "Marathon",
        "trophyDetail": "Run far",
        "trophyIconUrl": "https://psnobj.prod.dl.playstation.net/x.png",
        "trophyType": "gold",
        "trophyGroupId": "default",
        "trophyHidden": false,
        "trophyProgressTargetValue": "42195"
    })");

    Trophy trophy;
    CHECK(parseTrophyDefinition(doc.get(), trophy));
    CHECK_EQ(trophy.trophyId, 12);
    CHECK_EQ(trophy.trophyType, std::string("gold"));
    CHECK_EQ(trophy.hasProgress, true);
    CHECK_EQ(trophy.progressTarget, int64_t(42195));
}

TEST(parse_trophy_definition_leaves_progress_unset_when_absent)
{
    Doc doc(R"({"trophyId": 3, "trophyName": "Plain", "trophyType": "bronze"})");

    Trophy trophy;
    CHECK(parseTrophyDefinition(doc.get(), trophy));
    CHECK_EQ(trophy.hasProgress, false);
    CHECK_EQ(trophy.progressTarget, int64_t(0));
}

TEST(parse_trophy_progress_reads_string_rate_and_progress)
{
    Doc doc(R"({
        "trophyId": 12,
        "trophyHidden": false,
        "earned": true,
        "earnedDateTime": "2026-03-04T05:06:07Z",
        "trophyType": "gold",
        "trophyRare": 1,
        "trophyEarnedRate": "3.7",
        "progress": "21000",
        "progressRate": 49
    })");

    Trophy trophy;
    CHECK(parseTrophyProgress(doc.get(), trophy));
    CHECK_EQ(trophy.earned, true);
    CHECK_EQ(trophy.trophyRare, 1);
    CHECK(trophy.trophyEarnedRate > 3.6 && trophy.trophyEarnedRate < 3.8);
    CHECK_EQ(trophy.hasProgress, true);
    CHECK_EQ(trophy.progress, int64_t(21000));
    CHECK_EQ(trophy.progressRate, 49);
    CHECK_EQ(trophy.earnedDateTime, std::string("2026-03-04T05:06:07Z"));
}

TEST(parse_trophy_progress_tolerates_a_row_with_only_the_required_fields)
{
    Doc doc(R"({"trophyId": 4, "earned": false, "trophyHidden": false, "trophyRare": 3, "trophyEarnedRate": "88.1", "trophyType": "bronze"})");

    Trophy trophy;
    CHECK(parseTrophyProgress(doc.get(), trophy));
    CHECK_EQ(trophy.earned, false);
    CHECK_EQ(trophy.hasProgress, false);
    CHECK_EQ(trophy.earnedDateTime, std::string());
}

TEST(parse_group_endpoints_read_disjoint_halves)
{
    Doc definition(R"({
        "trophyGroupId": "001",
        "trophyGroupName": "DLC Pack",
        "trophyGroupDetail": "Extra",
        "trophyGroupIconUrl": "https://x/y.png",
        "definedTrophies": {"bronze": 5, "silver": 2, "gold": 1, "platinum": 0}
    })");

    TrophyGroup group;
    CHECK(parseGroupDefinition(definition.get(), group));
    CHECK_EQ(group.trophyGroupName, std::string("DLC Pack"));
    CHECK_EQ(group.definedTrophies.total(), 8);

    Doc progress(R"({
        "trophyGroupId": "001",
        "earnedTrophies": {"bronze": 2, "silver": 0, "gold": 0, "platinum": 0},
        "progress": 2,
        "lastUpdatedDateTime": "2026-01-01T00:00:00Z"
    })");

    TrophyGroup state;
    CHECK(parseGroupProgress(progress.get(), state));
    CHECK_EQ(state.trophyGroupId, std::string("001"));
    CHECK_EQ(state.earnedTrophies.total(), 2);
    CHECK_EQ(state.progress, 2);
    CHECK_EQ(state.trophyGroupName, std::string());
}

TEST(rarity_maps_the_documented_codes)
{
    CHECK(rarityOf(0) == TrophyRarity::UltraRare);
    CHECK(rarityOf(1) == TrophyRarity::VeryRare);
    CHECK(rarityOf(2) == TrophyRarity::Rare);
    CHECK(rarityOf(3) == TrophyRarity::Common);
    CHECK(rarityOf(-1) == TrophyRarity::Common);
}

TEST(title_round_trips_through_the_disk_cache_format)
{
    TrophyTitle original;
    original.npCommunicationId = "NPWR99999_00";
    original.npServiceName = "trophy";
    original.trophyTitleName = "Round Trip";
    original.trophyTitleDetail = "detail";
    original.trophyTitleIconUrl = "https://x/y.png";
    original.trophyTitlePlatform = "PS4";
    original.trophySetVersion = "01.00";
    original.hasTrophyGroups = true;
    original.trophyGroupCount = 2;
    original.definedTrophies = {30, 8, 3, 1};
    original.earnedTrophies = {12, 2, 1, 0};
    original.progress = 33;
    original.hiddenFlag = true;
    original.lastUpdatedDateTime = "2026-05-06T07:08:09Z";

    json_object* encoded = toJson(original);

    TrophyTitle decoded;
    CHECK(parseTitle(encoded, decoded));
    json_object_put(encoded);

    CHECK_EQ(decoded.npCommunicationId, original.npCommunicationId);
    CHECK_EQ(decoded.npServiceName, original.npServiceName);
    CHECK_EQ(decoded.trophyTitleName, original.trophyTitleName);
    CHECK_EQ(decoded.trophyTitlePlatform, original.trophyTitlePlatform);
    CHECK_EQ(decoded.trophySetVersion, original.trophySetVersion);
    CHECK_EQ(decoded.hasTrophyGroups, original.hasTrophyGroups);
    CHECK_EQ(decoded.trophyGroupCount, original.trophyGroupCount);
    CHECK_EQ(decoded.definedTrophies.total(), original.definedTrophies.total());
    CHECK_EQ(decoded.earnedTrophies.total(), original.earnedTrophies.total());
    CHECK_EQ(decoded.progress, original.progress);
    CHECK_EQ(decoded.hiddenFlag, original.hiddenFlag);
    CHECK_EQ(decoded.lastUpdatedDateTime, original.lastUpdatedDateTime);
}

TEST(summary_round_trips_through_the_disk_cache_format)
{
    TrophySummary original;
    original.accountId = "1234567890";
    original.trophyLevel = 328;
    original.tier = 4;
    original.progress = 41;
    original.trophyPoint = 25130;
    original.trophyLevelBasePoint = 24000;
    original.trophyLevelNextPoint = 27000;
    original.earnedTrophies = {95, 26, 6, 0};

    json_object* encoded = toJson(original);

    TrophySummary decoded;
    CHECK(parseSummary(encoded, decoded));
    json_object_put(encoded);

    CHECK_EQ(decoded.accountId, original.accountId);
    CHECK_EQ(decoded.trophyLevel, original.trophyLevel);
    CHECK_EQ(decoded.trophyPoint, original.trophyPoint);
    CHECK_EQ(decoded.earnedTrophies.total(), original.earnedTrophies.total());
}

TEST(json_handle_survives_move_and_bad_input)
{
    Json bad("not json at all");
    CHECK(!bad);

    Json empty("");
    CHECK(!empty);

    Json good(R"({"a": 1})");
    CHECK(static_cast<bool>(good));

    Json moved(std::move(good));
    CHECK(static_cast<bool>(moved));
    CHECK(!good);

    Json target;
    target = std::move(moved);
    CHECK(static_cast<bool>(target));
    CHECK_EQ(jsonInt(target.get(), "a"), 1);
}

TEST(merge_joins_definitions_and_progress_by_trophy_id)
{
    std::vector<Trophy> definitions(3);
    definitions[0].trophyId = 1;
    definitions[0].trophyName = "First";
    definitions[0].trophyType = "bronze";
    definitions[1].trophyId = 2;
    definitions[1].trophyName = "Second";
    definitions[1].trophyType = "gold";
    definitions[1].hasProgress = true;
    definitions[1].progressTarget = 9;
    definitions[2].trophyId = 3;
    definitions[2].trophyName = "Third";

    std::vector<Trophy> progress(3);
    progress[2].trophyId = 1;
    progress[2].earned = true;
    progress[2].earnedDateTime = "2026-01-02T00:00:00Z";
    progress[2].trophyRare = 0;
    progress[2].trophyEarnedRate = 0.4;
    progress[0].trophyId = 2;
    progress[0].hasProgress = true;
    progress[0].progress = 6;
    progress[0].progressRate = 66;
    progress[1].trophyId = 3;

    mergeTrophies(definitions, progress);

    CHECK_EQ(definitions[0].trophyName, std::string("First"));
    CHECK_EQ(definitions[0].earned, true);
    CHECK_EQ(definitions[0].trophyRare, 0);
    CHECK(definitions[0].trophyEarnedRate > 0.39 && definitions[0].trophyEarnedRate < 0.41);

    CHECK_EQ(definitions[1].progressTarget, int64_t(9));
    CHECK_EQ(definitions[1].progress, int64_t(6));
    CHECK_EQ(definitions[1].progressRate, 66);
    CHECK_EQ(definitions[1].hasProgress, true);

    CHECK_EQ(definitions[2].earned, false);
    CHECK_EQ(definitions[2].earnedDateTime, std::string());
}

TEST(merge_keeps_a_definition_with_no_matching_progress_row)
{
    std::vector<Trophy> definitions(1);
    definitions[0].trophyId = 7;
    definitions[0].trophyName = "Orphan";

    std::vector<Trophy> progress(1);
    progress[0].trophyId = 99;
    progress[0].earned = true;

    mergeTrophies(definitions, progress);

    CHECK_EQ(definitions.size(), size_t(1));
    CHECK_EQ(definitions[0].trophyName, std::string("Orphan"));
    CHECK_EQ(definitions[0].earned, false);
}

TEST(merge_does_not_clear_a_definition_progress_target)
{
    std::vector<Trophy> definitions(1);
    definitions[0].trophyId = 5;
    definitions[0].hasProgress = true;
    definitions[0].progressTarget = 42195;

    std::vector<Trophy> progress(1);
    progress[0].trophyId = 5;
    progress[0].earned = false;

    mergeTrophies(definitions, progress);

    CHECK_EQ(definitions[0].hasProgress, true);
    CHECK_EQ(definitions[0].progressTarget, int64_t(42195));
    CHECK_EQ(definitions[0].progress, int64_t(0));
}

TEST(group_merge_fills_the_half_each_endpoint_is_missing)
{
    std::vector<TrophyGroup> definitions(2);
    definitions[0].trophyGroupId = "default";
    definitions[0].trophyGroupName = "Base Game";
    definitions[0].definedTrophies = {24, 8, 7, 1};
    definitions[1].trophyGroupId = "001";
    definitions[1].trophyGroupName = "DLC";
    definitions[1].definedTrophies = {5, 2, 1, 0};

    std::vector<TrophyGroup> progress(2);
    progress[0].trophyGroupId = "001";
    progress[0].earnedTrophies = {2, 0, 0, 0};
    progress[0].progress = 2;
    progress[1].trophyGroupId = "default";
    progress[1].earnedTrophies = {20, 8, 3, 1};
    progress[1].progress = 74;
    progress[1].lastUpdatedDateTime = "2026-02-03T00:00:00Z";

    mergeGroups(definitions, progress);

    CHECK_EQ(definitions[0].trophyGroupName, std::string("Base Game"));
    CHECK_EQ(definitions[0].definedTrophies.total(), 40);
    CHECK_EQ(definitions[0].earnedTrophies.total(), 32);
    CHECK_EQ(definitions[0].progress, 74);
    CHECK_EQ(definitions[0].lastUpdatedDateTime, std::string("2026-02-03T00:00:00Z"));
    CHECK_EQ(definitions[1].trophyGroupName, std::string("DLC"));
    CHECK_EQ(definitions[1].earnedTrophies.total(), 2);
}

TEST(group_earned_counts_can_be_tallied_without_the_progress_call)
{
    std::vector<TrophyGroup> groups(2);
    groups[0].trophyGroupId = "default";
    groups[1].trophyGroupId = "001";

    std::vector<Trophy> trophies(5);
    trophies[0] = {1, "", "", "", "bronze", "default", false, true};
    trophies[1] = {2, "", "", "", "gold", "default", false, true};
    trophies[2] = {3, "", "", "", "silver", "default", false, false};
    trophies[3] = {4, "", "", "", "platinum", "001", false, true};
    trophies[4] = {5, "", "", "", "bronze", "001", false, false};

    tallyGroupEarned(groups, trophies);

    CHECK_EQ(groups[0].earnedTrophies.bronze, 1);
    CHECK_EQ(groups[0].earnedTrophies.gold, 1);
    CHECK_EQ(groups[0].earnedTrophies.silver, 0);
    CHECK_EQ(groups[0].earnedTrophies.total(), 2);
    CHECK_EQ(groups[1].earnedTrophies.platinum, 1);
    CHECK_EQ(groups[1].earnedTrophies.total(), 1);
}

TEST(merged_trophy_round_trips_through_the_detail_cache_format)
{
    Trophy original;
    original.trophyId = 12;
    original.trophyName = "Marathon";
    original.trophyDetail = "Run far";
    original.trophyIconUrl = "https://psnobj.prod.dl.playstation.net/x.png";
    original.trophyType = "gold";
    original.trophyGroupId = "default";
    original.trophyHidden = true;
    original.earned = true;
    original.earnedDateTime = "2026-03-04T05:06:07Z";
    original.trophyRare = 1;
    original.trophyEarnedRate = 3.7;
    original.hasProgress = true;
    original.progress = 21000;
    original.progressTarget = 42195;
    original.progressRate = 49;
    original.progressedDateTime = "2026-03-01T00:00:00Z";

    json_object* encoded = toJson(original);

    Trophy decoded;
    CHECK(parseCachedTrophy(encoded, decoded));
    json_object_put(encoded);

    CHECK_EQ(decoded.trophyId, original.trophyId);
    CHECK_EQ(decoded.trophyName, original.trophyName);
    CHECK_EQ(decoded.trophyType, original.trophyType);
    CHECK_EQ(decoded.trophyGroupId, original.trophyGroupId);
    CHECK_EQ(decoded.trophyHidden, original.trophyHidden);
    CHECK_EQ(decoded.earned, original.earned);
    CHECK_EQ(decoded.earnedDateTime, original.earnedDateTime);
    CHECK_EQ(decoded.trophyRare, original.trophyRare);
    CHECK(decoded.trophyEarnedRate > 3.69 && decoded.trophyEarnedRate < 3.71);
    CHECK_EQ(decoded.hasProgress, true);
    CHECK_EQ(decoded.progress, original.progress);
    CHECK_EQ(decoded.progressTarget, original.progressTarget);
    CHECK_EQ(decoded.progressRate, original.progressRate);
    CHECK_EQ(decoded.progressedDateTime, original.progressedDateTime);
}

TEST(group_round_trips_through_the_detail_cache_format)
{
    TrophyGroup original;
    original.trophyGroupId = "001";
    original.trophyGroupName = "DLC Pack";
    original.trophyGroupDetail = "Extra";
    original.trophyGroupIconUrl = "https://x/y.png";
    original.definedTrophies = {5, 2, 1, 0};
    original.earnedTrophies = {2, 0, 0, 0};
    original.progress = 25;
    original.lastUpdatedDateTime = "2026-01-01T00:00:00Z";

    json_object* encoded = toJson(original);

    TrophyGroup decoded;
    CHECK(parseCachedGroup(encoded, decoded));
    json_object_put(encoded);

    CHECK_EQ(decoded.trophyGroupId, original.trophyGroupId);
    CHECK_EQ(decoded.trophyGroupName, original.trophyGroupName);
    CHECK_EQ(decoded.definedTrophies.total(), original.definedTrophies.total());
    CHECK_EQ(decoded.earnedTrophies.total(), original.earnedTrophies.total());
    CHECK_EQ(decoded.progress, original.progress);
    CHECK_EQ(decoded.lastUpdatedDateTime, original.lastUpdatedDateTime);
}
