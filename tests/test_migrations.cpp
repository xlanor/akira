#include "test_util.hpp"

#include <toml++/toml.hpp>

#include <string>
#include <string_view>

#include "core/migrations/m000_baseline.hpp"
#include "core/migrations/m001_unify_psn_auth.hpp"
#include "core/migrations/m002_multi_profile.hpp"

namespace {

toml::table run_migrate(std::string_view src)
{
    tomlmigrate::Options opts;
    opts.version_key = "version";
    opts.assume_missing_version = 0;

    tomlmigrate::Migrator m(opts);
    chiaki_migrations::register_m000_baseline(m);
    chiaki_migrations::register_m001_unify_psn_auth(m);
    chiaki_migrations::register_m002_multi_profile(m);

    toml::table doc = toml::parse(src);
    m.migrate(doc);
    return doc;
}

int64_t arr_size(const toml::table& doc, const char* key)
{
    const auto* a = doc.get_as<toml::array>(key);
    return a ? static_cast<int64_t>(a->size()) : -1;
}

const toml::table* nth(const toml::table& doc, const char* key, size_t i)
{
    const auto* a = doc.get_as<toml::array>(key);
    if (!a || i >= a->size())
        return nullptr;
    return (*a)[i].as_table();
}

std::string tstr(const toml::table* t, const char* k)
{
    if (!t)
        return "";
    auto v = (*t)[k].value<std::string>();
    return v ? *v : "";
}

int64_t tint(const toml::table* t, const char* k)
{
    if (!t)
        return -999;
    auto v = (*t)[k].value<int64_t>();
    return v ? *v : -999;
}

int64_t version_of(const toml::table& doc)
{
    auto v = doc["version"].value<int64_t>();
    return v ? *v : -1;
}

const toml::table* find_str(const toml::table& doc, const char* key,
                            const char* field, const std::string& val)
{
    const auto* a = doc.get_as<toml::array>(key);
    if (!a)
        return nullptr;
    for (const auto& e : *a) {
        const toml::table* t = e.as_table();
        if (t && tstr(t, field) == val)
            return t;
    }
    return nullptr;
}

const toml::table* find_int(const toml::table& doc, const char* key,
                            const char* field, int64_t val)
{
    const auto* a = doc.get_as<toml::array>(key);
    if (!a)
        return nullptr;
    for (const auto& e : *a) {
        const toml::table* t = e.as_table();
        if (t && tint(t, field) == val)
            return t;
    }
    return nullptr;
}

} // namespace

TEST(migration_v2_flat_to_relational)
{
    static constexpr std::string_view src = R"(
version = 2
psn_online_id = "coolgamer"
psn_account_id = "QUFBQUFBQUE="
psn_refresh_token = "rt-token"
psn_access_token = "at-token"
psn_token_expires_at = 1700000000
global_duid = "deadbeef"
local_video_bitrate = 12000

["PS5-123"]
host_type = 1
host_addr = "192.168.1.10"
target = 1000
psn_online_id = "coolgamer"
psn_account_id = "QUFBQUFBQUE="
console_pin = "1234"
rp_key = "cnBrZXk="
rp_regist_key = "cmVnaXN0"
rp_key_type = 0
haptic = -1

["PS4-abc"]
host_type = 2
host_addr = "192.168.1.20"
target = 800
psn_online_id = "coolgamer"
psn_account_id = "QUFBQUFBQUE="
rp_key = "cA=="
rp_regist_key = "cg=="
rp_key_type = 0
)";

    toml::table doc = run_migrate(src);

    CHECK_EQ(version_of(doc), 3);

    CHECK_EQ(arr_size(doc, "profiles"), 1);
    const toml::table* p0 = nth(doc, "profiles", 0);
    CHECK_EQ(tint(p0, "profile_id"), 1);
    CHECK_EQ(tstr(p0, "account_id"), std::string("QUFBQUFBQUE="));
    CHECK_EQ(tstr(p0, "online_id"), std::string("coolgamer"));
    CHECK_EQ(tstr(p0, "refresh_token"), std::string("rt-token"));
    CHECK_EQ(tstr(p0, "duid"), std::string("deadbeef"));

    CHECK_EQ(arr_size(doc, "consoles"), 2);
    const toml::table* cPs5 = find_str(doc, "consoles", "nickname", "PS5-123");
    const toml::table* cPs4 = find_str(doc, "consoles", "nickname", "PS4-abc");
    CHECK(cPs5 != nullptr);
    CHECK(cPs4 != nullptr);
    CHECK_EQ(tint(cPs5, "target"), 1000);
    CHECK_EQ(tstr(cPs5, "console_pin"), std::string("1234"));
    CHECK_EQ(tint(cPs4, "target"), 800);
    const int64_t idPs5 = tint(cPs5, "console_id");
    const int64_t idPs4 = tint(cPs4, "console_id");
    CHECK(idPs5 >= 1);
    CHECK(idPs4 >= 1);
    CHECK(idPs5 != idPs4);

    CHECK_EQ(arr_size(doc, "registrations"), 2);
    const toml::table* rPs5 = find_int(doc, "registrations", "console_id", idPs5);
    const toml::table* rPs4 = find_int(doc, "registrations", "console_id", idPs4);
    CHECK_EQ(tint(rPs5, "profile_id"), 1);
    CHECK_EQ(tstr(rPs5, "rp_key"), std::string("cnBrZXk="));
    CHECK_EQ(tint(rPs4, "profile_id"), 1);
    CHECK_EQ(tstr(rPs4, "rp_key"), std::string("cA=="));

    CHECK_EQ(tint(&doc, "active_profile_id"), 1);

    CHECK(!doc.contains("psn_account_id"));
    CHECK(!doc.contains("psn_refresh_token"));
    CHECK(!doc.contains("PS5-123"));
    CHECK(!doc.contains("PS4-abc"));
    CHECK(doc.contains("local_video_bitrate"));
}

