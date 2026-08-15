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
