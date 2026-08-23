#include "views/settings_developer_view.hpp"
#include "views/host_list_tab.hpp"
#include "views/update_flow.hpp"

#include <borealis/core/i18n.hpp>

#include "util/http.hpp"
#include "util/http_pool.hpp"

using namespace brls::literals;

namespace {

void runTlsVerifyProbe(HttpSession& session, brls::DetailCell* cell) {
    auto show = [cell](const std::string& text) {
        brls::sync([cell, text]() { cell->setDetailText(text); });
    };

    struct Case { const char* url; bool expectOk; const char* what; };
    static const Case cases[] = {
        { "https://badssl.com/",                true,  "control (valid chain)" },
        { "https://expired.badssl.com/",        false, "expired certificate" },
        { "https://wrong.host.badssl.com/",     false, "hostname mismatch" },
        { "https://self-signed.badssl.com/",    false, "self-signed" },
        { "https://untrusted-root.badssl.com/", false, "untrusted root" },
    };

    brls::Logger::info("tlsprobe: ===== TLS verification probe (certinfo path) =====");
    show("running...");
    int failures = 0;
    int done = 0;
    for (const auto& c : cases) {
        HttpRequest req;
        req.url = c.url;
        req.certInfo = true;
        req.timeoutSec = 15;
        req.followLocation = false;

        HttpResponse res = session.perform(req);
        bool connected = !res.transportFailed();
        bool asExpected = (connected == c.expectOk);
        if (!asExpected) failures++;

        brls::Logger::info("tlsprobe: {} {} -> {} ({})",
            asExpected ? "PASS" : "FAIL", c.what,
            connected ? "connected" : "rejected",
            res.error.empty() ? "no transport error" : res.error);

        done++;
        show(fmt::format("{}/{} checked, {} unexpected - {} {}",
            done, (int)(sizeof(cases) / sizeof(cases[0])), failures,
            asExpected ? "ok:" : "UNEXPECTED:", c.what));
    }

    if (failures == 0)
    {
        brls::Logger::info("tlsprobe: ===== all cases as expected: the service verifies this path =====");
        show("PASS - service verifies CA, hostname and expiry");
    }
    else
    {
        brls::Logger::error("tlsprobe: ===== {} case(s) UNEXPECTED: do not rely on the service here =====", failures);
        show(fmt::format("FAIL - {} of 5 unexpected, see log", failures));
    }
}

} // namespace

SettingsDeveloperView::SettingsDeveloperView() {
    this->inflateFromXMLRes("xml/settings/developer.xml");

    settings = SettingsManager::getInstance();

    fakeHostsToggle->init(
        "akira/settings/dev_fake_hosts"_i18n,
        settings->getDevFakeHosts(),
        [this](bool isOn) {
            settings->setDevFakeHosts(isOn);
            settings->writeFile();
            HostListTab::notifyActiveProfileChanged();
        });

    static const std::vector<std::string> wsNodes = {
        "",
        "44-231-131-164-pushcl.np.communication.playstation.net",
        "44-234-162-2-pushcl.np.communication.playstation.net",
        "34-215-159-77-pushcl.np.communication.playstation.net",
        "44-233-186-110-pushcl.np.communication.playstation.net",
    };

    std::vector<std::string> wsNodeLabels = { "akira/settings/dev_force_ws_node_off"_i18n };
    for (size_t i = 1; i < wsNodes.size(); i++)
        wsNodeLabels.push_back(wsNodes[i].substr(0, wsNodes[i].find("-pushcl")));

    const std::string currentWsNode = settings->getDevForceWsFqdn();
    int currentWsIndex = 0;
    for (size_t i = 1; i < wsNodes.size(); i++)
    {
        if (wsNodes[i] == currentWsNode)
        {
            currentWsIndex = static_cast<int>(i);
            break;
        }
    }

    forceWsNodeSelector->init(
        "akira/settings/dev_force_ws_node"_i18n,
        wsNodeLabels,
        currentWsIndex,
        [this](int selected) {
            settings->setDevForceWsFqdn(wsNodes[selected]);
            settings->writeFile();
        });

    simulateUpdateCell->setText("akira/settings/simulate_update"_i18n);
    tlsVerifyProbeCell->setText("akira/settings/dev_tls_verify_probe"_i18n);
    tlsVerifyProbeCell->registerClickAction([this](brls::View*) {
        brls::DetailCell* cell = this->tlsVerifyProbeCell;
        HttpPool::instance().submit([cell](HttpSession& session) {
            runTlsVerifyProbe(session, cell);
        });
        return true;
    });

    simulateUpdateCell->registerClickAction([](brls::View*) {
        akira::UpdateFlow::simulate();
        return true;
    });
}
