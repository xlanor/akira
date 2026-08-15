#include "test_util.hpp"

#include "psn/auth_bootstrap.hpp"

using namespace psn::bootstrap;

TEST(extract_auth_code_reads_the_code_query_param)
{
    CHECK_EQ(extractAuthCode("https://remoteplay.dl.playstation.net/remoteplay/redirect?code=abc123&cid=123"),
        std::string("abc123"));
    CHECK_EQ(extractAuthCode("https://example.com/callback?state=1&code=xyz"),
        std::string("xyz"));
}

TEST(extract_auth_code_returns_empty_when_missing)
{
    CHECK_EQ(extractAuthCode("https://example.com/callback?state=1"), std::string());
    CHECK_EQ(extractAuthCode(""), std::string());
}

TEST(build_authorize_url_uses_remote_play_fields)
{
    std::string url = buildAuthorizeUrl(Credential::RemotePlay, "deadbeef", "cid-123");
    CHECK(url.find("client_id=ba495a24-818c-472b-b12d-ff231c1b5745") != std::string::npos);
    CHECK(url.find("duid=deadbeef") != std::string::npos);
    CHECK(url.find("cid=cid-123") != std::string::npos);
    CHECK(url.find("smcid=remoteplay") != std::string::npos);
}

TEST(build_authorize_url_uses_mobile_fields)
{
    std::string url = buildAuthorizeUrl(Credential::MobileSso, "", "");
    CHECK(url.find("client_id=09515159-7237-4370-9b40-3806e67c0891") != std::string::npos);
    CHECK(url.find("redirect_uri=com.scee.psxandroid.scecompcall%3A%2F%2Fredirect") != std::string::npos);
    CHECK(url.find("scope=psn%3Amobile.v2.core%20psn%3Aclientapp") != std::string::npos);
}

TEST(build_code_exchange_body_uses_the_expected_remote_play_fields)
{
    std::string body = buildCodeExchangeBody(Credential::RemotePlay, "auth-code");
    CHECK(body.find("grant_type=authorization_code") != std::string::npos);
    CHECK(body.find("code=auth-code") != std::string::npos);
    CHECK(body.find("client_secret=mvaiZkRsAsI1IBkY") != std::string::npos);
    CHECK(body.find("redirect_uri=https%3A%2F%2Fremoteplay.dl.playstation.net%2Fremoteplay%2Fredirect") != std::string::npos);
}

TEST(should_validate_npsso_forces_first_time_and_stale_checks)
{
    NpssoState unknown{.hasNpsso = true, .valid = false, .lastCheckedAt = 0};
    CHECK(shouldValidateNpsso(unknown, 1000, 60, false));

    NpssoState fresh{.hasNpsso = true, .valid = true, .lastCheckedAt = 995};
    CHECK(!shouldValidateNpsso(fresh, 1000, 60, false));

    NpssoState stale{.hasNpsso = true, .valid = true, .lastCheckedAt = 900};
    CHECK(shouldValidateNpsso(stale, 1000, 60, false));
    CHECK(shouldValidateNpsso(fresh, 1000, 60, true));
}
