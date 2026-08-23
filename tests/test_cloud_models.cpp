#include "test_util.hpp"

#include "cloud/models.hpp"

using namespace cloud;

TEST(parse_catalog_reads_the_unified_payload)
{
    Catalog catalog;
    CHECK(parseCatalog(R"({
        "schemaVersion": 3,
        "total": 2,
        "nativeMode": true,
        "fallbackRegion": "US",
        "resolvedStoreLang": "en",
        "settledLocale": "en-US",
        "warning": "",
        "games": [
            {
                "productId": "PPSA-1",
                "name": "Astro Bot",
                "imageUrl": "https://example.com/portrait.png",
                "landscapeImageUrl": "https://example.com/landscape.png",
                "conceptId": "CID-1",
                "category": "owned",
                "serviceType": "pscloud",
                "platform": "ps5",
                "isOwned": true,
                "streamServiceType": "pscloud",
                "streamIdentifier": "ENT-1",
                "entitlementId": "ENT-1",
                "storeProductId": "STORE-1",
                "conceptUrl": "https://store.playstation.com/en-us/concept/1",
                "plusCatalog": true
            },
            {
                "productId": "CUSA-2",
                "name": "MotorStorm",
                "imageUrl": "https://example.com/motorstorm.png",
                "category": "streamable",
                "serviceType": "psnow",
                "platform": "ps3",
                "isOwned": false,
                "streamServiceType": "psnow",
                "streamIdentifier": "CUSA-2",
                "entitlementId": "",
                "storeProductId": "",
                "conceptUrl": "",
                "plusCatalog": false
            }
        ]
    })", catalog));

    CHECK_EQ(catalog.schemaVersion, 3);
    CHECK_EQ(catalog.total, 2);
    CHECK_EQ(catalog.nativeMode, true);
    CHECK_EQ(catalog.fallbackRegion, std::string("US"));
    CHECK_EQ(catalog.resolvedStoreLang, std::string("en"));
    CHECK_EQ(catalog.games.size(), size_t(2));
    CHECK_EQ(catalog.launchableCount(), 2);
    CHECK_EQ(catalog.games[0].name, std::string("Astro Bot"));
    CHECK_EQ(catalog.games[0].artworkUrl(), std::string("https://example.com/landscape.png"));
    CHECK_EQ(catalog.games[1].artworkUrl(), std::string("https://example.com/motorstorm.png"));
    CHECK_EQ(platformBadge(catalog.games[1]), std::string("PS3"));
    CHECK_EQ(categoryBadge(catalog.games[0]), std::string("Owned"));
}

TEST(catalog_warning_and_launch_errors_are_classified)
{
    CHECK(classifyWarning("") == WarningKind::None);
    CHECK(classifyWarning("Your session has expired. Please log in again to see your owned games.")
        == WarningKind::SessionExpired);
    CHECK(classifyWarning("some other warning") == WarningKind::Other);

    CHECK(classifyLaunchFailure("AUTHORIZATION_FAILED") == LaunchFailureKind::AuthorizationFailed);
    CHECK(classifyLaunchFailure("PS_PLUS_SUBSCRIPTION_REQUIRED") == LaunchFailureKind::PsPlusRequired);
    CHECK(classifyLaunchFailure("PING_TIMEOUT") == LaunchFailureKind::PingTimeout);
    CHECK(classifyLaunchFailure("Couldn't reach the cloud server (network error). Please try again.")
        == LaunchFailureKind::NetworkError);
    CHECK(classifyLaunchFailure("Selected datacenter 'Tokyo' not available")
        == LaunchFailureKind::DatacenterUnavailable);
    CHECK(classifyLaunchFailure("odd failure") == LaunchFailureKind::Other);
}

TEST(datacenters_round_trip_through_json)
{
    std::vector<Datacenter> dcs = parseDatacenters(R"([
        {"dataCenter":"mila","rtt":7,"rtts":[7,9],"mtu_in":1454,"mtu_out":1254,
         "port":40101,"publicIp":"senkusha.mila.prod.playstation-cloud.com",
         "maxBandwidth":25000,"measured":false},
        {"dataCenter":"lonb","rtt":1,"rtts":[1],"mtu_in":1454,"mtu_out":1454,
         "port":40101,"publicIp":"senkusha.lonb.prod.playstation-cloud.com",
         "maxBandwidth":25000,"measured":true},
        {"rtt":3}
    ])");

    CHECK_EQ(dcs.size(), size_t(2));
    CHECK_EQ(dcs[0].name, std::string("lonb"));
    CHECK_EQ(dcs[0].rttMs, 1);
    CHECK_EQ(dcs[1].name, std::string("mila"));
    CHECK_EQ(dcs[1].rttSamples.size(), size_t(2));
    CHECK_EQ(dcs[1].rttSamples[1], 9);
    CHECK_EQ(dcs[1].mtuOut, 1254);
    CHECK_EQ(dcs[1].port, 40101);
    CHECK_EQ(dcs[1].publicIp, std::string("senkusha.mila.prod.playstation-cloud.com"));
    CHECK_EQ(dcs[1].maxBandwidth, 25000);
    CHECK_EQ(dcs[1].measured, false);
    CHECK_EQ(dcs[0].measured, true);

    std::vector<Datacenter> again = parseDatacenters(serializeDatacenters(dcs));
    CHECK_EQ(again.size(), dcs.size());
    for (size_t i = 0; i < again.size(); i++)
    {
        CHECK_EQ(again[i].name, dcs[i].name);
        CHECK_EQ(again[i].rttMs, dcs[i].rttMs);
        CHECK_EQ(again[i].rttSamples.size(), dcs[i].rttSamples.size());
        for (size_t j = 0; j < again[i].rttSamples.size() && j < dcs[i].rttSamples.size(); j++)
            CHECK_EQ(again[i].rttSamples[j], dcs[i].rttSamples[j]);
        CHECK_EQ(again[i].mtuIn, dcs[i].mtuIn);
        CHECK_EQ(again[i].mtuOut, dcs[i].mtuOut);
        CHECK_EQ(again[i].port, dcs[i].port);
        CHECK_EQ(again[i].publicIp, dcs[i].publicIp);
        CHECK_EQ(again[i].maxBandwidth, dcs[i].maxBandwidth);
        CHECK_EQ(again[i].measured, dcs[i].measured);
    }

    CHECK_EQ(serializeDatacenters({}), std::string(""));
}