TEST(migration_differing_override_mints_local_profile)
{
    static constexpr std::string_view src = R"(
version = 2
psn_account_id = "R0xPQkFM"
psn_online_id = "mainguy"

["PS5-shared"]
host_type = 1
psn_account_id = "T1RIRVI="
psn_online_id = "friend"
rp_key = "eA=="
rp_regist_key = "eQ=="
rp_key_type = 0
)";

    toml::table doc = run_migrate(src);

    CHECK_EQ(arr_size(doc, "profiles"), 2);
    const toml::table* p0 = nth(doc, "profiles", 0);
    const toml::table* p1 = nth(doc, "profiles", 1);
    CHECK_EQ(tstr(p0, "account_id"), std::string("R0xPQkFM"));
    CHECK_EQ(tint(p1, "profile_id"), 2);
    CHECK_EQ(tstr(p1, "account_id"), std::string("T1RIRVI="));
    CHECK_EQ(tstr(p1, "online_id"), std::string("friend"));

    const toml::table* r0 = nth(doc, "registrations", 0);
    CHECK_EQ(tint(r0, "profile_id"), 2);
    CHECK_EQ(tint(&doc, "active_profile_id"), 1);
}

TEST(migration_legacy_no_version_full_chain)
{
    static constexpr std::string_view src = R"(
psn_account_id = "TEVH"

["[Auto] PS5-old"]
host_addr = "10.0.0.5"
target = 1000
rp_key = "bA=="
rp_regist_key = "bQ=="
rp_key_type = 0
)";

    toml::table doc = run_migrate(src);

    CHECK_EQ(version_of(doc), 3);
    CHECK_EQ(arr_size(doc, "profiles"), 1);
    CHECK_EQ(tstr(nth(doc, "profiles", 0), "account_id"), std::string("TEVH"));

    CHECK_EQ(arr_size(doc, "consoles"), 1);
    CHECK_EQ(tstr(nth(doc, "consoles", 0), "nickname"), std::string("PS5-old"));

    CHECK_EQ(arr_size(doc, "registrations"), 1);
    CHECK_EQ(tint(nth(doc, "registrations", 0), "profile_id"), 1);
    CHECK_EQ(tint(&doc, "active_profile_id"), 1);
    CHECK(!doc.contains("[Auto] PS5-old"));
}

TEST(migration_already_relational_is_noop)
{
    static constexpr std::string_view src = R"(
version = 3
active_profile_id = 1

[[profiles]]
profile_id = 1
account_id = "WA=="

[[consoles]]
console_id = 1
nickname = "PS5"
)";

    toml::table doc = run_migrate(src);

    CHECK_EQ(version_of(doc), 3);
    CHECK_EQ(arr_size(doc, "profiles"), 1);
    CHECK_EQ(arr_size(doc, "consoles"), 1);
    CHECK_EQ(tstr(nth(doc, "profiles", 0), "account_id"), std::string("WA=="));
}

TEST(migration_empty_new_user)
{
    static constexpr std::string_view src = "";

    toml::table doc = run_migrate(src);

    CHECK_EQ(version_of(doc), 3);
    CHECK(!doc.contains("profiles"));
    CHECK(!doc.contains("consoles"));
    CHECK(!doc.contains("active_profile_id"));
}

TEST(migration_account_without_consoles)
{
    static constexpr std::string_view src = R"(
version = 2
psn_account_id = "TUU="
)";

    toml::table doc = run_migrate(src);

    CHECK_EQ(arr_size(doc, "profiles"), 1);
    CHECK(!doc.contains("consoles"));
    CHECK(!doc.contains("registrations"));
    CHECK_EQ(tint(&doc, "active_profile_id"), 1);
}
