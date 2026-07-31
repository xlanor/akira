#include "views/update_flow.hpp"

#include <borealis.hpp>
#include <borealis/core/i18n.hpp>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>

#include "core/settings_manager.hpp"
#include "core/version.hpp"
#include "util/http_pool.hpp"
#include "views/progress_ring.hpp"

using namespace brls::literals;

namespace akira {

void UpdateFlow::checkOnLaunch() {
    SettingsManager* settings = SettingsManager::getInstance();
    if (!settings->getAutoCheckUpdates())
        return;
    if (akira::version::isDev())
        return;

    int64_t now = static_cast<int64_t>(std::time(nullptr));
    int64_t last = settings->getLastUpdateCheck();
    if (last != 0 && now - last < 86400)
        return;

    settings->setLastUpdateCheck(now);
    settings->writeFile();

    std::string channel = settings->getUpdateChannel();

    HttpPool::instance().submit([channel](HttpSession&) {
        UpdateInfo info = UpdateManager::getInstance().checkForUpdate(channel);
        if (!info.error.empty() || !info.available)
            return;
        brls::sync([info]() { UpdateFlow::promptUpdate(info); });
    });
}

void UpdateFlow::simulate() {
    SettingsManager* settings = SettingsManager::getInstance();
    std::string initial = settings->getDevUpdateServer();

    brls::Application::getImeManager()->openForText(
        [settings](std::string text) {
            if (text.empty())
                return;

            settings->setDevUpdateServer(text);
            settings->writeFile();

            std::string base = text;
            if (base.rfind("http://", 0) != 0 && base.rfind("https://", 0) != 0)
                base = "http://" + base;

            UpdateInfo info;
            info.available = true;
            info.channel = settings->getUpdateChannel();
            info.version = "9.9.9";
            info.tag = "v9.9.9";
            info.notes = "Developer-simulated update from " + base;
            info.nroUrl = base + "/akira.nro";
            info.sha256Url = base + "/SHA256SUMS";
            info.size = 0;

            UpdateFlow::promptUpdate(info);
        },
        "akira/settings/simulate_update"_i18n,
        "akira/settings/dev_update_server_hint"_i18n,
        64, initial, 0);
}

static void showUpdateError(const std::string& message) {
    auto* dialog = new brls::Dialog(message);
    dialog->addButton("akira/common/ok"_i18n, [dialog]() { dialog->close(); });
    dialog->open();
}

void UpdateFlow::promptUpdate(const UpdateInfo& info) {
    std::string self = akira::version::semver();
    auto* dialog = new brls::Dialog(brls::getStr("akira/update/prompt_body", info.version, self));

    dialog->addButton("akira/update/update_now"_i18n, [dialog, info]() {
        dialog->close([info]() { UpdateFlow::startDownload(info); });
    });
    dialog->addButton("akira/update/later"_i18n, [dialog]() {
        dialog->close();
    });

    dialog->open();
}

void UpdateFlow::startDownload(const UpdateInfo& info) {
    auto* content = new brls::Box();
    content->setAxis(brls::Axis::COLUMN);
    content->setJustifyContent(brls::JustifyContent::CENTER);
    content->setAlignItems(brls::AlignItems::CENTER);
    content->setPadding(24.0f, 24.0f, 24.0f, 24.0f);

    auto* ring = new ProgressRing();
    ring->setWidth(84.0f);
    ring->setHeight(84.0f);
    content->addView(ring);

    auto* label = new brls::Label();
    label->setText(brls::getStr("akira/update/downloading", 0));
    label->setMarginTop(18.0f);
    content->addView(label);

    auto* dialog = new brls::Dialog(content);
    dialog->setCancelable(false);
    dialog->open();

    HttpPool::instance().submit([info, dialog, ring, label](HttpSession&) {
        UpdateManager& mgr = UpdateManager::getInstance();
        std::string err;
        std::string sha = info.sha256.empty() ? mgr.fetchExpectedSha256(info) : info.sha256;

        auto lastPct = std::make_shared<int>(-1);
        std::string tmp = mgr.download(info,
            [ring, label, lastPct](int64_t received, int64_t total) {
                int pct = total > 0 ? static_cast<int>((received * 100) / total) : 0;
                if (pct != *lastPct) {
                    *lastPct = pct;
                    float ratio = total > 0 ? static_cast<float>(received) / static_cast<float>(total) : 0.0f;
                    brls::sync([ring, label, pct, ratio]() {
                        ring->setProgress(ratio);
                        label->setText(brls::getStr("akira/update/downloading", pct));
                    });
                }
                return true;
            },
            err);

        if (tmp.empty()) {
            brls::sync([dialog, err]() {
                dialog->close();
                showUpdateError("akira/update/failed"_i18n + std::string("\n") + err);
            });
            return;
        }

        brls::sync([label]() { label->setText("akira/update/verifying"_i18n); });

        if (!mgr.verify(tmp, sha, info.size, err)) {
            std::remove(tmp.c_str());
            brls::sync([dialog, err]() {
                dialog->close();
                showUpdateError("akira/update/verify_failed"_i18n + std::string("\n") + err);
            });
            return;
        }

        brls::sync([dialog, tmp]() {
            dialog->close();
            auto* done = new brls::Dialog("akira/update/installed_relaunch"_i18n);
            done->setCancelable(false);
            done->addButton("akira/common/ok"_i18n, [tmp]() {
                std::string err;
                if (UpdateManager::getInstance().applyDownloaded(tmp, err))
                    std::_Exit(0);
                else
                    showUpdateError("akira/update/install_failed"_i18n + std::string("\n") + err);
            });
            done->open();
        });
    });
}

}
