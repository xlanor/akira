#include "core/settings_manager.hpp"
#include "core/swipe_direction.hpp"
#include "core/host.hpp"

#include <borealis.hpp>
#include <format>
#include <ranges>
#include <utility>
#include <chiaki/base64.h>
#include <chiaki/controller.h>
#include <toml++/toml.hpp>
#include <switch.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <regex>
#include <sys/stat.h>

namespace {

int fsrTargetHeightForResolution(ChiakiVideoResolutionPreset resolution) {
    switch (resolution) {
        case CHIAKI_VIDEO_RESOLUTION_PRESET_540p:
            return 720;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_720p:
            return 1080;
        default:
            return 0;
    }
}

std::string hidButtonToConfigString(uint64_t button) {
    switch (button) {
        case HidNpadButton_A: return "A";
        case HidNpadButton_B: return "B";
        case HidNpadButton_X: return "X";
        case HidNpadButton_Y: return "Y";
        case HidNpadButton_L: return "L";
        case HidNpadButton_R: return "R";
        case HidNpadButton_ZL: return "ZL";
        case HidNpadButton_ZR: return "ZR";
        case HidNpadButton_Plus: return "Plus";
        case HidNpadButton_Minus: return "Minus";
        case HidNpadButton_StickL: return "L Stick";
        case HidNpadButton_StickR: return "R Stick";
        case HidNpadButton_LeftSL: return "SL(L)";
        case HidNpadButton_LeftSR: return "SR(L)";
        case HidNpadButton_RightSL: return "SL(R)";
        case HidNpadButton_RightSR: return "SR(R)";
        case HidNpadButton_Up: return "D-Up";
        case HidNpadButton_Down: return "D-Down";
        case HidNpadButton_Left: return "D-Left";
        case HidNpadButton_Right: return "D-Right";
        case HidNpadButton_StickLUp: return "LS-Up";
        case HidNpadButton_StickLDown: return "LS-Down";
        case HidNpadButton_StickLLeft: return "LS-Left";
        case HidNpadButton_StickLRight: return "LS-Right";
        case HidNpadButton_StickRUp: return "RS-Up";
        case HidNpadButton_StickRDown: return "RS-Down";
        case HidNpadButton_StickRLeft: return "RS-Left";
        case HidNpadButton_StickRRight: return "RS-Right";
        default: return "";
    }
}

uint64_t configStringToHidButton(const std::string& name) {
    if (name == "A") return HidNpadButton_A;
    if (name == "B") return HidNpadButton_B;
    if (name == "X") return HidNpadButton_X;
    if (name == "Y") return HidNpadButton_Y;
    if (name == "L") return HidNpadButton_L;
    if (name == "R") return HidNpadButton_R;
    if (name == "ZL") return HidNpadButton_ZL;
    if (name == "ZR") return HidNpadButton_ZR;
    if (name == "Plus") return HidNpadButton_Plus;
    if (name == "Minus") return HidNpadButton_Minus;
    if (name == "L Stick") return HidNpadButton_StickL;
    if (name == "R Stick") return HidNpadButton_StickR;
    if (name == "SL(L)") return HidNpadButton_LeftSL;
    if (name == "SR(L)") return HidNpadButton_LeftSR;
    if (name == "SL(R)") return HidNpadButton_RightSL;
    if (name == "SR(R)") return HidNpadButton_RightSR;
    if (name == "D-Up") return HidNpadButton_Up;
    if (name == "D-Down") return HidNpadButton_Down;
    if (name == "D-Left") return HidNpadButton_Left;
    if (name == "D-Right") return HidNpadButton_Right;
    if (name == "LS-Up") return HidNpadButton_StickLUp;
    if (name == "LS-Down") return HidNpadButton_StickLDown;
    if (name == "LS-Left") return HidNpadButton_StickLLeft;
    if (name == "LS-Right") return HidNpadButton_StickLRight;
    if (name == "RS-Up") return HidNpadButton_StickRUp;
    if (name == "RS-Down") return HidNpadButton_StickRDown;
    if (name == "RS-Left") return HidNpadButton_StickRLeft;
    if (name == "RS-Right") return HidNpadButton_StickRRight;
    return 0;
}

std::string chiakiButtonToConfigKey(uint32_t button) {
    switch (button) {
        case CHIAKI_CONTROLLER_BUTTON_CROSS: return "cross";
        case CHIAKI_CONTROLLER_BUTTON_MOON: return "circle";
        case CHIAKI_CONTROLLER_BUTTON_BOX: return "square";
        case CHIAKI_CONTROLLER_BUTTON_PYRAMID: return "triangle";
        case CHIAKI_CONTROLLER_BUTTON_L1: return "l1";
        case CHIAKI_CONTROLLER_BUTTON_R1: return "r1";
        case CHIAKI_CONTROLLER_ANALOG_BUTTON_L2: return "l2";
        case CHIAKI_CONTROLLER_ANALOG_BUTTON_R2: return "r2";
        case CHIAKI_CONTROLLER_BUTTON_L3: return "l3";
        case CHIAKI_CONTROLLER_BUTTON_R3: return "r3";
        case CHIAKI_CONTROLLER_BUTTON_OPTIONS: return "options";
        case CHIAKI_CONTROLLER_BUTTON_SHARE: return "share";
        case CHIAKI_CONTROLLER_BUTTON_TOUCHPAD: return "touchpad";
        case CHIAKI_CONTROLLER_BUTTON_PS: return "ps";
        case SWIPE_TOUCHPAD_UP: return "swipe_up";
        case SWIPE_TOUCHPAD_DOWN: return "swipe_down";
        case SWIPE_TOUCHPAD_LEFT: return "swipe_left";
        case SWIPE_TOUCHPAD_RIGHT: return "swipe_right";
        default: return "";
    }
}

uint32_t configKeyToChiakiButton(const std::string& key) {
    if (key == "cross") return CHIAKI_CONTROLLER_BUTTON_CROSS;
    if (key == "circle") return CHIAKI_CONTROLLER_BUTTON_MOON;
    if (key == "square") return CHIAKI_CONTROLLER_BUTTON_BOX;
    if (key == "triangle") return CHIAKI_CONTROLLER_BUTTON_PYRAMID;
    if (key == "l1") return CHIAKI_CONTROLLER_BUTTON_L1;
    if (key == "r1") return CHIAKI_CONTROLLER_BUTTON_R1;
    if (key == "l2") return CHIAKI_CONTROLLER_ANALOG_BUTTON_L2;
    if (key == "r2") return CHIAKI_CONTROLLER_ANALOG_BUTTON_R2;
    if (key == "l3") return CHIAKI_CONTROLLER_BUTTON_L3;
    if (key == "r3") return CHIAKI_CONTROLLER_BUTTON_R3;
    if (key == "options") return CHIAKI_CONTROLLER_BUTTON_OPTIONS;
    if (key == "share") return CHIAKI_CONTROLLER_BUTTON_SHARE;
    if (key == "touchpad") return CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
    if (key == "ps") return CHIAKI_CONTROLLER_BUTTON_PS;
    if (key == "swipe_up") return SWIPE_TOUCHPAD_UP;
    if (key == "swipe_down") return SWIPE_TOUCHPAD_DOWN;
    if (key == "swipe_left") return SWIPE_TOUCHPAD_LEFT;
    if (key == "swipe_right") return SWIPE_TOUCHPAD_RIGHT;
    return 0;
}

bool decodeRpKey(const std::string& b64, uint8_t out[0x10]) {
    size_t sz = 0x10;
    return chiaki_base64_decode(b64.c_str(), b64.length(), out, &sz) == CHIAKI_ERR_SUCCESS;
}

bool decodeRpRegistKey(const std::string& b64, char out[CHIAKI_SESSION_AUTH_SIZE]) {
    size_t sz = CHIAKI_SESSION_AUTH_SIZE;
    return chiaki_base64_decode(b64.c_str(), b64.length(), reinterpret_cast<uint8_t*>(out), &sz) == CHIAKI_ERR_SUCCESS;
}

std::string encodeRpKey(const uint8_t in[0x10]) {
    char b64[64] = {0};
    if (chiaki_base64_encode(in, 0x10, b64, sizeof(b64)) == CHIAKI_ERR_SUCCESS)
        return std::string(b64);
    return "";
}

std::string encodeRpRegistKey(const char in[CHIAKI_SESSION_AUTH_SIZE]) {
    char b64[64] = {0};
    if (chiaki_base64_encode(reinterpret_cast<const uint8_t*>(in), CHIAKI_SESSION_AUTH_SIZE, b64, sizeof(b64)) == CHIAKI_ERR_SUCCESS)
        return std::string(b64);
    return "";
}

} // anonymous namespace



SettingsManager::SettingsManager() {
    buttonMapping = getDefaultButtonMapping();
}

void SettingsManager::setLogger(ChiakiLog* logger) {
    this->log = logger;
    for (auto& [name, host] : hosts) {
        host->setLogger(logger);
    }
}

SettingsManager* SettingsManager::getInstance() {
    static SettingsManager* instance = nullptr;
    if (!instance) {
        instance = new SettingsManager();
        instance->ensureConfigDir();
        instance->parseFile();
    }
    return instance;
}

void SettingsManager::ensureConfigDir() {
    mkdir(CONFIG_DIR, 0755);
}

bool SettingsManager::fileExists(const char* path) {
    struct stat buffer;
    return (stat(path, &buffer) == 0);
}

size_t SettingsManager::getB64EncodeSize(size_t inputSize) {
    return ((4 * inputSize / 3) + 3) & ~3;
}

std::map<std::string, std::unique_ptr<Host>>* SettingsManager::getHostsMap() {
    return &hosts;
}

Host* SettingsManager::getOrCreateHost(const std::string& hostName) {
    bool created = false;

    if (hosts.find(hostName) == hosts.end()) {
        hosts[hostName] = std::make_unique<Host>(hostName);
        if (log) {
            hosts[hostName]->setLogger(log);
        }
        created = true;
    }

    Host* host = hosts.at(hostName).get();
    host->consoleKey = hostName;

    if (created) {
        if (const Account* a = getDefaultAccount()) {
            setPsnOnlineId(host, a->onlineId);
            setPsnAccountId(host, a->accountId);
        }
    }

    return host;
}

Host* SettingsManager::getOrCreateDiscoveredHost(const std::string& hostId, const std::string& name) {
    std::string mac = Host::normalizeMac(hostId);

    if (!mac.empty()) {
        for (auto& [key, h] : hosts) {
            if (h && !h->isRemote() && h->mac == mac) {
                h->hostId = hostId;
                return h.get();
            }
        }
    }

    for (auto& [key, h] : hosts) {
        if (h && !h->isRemote() && h->mac.empty() && h->hostName == name) {
            h->setMac(hostId);
            h->hostId = hostId;
            return h.get();
        }
    }

    std::string key = mac.empty() ? name : mac;
    Host* host = getOrCreateHost(key);
    host->setMac(hostId);
    host->hostId = hostId;
    host->hostName = name;
    host->setHostType(HostType::Auto);
    return host;
}

Host* SettingsManager::createManualHost(const std::string& nickname) {
    std::string key = "manual:" + std::to_string(manualHostNextId++);
    Host* host = getOrCreateHost(key);
    host->hostName = nickname;
    host->setHostType(HostType::Manual);
    return host;
}

void SettingsManager::setHostNickname(Host* host, const std::string& nickname) {
    if (host) host->hostName = nickname;
}

void SettingsManager::removeHost(const std::string& hostName) {
    hosts.erase(hostName);
}

void SettingsManager::removeHost(Host* host) {
    if (!host) return;
    hosts.erase(host->consoleKey);
}

void SettingsManager::renameHost(const std::string& oldName, const std::string& newName) {
    auto it = hosts.find(oldName);
    if (it == hosts.end()) return;

    auto host = std::move(it->second);
    hosts.erase(it);
    host->hostName = newName;
    hosts[newName] = std::move(host);
}

Host* SettingsManager::findHostByDuid(const std::string& duid) {
    if (duid.empty()) {
        return nullptr;
    }
    for (auto& [name, host] : hosts) {
        if (host && host->getRemoteDuid() == duid) {
            return host.get();
        }
    }
    return nullptr;
}

void SettingsManager::parseFile() {
    if (fileExists(TOML_CONFIG_FILE)) {
        parseTomlFile();
        migrateConfigToMultiAccount();
        if (legacyConfigDetected) {
            std::string backupPath = std::string(TOML_CONFIG_FILE) + ".bak";
            std::ifstream src(TOML_CONFIG_FILE, std::ios::binary);
            std::ofstream dst(backupPath, std::ios::binary | std::ios::trunc);
            if (src && dst) {
                dst << src.rdbuf();
            }
            src.close();
            dst.close();
            brls::Logger::info("Migrating config to multi-account format; backup at {}", backupPath);
            writeFile();
        }
        removeLegacyConfig();
    } else if (fileExists(LEGACY_CONFIG_FILE)) {
        brls::Logger::info("Migrating from legacy config format");
        parseLegacyFile();
        migrateConfigToMultiAccount();
        writeFile();
    } else {
        brls::Logger::info("No config file found, using defaults");
    }
}

void SettingsManager::removeLegacyConfig() {
    static const std::vector<std::string> legacyKeys = {
        "invert_ab",
        "enable_experimental_crypto",
        "video_resolution",
        "video_fps",
        "video_bitrate",
        "power_user_mode",
    };

    std::string content;
    {
        std::ifstream f(TOML_CONFIG_FILE);
        if (!f) return;
        content.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }

    bool needsRewrite = false;
    for (const auto& key : legacyKeys) {
        if (content.find(key) != std::string::npos) {
            brls::Logger::info("Removing legacy config key: {}", key);
            needsRewrite = true;
        }
    }

    if (needsRewrite) {
        writeFile();
    }
}

void SettingsManager::parseTomlFile() {
    brls::Logger::info("Parsing TOML config file: {}", TOML_CONFIG_FILE);

    try {
        auto config = toml::parse_file(TOML_CONFIG_FILE);

        if (auto val = config["local_video_resolution"].value<std::string>())
            localVideoResolution = stringToResolution(*val);
        else if (auto val = config["video_resolution"].value<std::string>())
            localVideoResolution = stringToResolution(*val);

        if (auto val = config["remote_video_resolution"].value<std::string>())
            remoteVideoResolution = stringToResolution(*val);
        else if (auto val = config["video_resolution"].value<std::string>())
            remoteVideoResolution = stringToResolution(*val);

        if (auto val = config["local_video_fps"].value<int64_t>())
            localVideoFPS = stringToFps(std::to_string(*val));
        else if (auto val = config["video_fps"].value<int64_t>())
            localVideoFPS = stringToFps(std::to_string(*val));

        if (auto val = config["remote_video_fps"].value<int64_t>())
            remoteVideoFPS = stringToFps(std::to_string(*val));
        else if (auto val = config["video_fps"].value<int64_t>())
            remoteVideoFPS = stringToFps(std::to_string(*val));
        if (auto val = config["haptic"].value<int64_t>())
            globalHaptic = static_cast<HapticPreset>(*val);

        if (auto rumbleTable = config["rumble"].as_table()) {
            if (auto val = (*rumbleTable)["freq_low"].value<double>())
                rumbleFreqLow = std::max(40.0f, std::min(320.0f, static_cast<float>(*val)));
            if (auto val = (*rumbleTable)["freq_high"].value<double>())
                rumbleFreqHigh = std::max(40.0f, std::min(320.0f, static_cast<float>(*val)));
            if (auto val = (*rumbleTable)["envelope_decay"].value<double>())
                rumbleEnvelopeDecay = std::max(0.50f, std::min(0.95f, static_cast<float>(*val)));
            if (auto val = (*rumbleTable)["envelope_attack"].value<double>())
                rumbleEnvelopeAttack = std::max(0.20f, std::min(1.00f, static_cast<float>(*val)));
        }
        if (auto accountsArr = config["accounts"].as_array()) {
            for (auto& elem : *accountsArr) {
                auto* at = elem.as_table();
                if (!at) continue;
                Account acc;
                if (auto v = (*at)["online_id"].value<std::string>()) acc.onlineId = *v;
                if (auto v = (*at)["account_id"].value<std::string>()) acc.accountId = *v;
                if (auto v = (*at)["refresh_token"].value<std::string>()) acc.refreshToken = *v;
                if (auto v = (*at)["access_token"].value<std::string>()) acc.accessToken = *v;
                if (auto v = (*at)["token_expires_at"].value<int64_t>()) acc.tokenExpiresAt = *v;
                if (auto v = (*at)["duid"].value<std::string>()) acc.duid = *v;
                accounts.push_back(acc);
            }
        }
        if (auto val = config["default_account"].value<std::string>())
            defaultAccountId = *val;

        {
            Account legacy;
            bool hasLegacy = false;
            if (auto v = config["psn_online_id"].value<std::string>()) { legacy.onlineId = *v; hasLegacy = true; }
            if (auto v = config["psn_account_id"].value<std::string>()) { legacy.accountId = *v; hasLegacy = true; }
            if (auto v = config["psn_refresh_token"].value<std::string>()) { legacy.refreshToken = *v; hasLegacy = true; }
            if (auto v = config["psn_access_token"].value<std::string>()) { legacy.accessToken = *v; hasLegacy = true; }
            if (auto v = config["psn_token_expires_at"].value<int64_t>()) { legacy.tokenExpiresAt = *v; hasLegacy = true; }
            if (auto v = config["global_duid"].value<std::string>()) { legacy.duid = *v; hasLegacy = true; }
            if (hasLegacy) {
                legacyConfigDetected = true;
                upsertAccount(legacy);
                if (defaultAccountId.empty()) defaultAccountId = legacy.accountId;
            }
        }
        if (auto val = config["holepunch_retry"].value<bool>())
            holepunchRetry = *val;
        if (auto val = config["port_guessing"].value<bool>())
            portGuessing = *val;
        if (auto val = config["port_guessing_count"].value<int64_t>())
            portGuessingCount = static_cast<int>(*val);
        if (auto val = config["port_guessing_socks"].value<int64_t>())
            portGuessingSocks = static_cast<int>(*val);
        if (auto val = config["power_user_menu_unlocked"].value<bool>())
            powerUserMenuUnlocked = *val;
        else if (auto val = config["power_user_mode"].value<bool>())
            powerUserMenuUnlocked = *val;
        if (auto val = config["ipc_stats_enabled"].value<bool>())
            ipcStatsEnabled = *val;
        if (auto val = config["unlock_bitrate_max"].value<bool>())
            unlockBitrateMax = *val;
        if (auto val = config["auto_reconnect"].value<bool>())
            autoReconnect = *val;
        if (auto val = config["sleep_on_exit"].value<bool>())
            sleepOnExit = *val;
        if (auto val = config["request_idr_on_fec_failure"].value<bool>())
            requestIdrOnFecFailure = *val;
        if (auto val = config["packet_loss_max"].value<double>())
            packetLossMax = static_cast<float>(*val);
        if (auto val = config["enable_file_logging"].value<bool>())
            enableFileLogging = *val;
        if (auto val = config["enable_thread_affinity"].value<bool>())
            enableThreadAffinity = *val;
        if (auto val = config["low_latency_mode"].value<bool>())
            lowLatencyMode = *val;
        if (auto val = config["debug_locale"].value<std::string>())
            debugLocale = *val;
        if (auto val = config["local_fsr_enabled"].value<bool>())
            localFsrEnabled = *val;
        if (auto val = config["remote_fsr_enabled"].value<bool>())
            remoteFsrEnabled = *val;
        if (auto val = config["vpn_fsr_enabled"].value<bool>())
            vpnFsrEnabled = *val;
        if (!config["local_fsr_enabled"].value<bool>() &&
            !config["remote_fsr_enabled"].value<bool>() &&
            !config["vpn_fsr_enabled"].value<bool>()) {
            bool legacyEasu = false;
            if (auto val = config["easu_enabled"].value<bool>())
                legacyEasu = *val;
            else if (auto val = config["fsr_enabled"].value<bool>())
                legacyEasu = *val;
            if (legacyEasu) {
                localFsrEnabled = true;
                remoteFsrEnabled = true;
                vpnFsrEnabled = true;
            }
        }
        if (auto val = config["rcas_enabled"].value<bool>())
            rcasEnabled = *val;
        else if (auto val = config["fsr_enabled"].value<bool>())
            rcasEnabled = *val;
        if (auto val = config["rcas_sharpness"].value<double>())
            rcasSharpness = static_cast<float>(*val);
        else if (auto val = config["fsr_sharpness"].value<double>())
            rcasSharpness = static_cast<float>(*val);
        if (auto val = config["debug_lwip_log"].value<bool>())
            debugLwipLog = *val;
        if (auto val = config["debug_wireguard_log"].value<bool>())
            debugWireguardLog = *val;
        if (auto val = config["debug_render_log"].value<bool>())
            debugRenderLog = *val;
        if (auto val = config["debug_chiaki_log"].value<bool>())
            debugChiakiLog = *val;
        if (auto val = config["debug_discovery_log"].value<bool>())
            debugDiscoveryLog = *val;
        if (auto val = config["debug_ffmpeg_log"].value<bool>())
            debugFfmpegLog = *val;
        if (auto pictureTable = config["picture_adjustments"].as_table()) {
            if (auto val = (*pictureTable)["enable_dithering"].value<bool>())
                enableDithering = *val;
            if (auto val = (*pictureTable)["dithering_strength"].value<double>())
                ditheringStrength = std::max(1.0f, std::min(10.0f, static_cast<float>(*val)));
        }
        if (auto val = config["gyro_source"].value<int64_t>())
            globalGyroSource = static_cast<GyroSource>(*val);
        if (auto val = config["companion_host"].value<std::string>())
            companionHost = *val;
        if (auto val = config["companion_port"].value<int64_t>())
            companionPort = static_cast<int>(*val);
        if (auto val = config["manual_host_next_id"].value<int64_t>())
            manualHostNextId = static_cast<int>(*val);
        if (auto val = config["local_video_bitrate"].value<int64_t>())
            localVideoBitrate = static_cast<int>(*val);
        else if (auto val = config["video_bitrate"].value<int64_t>())
            localVideoBitrate = static_cast<int>(*val);
        else
            localVideoBitrate = getDefaultBitrateForResolution(localVideoResolution);

        if (auto val = config["remote_video_bitrate"].value<int64_t>())
            remoteVideoBitrate = static_cast<int>(*val);
        else if (auto val = config["video_bitrate"].value<int64_t>())
            remoteVideoBitrate = static_cast<int>(*val);
        else
            remoteVideoBitrate = getDefaultBitrateForResolution(remoteVideoResolution);

        if (auto val = config["vpn_video_bitrate"].value<int64_t>())
            vpnVideoBitrate = static_cast<int>(*val);

        if (auto val = config["vpn_video_resolution"].value<std::string>())
            vpnVideoResolution = stringToResolution(*val);

        if (auto val = config["vpn_video_fps"].value<int64_t>())
            vpnVideoFPS = (*val == 30) ? CHIAKI_VIDEO_FPS_PRESET_30 : CHIAKI_VIDEO_FPS_PRESET_60;

        if (auto mappingTable = config["button_mapping"].as_table()) {
            for (auto& [key, value] : *mappingTable) {
                std::string keyStr(key.str());
                uint32_t chiakiBtn = configKeyToChiakiButton(keyStr);
                if (chiakiBtn == 0) continue;

                if (auto* arr = value.as_array()) {
                    std::vector<uint64_t> combo;
                    for (auto& elem : *arr) {
                        if (auto btnName = elem.value<std::string>()) {
                            uint64_t hidBtn = configStringToHidButton(*btnName);
                            if (hidBtn != 0) {
                                combo.push_back(hidBtn);
                            }
                        }
                    }
                    buttonMapping[chiakiBtn] = combo;
                }
            }
            if (auto val = (*mappingTable)["touchpad_enabled"].value<bool>())
                touchpadEnabled = *val;
            if (auto val = (*mappingTable)["swipe_up_enabled"].value<bool>())
                swipeUpEnabled = *val;
            if (auto val = (*mappingTable)["swipe_down_enabled"].value<bool>())
                swipeDownEnabled = *val;
            if (auto val = (*mappingTable)["swipe_left_enabled"].value<bool>())
                swipeLeftEnabled = *val;
            if (auto val = (*mappingTable)["swipe_right_enabled"].value<bool>())
                swipeRightEnabled = *val;

            brls::Logger::info("Loaded button mapping from config");
        }

        for (auto& [key, value] : config) {
            if (!value.is_table()) continue;

            std::string hostName(key.str());

            if (hostName == "button_mapping" || hostName == "rumble" || hostName == "picture_adjustments") continue;

            auto* table = value.as_table();

            std::string cleanName = hostName;
            HostType migratedType = HostType::Discovered;

            if (auto val = (*table)["host_type"].value<int64_t>()) {
                migratedType = static_cast<HostType>(*val);
            } else {
                if (hostName.length() > 9 && hostName.substr(hostName.length() - 9) == " (Remote)") {
                    cleanName = hostName;
                    migratedType = HostType::Remote;
                } else if (hostName.rfind("[Manual] ", 0) == 0) {
                    cleanName = hostName.substr(9);
                    migratedType = HostType::Manual;
                } else if (hostName.rfind("[Auto] ", 0) == 0) {
                    cleanName = hostName.substr(7);
                    migratedType = HostType::Auto;
                }
            }

            if (cleanName != hostName) {
                if (hosts.find(cleanName) != hosts.end()) {
                    Host* existing = hosts[cleanName].get();
                    if (existing->hostType == HostType::Manual && migratedType != HostType::Manual) {
                        brls::Logger::info("Skipping {} - Manual host {} already exists", hostName, cleanName);
                        continue;
                    }
                }
                brls::Logger::info("Migrating host '{}' to '{}'", hostName, cleanName);
            }

            Host* host = getOrCreateHost(cleanName);
            host->inConfig = true;
            host->hostType = migratedType;
            host->hostName = cleanName;

            if (auto val = (*table)["nickname"].value<std::string>())
                host->hostName = *val;
            if (cleanName.rfind("manual:", 0) == 0) {
                int n = std::atoi(cleanName.c_str() + 7);
                if (n >= manualHostNextId) manualHostNextId = n + 1;
            }

            if (auto val = (*table)["host_addr"].value<std::string>())
                host->hostAddr = *val;
            if (auto val = (*table)["mac"].value<std::string>())
                host->setMac(*val);
            if (auto val = (*table)["target"].value<int64_t>())
                host->setChiakiTarget(static_cast<ChiakiTarget>(*val));
            if (auto val = (*table)["psn_online_id"].value<std::string>())
                host->psnOnlineId = *val;
            if (auto val = (*table)["psn_account_id"].value<std::string>())
                host->psnAccountId = *val;
            if (auto val = (*table)["console_pin"].value<std::string>())
                host->consolePIN = *val;
            if (auto val = (*table)["haptic"].value<int64_t>())
                host->haptic = static_cast<int>(*val);
            if (auto val = (*table)["remote_duid"].value<std::string>())
                host->remoteDuid = *val;
            if (auto val = (*table)["remote_account"].value<std::string>())
                host->remoteAccountId = *val;

            bool rpKeySet = false, rpRegistKeySet = false, rpKeyTypeSet = false;
            if (auto val = (*table)["rp_key"].value<std::string>())
                rpKeySet = setHostRpKey(host, *val);
            if (auto val = (*table)["rp_regist_key"].value<std::string>())
                rpRegistKeySet = setHostRpRegistKey(host, *val);
            if (auto val = (*table)["rp_key_type"].value<int64_t>()) {
                host->rpKeyType = static_cast<int>(*val);
                rpKeyTypeSet = true;
            }

            if (rpKeySet && rpRegistKeySet && rpKeyTypeSet) {
                host->rpKeyData = true;
            }

            if (rpKeySet || rpRegistKeySet ||
                (*table)["psn_account_id"].value<std::string>() ||
                (*table)["psn_online_id"].value<std::string>() ||
                (*table)["console_pin"].value<std::string>()) {
                legacyConfigDetected = true;
            }

            if (auto regsArr = (*table)["registrations"].as_array()) {
                for (auto& elem : *regsArr) {
                    auto* rt = elem.as_table();
                    if (!rt) continue;
                    HostProfile profile;
                    if (auto v = (*rt)["account_id"].value<std::string>()) profile.psnAccountId = *v;
                    if (auto v = (*rt)["online_id"].value<std::string>()) profile.psnOnlineId = *v;
                    if (auto v = (*rt)["console_pin"].value<std::string>()) profile.consolePIN = *v;
                    bool k = false, rk = false, kt = false;
                    if (auto v = (*rt)["rp_key"].value<std::string>()) k = decodeRpKey(*v, profile.rpKey);
                    if (auto v = (*rt)["rp_regist_key"].value<std::string>()) rk = decodeRpRegistKey(*v, profile.rpRegistKey);
                    if (auto v = (*rt)["rp_key_type"].value<int64_t>()) { profile.rpKeyType = static_cast<uint32_t>(*v); kt = true; }
                    profile.hasRpKey = k && rk && kt;
                    host->profiles.push_back(profile);
                }
            }

            std::string activeAccount;
            if (auto val = (*table)["active_account"].value<std::string>())
                activeAccount = *val;

            if (!host->profiles.empty() || !activeAccount.empty()) {
                int activeIdx = activeAccount.empty() ? -1 : host->findProfileByAccount(activeAccount);
                if (activeIdx < 0 && !activeAccount.empty()) {
                    HostProfile ap;
                    ap.psnAccountId = activeAccount;
                    host->profiles.push_back(ap);
                    activeIdx = static_cast<int>(host->profiles.size()) - 1;
                }
                if (activeIdx < 0) activeIdx = 0;
                host->activeProfile = activeIdx;
                host->applyActiveProfile();
            }
        }

        brls::Logger::info("Loaded {} host(s) from TOML config", hosts.size());
    } catch (const toml::parse_error& err) {
        brls::Logger::error("Failed to parse TOML config: {}", err.what());
    }
}

void SettingsManager::parseLegacyFile() {
    brls::Logger::info("Parsing legacy config file: {}", LEGACY_CONFIG_FILE);

    std::ifstream configFile(LEGACY_CONFIG_FILE);
    if (!configFile.is_open()) {
        brls::Logger::error("Failed to open legacy config file");
        return;
    }

    enum class ConfigItem {
        Unknown, HostName, HostAddr, PsnOnlineId, PsnAccountId, PsnRefreshToken,
        PsnAccessToken, ConsolePIN, RpKey, RpKeyType, RpRegistKey, VideoResolution,
        VideoFps, Target, Haptic, RemoteDuid, CompanionHost, CompanionPort,
        PsnTokenExpiresAt, GlobalDuid
    };

    const std::map<ConfigItem, std::regex> regexMap = {
        {ConfigItem::HostName, std::regex("^\\[\\s*(.+)\\s*\\]")},
        {ConfigItem::HostAddr, std::regex("^\\s*host_(?:ip|addr)\\s*=\\s*\"?((\\d+\\.\\d+\\.\\d+\\.\\d+)|([A-Za-z0-9-]+(\\.[A-Za-z0-9-]+)+))\"?")},
        {ConfigItem::PsnOnlineId, std::regex("^\\s*psn_online_id\\s*=\\s*\"?([\\w_-]+)\"?")},
        {ConfigItem::PsnAccountId, std::regex("^\\s*psn_account_id\\s*=\\s*\"?([\\w/=+]+)\"?")},
        {ConfigItem::PsnRefreshToken, std::regex("^\\s*psn_refresh_token\\s*=\\s*\"?([\\w._-]+)\"?")},
        {ConfigItem::PsnAccessToken, std::regex("^\\s*psn_access_token\\s*=\\s*\"?([\\w._-]+)\"?")},
        {ConfigItem::ConsolePIN, std::regex("^\\s*console_pin\\s*=\\s*\"?(\\d{4})\"?")},
        {ConfigItem::RpKey, std::regex("^\\s*rp_key\\s*=\\s*\"?([\\w/=+]+)\"?")},
        {ConfigItem::RpKeyType, std::regex("^\\s*rp_key_type\\s*=\\s*\"?(\\d)\"?")},
        {ConfigItem::RpRegistKey, std::regex("^\\s*rp_regist_key\\s*=\\s*\"?([\\w/=+]+)\"?")},
        {ConfigItem::VideoResolution, std::regex("^\\s*video_resolution\\s*=\\s*\"?(1080p|720p|540p|360p)\"?")},
        {ConfigItem::VideoFps, std::regex("^\\s*video_fps\\s*=\\s*\"?(60|30)\"?")},
        {ConfigItem::Target, std::regex("^\\s*target\\s*=\\s*\"?(\\d+)\"?")},
        {ConfigItem::Haptic, std::regex("^\\s*haptic\\s*=\\s*\"?(\\d+)\"?")},
        {ConfigItem::RemoteDuid, std::regex("^\\s*remote_duid\\s*=\\s*\"?([0-9a-fA-F]+)\"?")},
        {ConfigItem::CompanionHost, std::regex("^\\s*companion_host\\s*=\\s*\"?([\\w.-]+)\"?")},
        {ConfigItem::CompanionPort, std::regex("^\\s*companion_port\\s*=\\s*\"?(\\d+)\"?")},
        {ConfigItem::PsnTokenExpiresAt, std::regex("^\\s*psn_token_expires_at\\s*=\\s*\"?(\\d+)\"?")},
        {ConfigItem::GlobalDuid, std::regex("^\\s*global_duid\\s*=\\s*\"?([0-9a-fA-F]+)\"?")}
    };

    auto parseLine = [&regexMap](const std::string& line, std::string& value) -> ConfigItem {
        std::smatch match;
        for (const auto& [item, regex] : regexMap) {
            if (std::regex_search(line, match, regex)) {
                value = match[1];
                return item;
            }
        }
        return ConfigItem::Unknown;
    };

    std::string line;
    std::string value;
    Host* currentHost = nullptr;
    bool rpKeySet = false, rpRegistKeySet = false, rpKeyTypeSet = false;

    while (std::getline(configFile, line)) {
        ConfigItem item = parseLine(line, value);

        switch (item) {
            case ConfigItem::Unknown:
                break;
            case ConfigItem::HostName: {
                std::string cleanName = value;
                HostType migratedType = HostType::Discovered;
                if (value.length() > 9 && value.substr(value.length() - 9) == " (Remote)") {
                    cleanName = value;
                    migratedType = HostType::Remote;
                } else if (value.rfind("[Manual] ", 0) == 0) {
                    cleanName = value.substr(9);
                    migratedType = HostType::Manual;
                } else if (value.rfind("[Auto] ", 0) == 0) {
                    cleanName = value.substr(7);
                    migratedType = HostType::Auto;
                }
                if (cleanName != value && hosts.find(cleanName) != hosts.end()) {
                    Host* existing = hosts[cleanName].get();
                    if (existing->hostType == HostType::Manual && migratedType != HostType::Manual) {
                        brls::Logger::info("Skipping {} - Manual host {} already exists", value, cleanName);
                        currentHost = nullptr;
                        break;
                    }
                }
                currentHost = getOrCreateHost(cleanName);
                currentHost->inConfig = true;
                currentHost->hostType = migratedType;
                currentHost->hostName = cleanName;
                rpKeySet = rpRegistKeySet = rpKeyTypeSet = false;
                break;
            }
            case ConfigItem::HostAddr:
                if (currentHost) currentHost->hostAddr = value;
                break;
            case ConfigItem::PsnOnlineId:
                setPsnOnlineId(currentHost, value);
                break;
            case ConfigItem::PsnAccountId:
                setPsnAccountId(currentHost, value);
                break;
            case ConfigItem::PsnRefreshToken:
                setPsnRefreshToken(value);
                break;
            case ConfigItem::PsnAccessToken:
                setPsnAccessToken(value);
                break;
            case ConfigItem::ConsolePIN:
                if (currentHost) currentHost->consolePIN = value;
                break;
            case ConfigItem::RpKey:
                if (currentHost) rpKeySet = setHostRpKey(currentHost, value);
                break;
            case ConfigItem::RpKeyType:
                if (currentHost) rpKeyTypeSet = setHostRpKeyType(currentHost, value);
                break;
            case ConfigItem::RpRegistKey:
                if (currentHost) rpRegistKeySet = setHostRpRegistKey(currentHost, value);
                break;
            case ConfigItem::VideoResolution:
                localVideoResolution = stringToResolution(value);
                remoteVideoResolution = stringToResolution(value);
                break;
            case ConfigItem::VideoFps:
                localVideoFPS = stringToFps(value);
                remoteVideoFPS = stringToFps(value);
                break;
            case ConfigItem::Haptic:
                setHaptic(currentHost, value);
                break;
            case ConfigItem::RemoteDuid:
                if (currentHost) {
                    currentHost->remoteDuid = value;
                }
                break;
            case ConfigItem::Target:
                if (currentHost) setChiakiTarget(currentHost, value);
                break;
            case ConfigItem::CompanionHost:
                companionHost = value;
                break;
            case ConfigItem::CompanionPort:
                companionPort = std::atoi(value.c_str());
                if (companionPort <= 0 || companionPort > 65535) companionPort = 8080;
                break;
            case ConfigItem::PsnTokenExpiresAt:
                setPsnTokenExpiresAt(std::atoll(value.c_str()));
                break;
            case ConfigItem::GlobalDuid:
                setGlobalDuid(value);
                break;
        }

        if (rpKeySet && rpRegistKeySet && rpKeyTypeSet && currentHost) {
            currentHost->rpKeyData = true;
        }
    }

    configFile.close();
    brls::Logger::info("Loaded {} host(s) from legacy config", hosts.size());
}

int SettingsManager::writeFile() {
    brls::Logger::info("Writing config file: {}", TOML_CONFIG_FILE);

    ensureConfigDir();

    toml::table config;

    config.insert("local_video_resolution", resolutionToString(localVideoResolution));
    config.insert("remote_video_resolution", resolutionToString(remoteVideoResolution));
    config.insert("local_video_fps", fpsToInt(localVideoFPS));
    config.insert("remote_video_fps", fpsToInt(remoteVideoFPS));
    config.insert("local_video_bitrate", localVideoBitrate);
    config.insert("remote_video_bitrate", remoteVideoBitrate);
    config.insert("vpn_video_bitrate", vpnVideoBitrate);
    config.insert("vpn_video_resolution", resolutionToString(vpnVideoResolution));
    config.insert("vpn_video_fps", fpsToInt(vpnVideoFPS));
    config.insert("haptic", std::to_underlying(globalHaptic));
    {
        toml::array accountsArr;
        for (const auto& acc : accounts) {
            toml::table at;
            if (!acc.onlineId.empty()) at.insert("online_id", acc.onlineId);
            if (!acc.accountId.empty()) at.insert("account_id", acc.accountId);
            if (!acc.refreshToken.empty()) at.insert("refresh_token", acc.refreshToken);
            if (!acc.accessToken.empty()) at.insert("access_token", acc.accessToken);
            if (acc.tokenExpiresAt > 0) at.insert("token_expires_at", acc.tokenExpiresAt);
            if (!acc.duid.empty()) at.insert("duid", acc.duid);
            accountsArr.push_back(at);
        }
        if (!accountsArr.empty())
            config.insert("accounts", accountsArr);
    }
    if (!defaultAccountId.empty())
        config.insert("default_account", defaultAccountId);
    if (!companionHost.empty())
        config.insert("companion_host", companionHost);
    config.insert("companion_port", companionPort);
    if (manualHostNextId > 0)
        config.insert("manual_host_next_id", manualHostNextId);
    if (holepunchRetry)
        config.insert("holepunch_retry", holepunchRetry);
    config.insert("port_guessing", portGuessing);
    config.insert("port_guessing_count", portGuessingCount);
    config.insert("port_guessing_socks", portGuessingSocks);
    if (powerUserMenuUnlocked)
        config.insert("power_user_menu_unlocked", powerUserMenuUnlocked);
    if (ipcStatsEnabled)
        config.insert("ipc_stats_enabled", ipcStatsEnabled);
    if (unlockBitrateMax)
        config.insert("unlock_bitrate_max", unlockBitrateMax);
    if (!autoReconnect)
        config.insert("auto_reconnect", autoReconnect);
    if (sleepOnExit)
        config.insert("sleep_on_exit", sleepOnExit);
    config.insert("request_idr_on_fec_failure", requestIdrOnFecFailure);
    config.insert("packet_loss_max", static_cast<double>(packetLossMax));
    config.insert("enable_file_logging", enableFileLogging);
    if (localFsrEnabled)
        config.insert("local_fsr_enabled", localFsrEnabled);
    if (remoteFsrEnabled)
        config.insert("remote_fsr_enabled", remoteFsrEnabled);
    if (vpnFsrEnabled)
        config.insert("vpn_fsr_enabled", vpnFsrEnabled);
    if (rcasEnabled)
        config.insert("rcas_enabled", rcasEnabled);
    if (rcasSharpness != 0.2f)
        config.insert("rcas_sharpness", static_cast<double>(rcasSharpness));
    if (enableThreadAffinity)
        config.insert("enable_thread_affinity", enableThreadAffinity);
    if (lowLatencyMode)
        config.insert("low_latency_mode", lowLatencyMode);
    if (!debugLocale.empty())
        config.insert("debug_locale", debugLocale);
    if (debugLwipLog)
        config.insert("debug_lwip_log", debugLwipLog);
    if (debugWireguardLog)
        config.insert("debug_wireguard_log", debugWireguardLog);
    if (debugRenderLog)
        config.insert("debug_render_log", debugRenderLog);
    if (debugChiakiLog)
        config.insert("debug_chiaki_log", debugChiakiLog);
    if (debugDiscoveryLog)
        config.insert("debug_discovery_log", debugDiscoveryLog);
    if (debugFfmpegLog)
        config.insert("debug_ffmpeg_log", debugFfmpegLog);
    config.insert("gyro_source", std::to_underlying(globalGyroSource));

    {
        toml::table rumbleTable;
        rumbleTable.insert("freq_low", static_cast<double>(rumbleFreqLow));
        rumbleTable.insert("freq_high", static_cast<double>(rumbleFreqHigh));
        rumbleTable.insert("envelope_decay", static_cast<double>(rumbleEnvelopeDecay));
        rumbleTable.insert("envelope_attack", static_cast<double>(rumbleEnvelopeAttack));
        config.insert("rumble", rumbleTable);
    }

    {
        toml::table pictureTable;
        pictureTable.insert("enable_dithering", enableDithering);
        pictureTable.insert("dithering_strength", static_cast<double>(ditheringStrength));
        config.insert("picture_adjustments", pictureTable);
    }

    {
        toml::table mappingTable;

        ButtonMapping defaults = getDefaultButtonMapping();
        bool hasCustomMappings = (buttonMapping != defaults);
        if (hasCustomMappings) {
            for (const auto& [chiakiBtn, combo] : buttonMapping) {
                std::string key = chiakiButtonToConfigKey(chiakiBtn);
                if (key.empty()) continue;

                toml::array arr;
                for (uint64_t hidBtn : combo) {
                    std::string btnName = hidButtonToConfigString(hidBtn);
                    if (!btnName.empty()) {
                        arr.push_back(btnName);
                    }
                }
                mappingTable.insert(key, arr);
            }
        }

        mappingTable.insert("touchpad_enabled", touchpadEnabled);
        mappingTable.insert("swipe_up_enabled", swipeUpEnabled);
        mappingTable.insert("swipe_down_enabled", swipeDownEnabled);
        mappingTable.insert("swipe_left_enabled", swipeLeftEnabled);
        mappingTable.insert("swipe_right_enabled", swipeRightEnabled);

        config.insert("button_mapping", mappingTable);
    }

    for (const auto& [name, host] : hosts) {
        brls::Logger::debug("Writing host config: {}", name);
        host->inConfig = true;

        toml::table hostTable;
        hostTable.insert("host_addr", host->getHostAddr());
        hostTable.insert("target", static_cast<int>(host->getChiakiTarget()));
        hostTable.insert("host_type", std::to_underlying(host->hostType));
        if (!host->mac.empty())
            hostTable.insert("mac", host->mac);
        hostTable.insert("nickname", host->getHostName());

        std::string activeAccount;
        if (host->activeProfile >= 0 && host->activeProfile < static_cast<int>(host->profiles.size()))
            activeAccount = host->profiles[host->activeProfile].psnAccountId;
        else if (!host->psnAccountId.empty())
            activeAccount = host->psnAccountId;
        if (!activeAccount.empty())
            hostTable.insert("active_account", activeAccount);

        toml::array regsArr;
        for (const auto& p : host->profiles) {
            if (!p.hasRpKey) continue;
            toml::table rt;
            if (!p.psnAccountId.empty()) rt.insert("account_id", p.psnAccountId);
            if (!p.psnOnlineId.empty()) rt.insert("online_id", p.psnOnlineId);
            if (!p.consolePIN.empty()) rt.insert("console_pin", p.consolePIN);
            rt.insert("rp_key", encodeRpKey(p.rpKey));
            rt.insert("rp_regist_key", encodeRpRegistKey(p.rpRegistKey));
            rt.insert("rp_key_type", static_cast<int64_t>(p.rpKeyType));
            regsArr.push_back(rt);
        }
        if (!regsArr.empty())
            hostTable.insert("registrations", regsArr);

        if (!host->remoteDuid.empty())
            hostTable.insert("remote_duid", host->remoteDuid);

        if (!host->remoteAccountId.empty())
            hostTable.insert("remote_account", host->remoteAccountId);

        if (host->haptic >= 0)
            hostTable.insert("haptic", host->haptic);

        config.insert(name, hostTable);
    }

    std::ofstream configFile(TOML_CONFIG_FILE, std::ios::out | std::ios::trunc);
    if (!configFile.is_open()) {
        brls::Logger::error("Failed to open config file for writing");
        return -1;
    }

    configFile << config;
    configFile.close();
    return 0;
}

std::string SettingsManager::resolutionToString(ChiakiVideoResolutionPreset resolution) {
    switch (resolution) {
        case CHIAKI_VIDEO_RESOLUTION_PRESET_360p: return "360p";
        case CHIAKI_VIDEO_RESOLUTION_PRESET_540p: return "540p";
        case CHIAKI_VIDEO_RESOLUTION_PRESET_720p: return "720p";
        case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p: return "1080p";
        default: return "720p";
    }
}

int SettingsManager::resolutionToInt(ChiakiVideoResolutionPreset resolution) {
    switch (resolution) {
        case CHIAKI_VIDEO_RESOLUTION_PRESET_360p: return 360;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_540p: return 540;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_720p: return 720;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p: return 1080;
        default: return 720;
    }
}

ChiakiVideoResolutionPreset SettingsManager::stringToResolution(const std::string& value) {
    if (value == "1080p") return CHIAKI_VIDEO_RESOLUTION_PRESET_1080p;
    if (value == "720p") return CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
    if (value == "540p") return CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
    if (value == "360p") return CHIAKI_VIDEO_RESOLUTION_PRESET_360p;
    return CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
}

// FPS helpers
std::string SettingsManager::fpsToString(ChiakiVideoFPSPreset fps) {
    switch (fps) {
        case CHIAKI_VIDEO_FPS_PRESET_30: return "30";
        case CHIAKI_VIDEO_FPS_PRESET_60: return "60";
        default: return "60";
    }
}

int SettingsManager::fpsToInt(ChiakiVideoFPSPreset fps) {
    switch (fps) {
        case CHIAKI_VIDEO_FPS_PRESET_30: return 30;
        case CHIAKI_VIDEO_FPS_PRESET_60: return 60;
        default: return 60;
    }
}

ChiakiVideoFPSPreset SettingsManager::stringToFps(const std::string& value) {
    if (value == "60") return CHIAKI_VIDEO_FPS_PRESET_60;
    if (value == "30") return CHIAKI_VIDEO_FPS_PRESET_30;
    return CHIAKI_VIDEO_FPS_PRESET_60;
}

int SettingsManager::getDefaultBitrateForResolution(ChiakiVideoResolutionPreset res) {
    switch (res) {
        case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p: return 15000;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_720p: return 10000;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_540p: return 5000;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_360p: return 2000;
        default: return 10000;
    }
}

int SettingsManager::getMaxBitrateForResolution(ChiakiVideoResolutionPreset res) const {
    if (unlockBitrateMax) {
        switch (res) {
            case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p: return 50000;
            case CHIAKI_VIDEO_RESOLUTION_PRESET_720p: return 40000;
            case CHIAKI_VIDEO_RESOLUTION_PRESET_540p: return 10000;
            case CHIAKI_VIDEO_RESOLUTION_PRESET_360p: return 5000;
            default: return 40000;
        }
    }
    switch (res) {
        case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p: return 25000;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_720p: return 20000;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_540p: return 10000;
        case CHIAKI_VIDEO_RESOLUTION_PRESET_360p: return 5000;
        default: return 20000;
    }
}

bool SettingsManager::isValidIPv4(const std::string& addr) {
    static std::regex ipv4Regex(R"(^(\d+)\.(\d+)\.(\d+)\.(\d+)$)");
    std::smatch match;
    if (!std::regex_match(addr, match, ipv4Regex)) {
        return false;
    }
    for (int i = 1; i <= 4; i++) {
        int octet = std::stoi(match[i].str());
        if (octet > 255) {
            return false;
        }
    }
    return true;
}

bool SettingsManager::isValidFQDN(const std::string& addr) {
    if (addr.empty() || addr.length() > 253) {
        return false;
    }
    if (addr.front() == '.' || addr.back() == '.') {
        return false;
    }
    if (addr.front() == '-' || addr.back() == '-') {
        return false;
    }
    // TLDs must contain at least one letter - reject pure numeric strings like "192.168.50.266"
    bool hasAlpha = std::ranges::any_of(addr, [](unsigned char c) { return std::isalpha(c); });
    if (!hasAlpha) {
        return false;
    }
    static std::regex fqdnRegex(R"(^[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)+$)");
    return std::regex_match(addr, fqdnRegex);
}

bool SettingsManager::isValidHostAddress(const std::string& addr) {
    return isValidIPv4(addr) || isValidFQDN(addr);
}

std::string SettingsManager::getHostName(Host* host) {
    if (host) return host->getHostName();
    brls::Logger::error("Cannot getHostName from nullptr");
    return "";
}

std::string SettingsManager::getHostAddr(Host* host) {
    if (host) return host->getHostAddr();
    brls::Logger::error("Cannot getHostAddr from nullptr");
    return "";
}

void SettingsManager::setHostAddr(Host* host, const std::string& addr) {
    if (host) {
        host->hostAddr = addr;
    } else {
        brls::Logger::error("Cannot setHostAddr on nullptr");
    }
}

void SettingsManager::setDiscovered(Host* host, bool value) {
    if (host) {
        host->discovered = value;
    }
}

std::string SettingsManager::getPsnOnlineId(Host* host) {
    if (host && !host->psnOnlineId.empty()) {
        return host->psnOnlineId;
    }
    const Account* a = getDefaultAccount();
    return a ? a->onlineId : "";
}

void SettingsManager::setPsnOnlineId(Host* host, const std::string& id) {
    if (host) {
        host->psnOnlineId = id;
    } else {
        ensureDefaultAccount().onlineId = id;
    }
}

std::string SettingsManager::getPsnAccountId(Host* host) {
    if (host && !host->psnAccountId.empty()) {
        return host->psnAccountId;
    }
    const Account* a = getDefaultAccount();
    return a ? a->accountId : "";
}

void SettingsManager::setPsnAccountId(Host* host, const std::string& id) {
    if (host) {
        host->psnAccountId = id;
    } else {
        Account& a = ensureDefaultAccount();
        a.accountId = id;
        if (!id.empty()) defaultAccountId = id;
    }
}

std::string SettingsManager::getConsolePIN(Host* host) {
    if (host) return host->consolePIN;
    return "";
}

void SettingsManager::setConsolePIN(Host* host, const std::string& pin) {
    if (host) {
        host->consolePIN = pin;
        host->setActiveProfileConsolePin(pin);
    } else {
        brls::Logger::error("Cannot setConsolePIN on nullptr");
    }
}

ChiakiVideoResolutionPreset SettingsManager::getVideoResolution(Host* host) {
    if (host && host->isRemote()) return remoteVideoResolution;
    return localVideoResolution;
}

ChiakiVideoResolutionPreset SettingsManager::getLocalVideoResolution() const {
    return localVideoResolution;
}

void SettingsManager::setLocalVideoResolution(ChiakiVideoResolutionPreset value) {
    localVideoResolution = value;
}

ChiakiVideoResolutionPreset SettingsManager::getRemoteVideoResolution() const {
    return remoteVideoResolution;
}

void SettingsManager::setRemoteVideoResolution(ChiakiVideoResolutionPreset value) {
    remoteVideoResolution = value;
}

ChiakiVideoFPSPreset SettingsManager::getVideoFPS(Host* host) {
    if (host && host->isRemote()) return remoteVideoFPS;
    return localVideoFPS;
}

ChiakiVideoFPSPreset SettingsManager::getLocalVideoFPS() const {
    return localVideoFPS;
}

void SettingsManager::setLocalVideoFPS(ChiakiVideoFPSPreset value) {
    localVideoFPS = value;
}

ChiakiVideoFPSPreset SettingsManager::getRemoteVideoFPS() const {
    return remoteVideoFPS;
}

void SettingsManager::setRemoteVideoFPS(ChiakiVideoFPSPreset value) {
    remoteVideoFPS = value;
}

int SettingsManager::getVideoBitrate(Host* host) const {
    if (host && host->isRemote()) return remoteVideoBitrate;
    return localVideoBitrate;
}

int SettingsManager::getLocalVideoBitrate() const {
    return localVideoBitrate;
}

void SettingsManager::setLocalVideoBitrate(int value) {
    localVideoBitrate = value;
}

int SettingsManager::getRemoteVideoBitrate() const {
    return remoteVideoBitrate;
}

void SettingsManager::setRemoteVideoBitrate(int value) {
    remoteVideoBitrate = value;
}

int SettingsManager::getVpnVideoBitrate() const {
    return vpnVideoBitrate;
}

void SettingsManager::setVpnVideoBitrate(int value) {
    vpnVideoBitrate = value;
}

ChiakiVideoResolutionPreset SettingsManager::getVpnVideoResolution() const {
    return vpnVideoResolution;
}

void SettingsManager::setVpnVideoResolution(ChiakiVideoResolutionPreset value) {
    vpnVideoResolution = value;
}

ChiakiVideoFPSPreset SettingsManager::getVpnVideoFPS() const {
    return vpnVideoFPS;
}

void SettingsManager::setVpnVideoFPS(ChiakiVideoFPSPreset value) {
    vpnVideoFPS = value;
}

HapticPreset SettingsManager::getHaptic(Host* host) {
    if (!host) return globalHaptic;
    if (host->haptic < 0) return globalHaptic;
    switch (host->haptic) {
        case 0: return HapticPreset::Disabled;
        case 1: return HapticPreset::Weak;
        case 2: return HapticPreset::Strong;
        default: return globalHaptic;
    }
}

void SettingsManager::setHaptic(Host* host, HapticPreset value) {
    if (host) {
        host->haptic = static_cast<int>(value);
    } else {
        globalHaptic = value;
    }
}

void SettingsManager::setHaptic(Host* host, const std::string& value) {
    HapticPreset preset = HapticPreset::Disabled;
    if (value == "1") preset = HapticPreset::Weak;
    else if (value == "2") preset = HapticPreset::Strong;
    setHaptic(host, preset);
}

float SettingsManager::getRumbleFreqLow() const { return rumbleFreqLow; }
void SettingsManager::setRumbleFreqLow(float value) { rumbleFreqLow = std::max(40.0f, std::min(320.0f, value)); }
float SettingsManager::getRumbleFreqHigh() const { return rumbleFreqHigh; }
void SettingsManager::setRumbleFreqHigh(float value) { rumbleFreqHigh = std::max(40.0f, std::min(320.0f, value)); }
float SettingsManager::getRumbleEnvelopeDecay() const { return rumbleEnvelopeDecay; }
void SettingsManager::setRumbleEnvelopeDecay(float value) { rumbleEnvelopeDecay = std::max(0.50f, std::min(0.95f, value)); }
float SettingsManager::getRumbleEnvelopeAttack() const { return rumbleEnvelopeAttack; }
void SettingsManager::setRumbleEnvelopeAttack(float value) { rumbleEnvelopeAttack = std::max(0.20f, std::min(1.00f, value)); }

ChiakiTarget SettingsManager::getChiakiTarget(Host* host) {
    if (host) return host->getChiakiTarget();
    return CHIAKI_TARGET_PS4_UNKNOWN;
}

bool SettingsManager::setChiakiTarget(Host* host, ChiakiTarget target) {
    if (host) {
        host->setChiakiTarget(target);
        return true;
    }
    brls::Logger::error("Cannot setChiakiTarget on nullptr");
    return false;
}

bool SettingsManager::setChiakiTarget(Host* host, const std::string& value) {
    return setChiakiTarget(host, static_cast<ChiakiTarget>(std::atoi(value.c_str())));
}

std::string SettingsManager::getHostRpKey(Host* host) {
    if (!host) {
        brls::Logger::error("Cannot getHostRpKey from nullptr");
        return "";
    }

    if (!host->rpKeyData && !host->registered) {
        return "";
    }

    size_t b64Size = getB64EncodeSize(0x10);
    char b64[b64Size + 1] = {0};

    ChiakiErrorCode err = chiaki_base64_encode(host->rpKey, 0x10, b64, sizeof(b64));
    if (err == CHIAKI_ERR_SUCCESS) {
        return std::string(b64);
    }

    brls::Logger::error("Failed to encode rp_key to base64");
    return "";
}

bool SettingsManager::setHostRpKey(Host* host, const std::string& rpKeyB64) {
    if (!host) {
        brls::Logger::error("Cannot setHostRpKey on nullptr");
        return false;
    }

    size_t rpKeySize = sizeof(host->rpKey);
    ChiakiErrorCode err = chiaki_base64_decode(
        rpKeyB64.c_str(), rpKeyB64.length(),
        host->rpKey, &rpKeySize
    );

    if (err != CHIAKI_ERR_SUCCESS) {
        brls::Logger::error("Failed to decode rp_key from base64: {}", rpKeyB64);
        return false;
    }

    return true;
}

std::string SettingsManager::getHostRpRegistKey(Host* host) {
    if (!host) {
        brls::Logger::error("Cannot getHostRpRegistKey from nullptr");
        return "";
    }

    if (!host->rpKeyData && !host->registered) {
        return "";
    }

    size_t b64Size = getB64EncodeSize(CHIAKI_SESSION_AUTH_SIZE);
    char b64[b64Size + 1] = {0};

    ChiakiErrorCode err = chiaki_base64_encode(
        reinterpret_cast<uint8_t*>(host->rpRegistKey),
        CHIAKI_SESSION_AUTH_SIZE, b64, sizeof(b64)
    );

    if (err == CHIAKI_ERR_SUCCESS) {
        return std::string(b64);
    }

    brls::Logger::error("Failed to encode rp_regist_key to base64");
    return "";
}

bool SettingsManager::setHostRpRegistKey(Host* host, const std::string& rpRegistKeyB64) {
    if (!host) {
        brls::Logger::error("Cannot setHostRpRegistKey on nullptr");
        return false;
    }

    size_t rpRegistKeySize = sizeof(host->rpRegistKey);
    ChiakiErrorCode err = chiaki_base64_decode(
        rpRegistKeyB64.c_str(), rpRegistKeyB64.length(),
        reinterpret_cast<uint8_t*>(host->rpRegistKey), &rpRegistKeySize
    );

    if (err != CHIAKI_ERR_SUCCESS) {
        brls::Logger::error("Failed to decode rp_regist_key from base64: {}", rpRegistKeyB64);
        return false;
    }

    return true;
}

int SettingsManager::getHostRpKeyType(Host* host) {
    if (host) return host->rpKeyType;
    brls::Logger::error("Cannot getHostRpKeyType from nullptr");
    return 0;
}

bool SettingsManager::setHostRpKeyType(Host* host, const std::string& value) {
    if (host) {
        host->rpKeyType = std::atoi(value.c_str());
        return true;
    }
    return false;
}

std::string SettingsManager::getCompanionHost() const {
    return companionHost;
}

void SettingsManager::setCompanionHost(const std::string& host) {
    companionHost = host;
}

int SettingsManager::getCompanionPort() const {
    return companionPort;
}

void SettingsManager::setCompanionPort(int port) {
    if (port > 0 && port <= 65535) {
        companionPort = port;
    }
}

std::string SettingsManager::getPsnRefreshToken() const {
    const Account* a = getDefaultAccount();
    return a ? a->refreshToken : "";
}

void SettingsManager::setPsnRefreshToken(const std::string& token) {
    ensureDefaultAccount().refreshToken = token;
}

std::string SettingsManager::getPsnAccessToken() const {
    const Account* a = getDefaultAccount();
    return a ? a->accessToken : "";
}

void SettingsManager::setPsnAccessToken(const std::string& token) {
    ensureDefaultAccount().accessToken = token;
}

int64_t SettingsManager::getPsnTokenExpiresAt() const {
    const Account* a = getDefaultAccount();
    return a ? a->tokenExpiresAt : 0;
}

void SettingsManager::setPsnTokenExpiresAt(int64_t expiresAt) {
    ensureDefaultAccount().tokenExpiresAt = expiresAt;
}

void SettingsManager::clearPsnTokenData() {
    if (Account* a = getDefaultAccount()) {
        a->accessToken.clear();
        a->refreshToken.clear();
        a->tokenExpiresAt = 0;
        a->duid.clear();
    }
    brls::Logger::info("PSN token data cleared");
}

std::string SettingsManager::getGlobalDuid() const {
    const Account* a = getDefaultAccount();
    return a ? a->duid : "";
}

void SettingsManager::setGlobalDuid(const std::string& duid) {
    ensureDefaultAccount().duid = duid;
}

std::vector<Account>& SettingsManager::getAccounts() {
    return accounts;
}

Account* SettingsManager::findAccount(const std::string& accountId) {
    if (accountId.empty()) return nullptr;
    for (auto& a : accounts) {
        if (a.accountId == accountId) return &a;
    }
    return nullptr;
}

Account* SettingsManager::getAccountForHost(Host* host) {
    if (host) {
        if (host->isRemote() && !host->getRemoteAccountId().empty()) {
            if (Account* a = findAccount(host->getRemoteAccountId())) return a;
        }
        std::string aid = getPsnAccountId(host);
        if (Account* a = findAccount(aid)) return a;
    }
    return getDefaultAccount();
}

bool SettingsManager::hasAnyRemoteAccount() const {
    for (const auto& a : accounts) {
        if (!a.refreshToken.empty()) return true;
    }
    return false;
}

void SettingsManager::upsertAccount(const Account& account) {
    Account* existing = account.accountId.empty() ? nullptr : findAccount(account.accountId);
    if (existing) {
        *existing = account;
    } else {
        accounts.push_back(account);
    }
}

void SettingsManager::removeAccount(const std::string& accountId) {
    accounts.erase(std::remove_if(accounts.begin(), accounts.end(),
        [&](const Account& a) { return a.accountId == accountId; }), accounts.end());
    if (defaultAccountId == accountId) {
        defaultAccountId = accounts.empty() ? "" : accounts.front().accountId;
    }

    std::vector<std::string> hostsToRemove;
    for (auto& [name, host] : hosts) {
        if (!host) continue;

        if (host->getRemoteAccountId() == accountId) {
            if (host->isRemote()) {
                hostsToRemove.push_back(name);
                continue;
            }
            host->setRemoteAccountId("");
        }

        int active = host->activeProfile;
        if (active >= 0 && active < static_cast<int>(host->profiles.size()) &&
            host->profiles[active].psnAccountId == accountId) {
            int replacement = -1;
            for (size_t i = 0; i < host->profiles.size(); i++) {
                const auto& p = host->profiles[i];
                if (p.hasRpKey && !p.psnAccountId.empty() && findAccount(p.psnAccountId)) {
                    replacement = static_cast<int>(i);
                    break;
                }
            }
            host->activeProfile = replacement;
            host->applyActiveProfile();
        }
    }
    for (const auto& name : hostsToRemove) {
        hosts.erase(name);
    }
}

std::string SettingsManager::getDefaultAccountId() const {
    return defaultAccountId;
}

void SettingsManager::setDefaultAccountId(const std::string& accountId) {
    defaultAccountId = accountId;
}

const Account* SettingsManager::getDefaultAccount() const {
    if (accounts.empty()) return nullptr;
    if (!defaultAccountId.empty()) {
        for (const auto& a : accounts) {
            if (a.accountId == defaultAccountId) return &a;
        }
    }
    return &accounts.front();
}

Account* SettingsManager::getDefaultAccount() {
    return const_cast<Account*>(std::as_const(*this).getDefaultAccount());
}

Account& SettingsManager::ensureDefaultAccount() {
    if (Account* a = getDefaultAccount()) return *a;
    accounts.emplace_back();
    return accounts.back();
}

void SettingsManager::migrateConfigToMultiAccount() {
    for (auto& [name, host] : hosts) {
        if (!host) continue;

        if (!host->profiles.empty()) {
            for (const auto& p : host->profiles) {
                if (!p.psnAccountId.empty() && !findAccount(p.psnAccountId)) {
                    Account a;
                    a.accountId = p.psnAccountId;
                    a.onlineId = p.psnOnlineId;
                    accounts.push_back(a);
                }
            }
            continue;
        }

        bool hasFlat = host->rpKeyData || !host->psnAccountId.empty() || !host->psnOnlineId.empty();
        if (!hasFlat) continue;

        legacyConfigDetected = true;

        HostProfile p;
        p.psnAccountId = host->psnAccountId;
        p.psnOnlineId = host->psnOnlineId;
        p.consolePIN = host->consolePIN;
        memcpy(p.rpRegistKey, host->rpRegistKey, sizeof(p.rpRegistKey));
        p.rpKeyType = host->rpKeyType;
        memcpy(p.rpKey, host->rpKey, sizeof(p.rpKey));
        p.hasRpKey = host->rpKeyData;
        host->profiles.push_back(p);
        host->activeProfile = 0;
        host->applyActiveProfile();

        if (!p.psnAccountId.empty() && !findAccount(p.psnAccountId)) {
            Account a;
            a.accountId = p.psnAccountId;
            a.onlineId = p.psnOnlineId;
            accounts.push_back(a);
        }
    }
}

bool SettingsManager::getHolepunchRetry() const {
    return holepunchRetry;
}

void SettingsManager::setHolepunchRetry(bool retry) {
    holepunchRetry = retry;
}

bool SettingsManager::getPortGuessing() const {
    return portGuessing;
}

void SettingsManager::setPortGuessing(bool enabled) {
    portGuessing = enabled;
}

int SettingsManager::getPortGuessingCount() const {
    return portGuessingCount;
}

void SettingsManager::setPortGuessingCount(int count) {
    if (count > 0)
        portGuessingCount = count;
}

int SettingsManager::getPortGuessingSocks() const {
    return portGuessingSocks;
}

void SettingsManager::setPortGuessingSocks(int count) {
    if (count > 0)
        portGuessingSocks = count;
}

bool SettingsManager::getPowerUserMenuUnlocked() const {
    return powerUserMenuUnlocked;
}

void SettingsManager::setPowerUserMenuUnlocked(bool unlocked) {
    powerUserMenuUnlocked = unlocked;
}

bool SettingsManager::getIpcStatsEnabled() const {
    return ipcStatsEnabled;
}

void SettingsManager::setIpcStatsEnabled(bool enabled) {
    ipcStatsEnabled = enabled;
}

bool SettingsManager::getUnlockBitrateMax() const {
    return unlockBitrateMax;
}

void SettingsManager::setUnlockBitrateMax(bool enabled) {
    unlockBitrateMax = enabled;
}

bool SettingsManager::getAutoReconnect() const {
    return autoReconnect;
}

void SettingsManager::setAutoReconnect(bool enabled) {
    autoReconnect = enabled;
}

int SettingsManager::getMinBitrateForResolution(ChiakiVideoResolutionPreset res) const {
    if (unlockBitrateMax) {
        switch (res) {
            case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p: return 5000;
            case CHIAKI_VIDEO_RESOLUTION_PRESET_720p: return 5000;
            case CHIAKI_VIDEO_RESOLUTION_PRESET_540p: return 1000;
            case CHIAKI_VIDEO_RESOLUTION_PRESET_360p: return 1000;
            default: return 1000;
        }
    }
    return 1000;
}

GyroSource SettingsManager::getGyroSource() const {
    return globalGyroSource;
}

void SettingsManager::setGyroSource(GyroSource source) {
    globalGyroSource = source;
}

bool SettingsManager::getSleepOnExit() const {
    return sleepOnExit;
}

void SettingsManager::setSleepOnExit(bool enabled) {
    sleepOnExit = enabled;
}

bool SettingsManager::getRequestIdrOnFecFailure() const {
    return requestIdrOnFecFailure;
}

void SettingsManager::setRequestIdrOnFecFailure(bool enabled) {
    requestIdrOnFecFailure = enabled;
}

float SettingsManager::getPacketLossMax() const {
    return packetLossMax;
}

void SettingsManager::setPacketLossMax(float value) {
    packetLossMax = value;
}

bool SettingsManager::getEasuEnabled() const {
    switch (activeStreamProfile) {
        case StreamProfile::Remote: return remoteFsrEnabled;
        case StreamProfile::Vpn: return vpnFsrEnabled;
        case StreamProfile::Local:
        default: return localFsrEnabled;
    }
}

int SettingsManager::getEasuTargetHeight() const {
    ChiakiVideoResolutionPreset res;
    switch (activeStreamProfile) {
        case StreamProfile::Remote: res = remoteVideoResolution; break;
        case StreamProfile::Vpn: res = vpnVideoResolution; break;
        case StreamProfile::Local:
        default: res = localVideoResolution; break;
    }
    return fsrTargetHeightForResolution(res);
}

bool SettingsManager::getLocalFsrEnabled() const {
    return localFsrEnabled;
}

void SettingsManager::setLocalFsrEnabled(bool enabled) {
    localFsrEnabled = enabled;
}

bool SettingsManager::getRemoteFsrEnabled() const {
    return remoteFsrEnabled;
}

void SettingsManager::setRemoteFsrEnabled(bool enabled) {
    remoteFsrEnabled = enabled;
}

bool SettingsManager::getVpnFsrEnabled() const {
    return vpnFsrEnabled;
}

void SettingsManager::setVpnFsrEnabled(bool enabled) {
    vpnFsrEnabled = enabled;
}

bool SettingsManager::getRcasEnabled() const {
    return rcasEnabled;
}

void SettingsManager::setRcasEnabled(bool enabled) {
    rcasEnabled = enabled;
}

float SettingsManager::getRcasSharpness() const {
    return rcasSharpness;
}

void SettingsManager::setRcasSharpness(float sharpness) {
    rcasSharpness = sharpness;
}

SettingsManager::StreamProfile SettingsManager::getActiveStreamProfile() const {
    return activeStreamProfile;
}

void SettingsManager::setActiveStreamProfile(StreamProfile profile) {
    activeStreamProfile = profile;
}

bool SettingsManager::getEnableFileLogging() const {
    return enableFileLogging;
}

void SettingsManager::setEnableFileLogging(bool enabled) {
    enableFileLogging = enabled;
}

bool SettingsManager::getEnableThreadAffinity() const {
    return enableThreadAffinity;
}

void SettingsManager::setEnableThreadAffinity(bool enabled) {
    enableThreadAffinity = enabled;
}

bool SettingsManager::getLowLatencyMode() const {
    return lowLatencyMode;
}

void SettingsManager::setLowLatencyMode(bool enabled) {
    lowLatencyMode = enabled;
}

bool SettingsManager::getEnableDithering() const {
    return enableDithering;
}

void SettingsManager::setEnableDithering(bool enabled) {
    enableDithering = enabled;
}

float SettingsManager::getDitheringStrength() const {
    return ditheringStrength;
}

void SettingsManager::setDitheringStrength(float value) {
    ditheringStrength = std::max(1.0f, std::min(10.0f, value));
}

std::string SettingsManager::getDebugLocale() const {
    return debugLocale;
}

void SettingsManager::setDebugLocale(const std::string& locale) {
    debugLocale = locale;
}

bool SettingsManager::getDebugLwipLog() const {
    return debugLwipLog;
}

void SettingsManager::setDebugLwipLog(bool enabled) {
    debugLwipLog = enabled;
}

bool SettingsManager::getDebugWireguardLog() const {
    return debugWireguardLog;
}

void SettingsManager::setDebugWireguardLog(bool enabled) {
    debugWireguardLog = enabled;
}

bool SettingsManager::getDebugRenderLog() const {
    return debugRenderLog;
}

void SettingsManager::setDebugRenderLog(bool enabled) {
    debugRenderLog = enabled;
}

bool SettingsManager::getDebugChiakiLog() const {
    return debugChiakiLog;
}

void SettingsManager::setDebugChiakiLog(bool enabled) {
    debugChiakiLog = enabled;
}

bool SettingsManager::getDebugDiscoveryLog() const {
    return debugDiscoveryLog;
}

void SettingsManager::setDebugDiscoveryLog(bool enabled) {
    debugDiscoveryLog = enabled;
}

bool SettingsManager::getDebugFfmpegLog() const {
    return debugFfmpegLog;
}

void SettingsManager::setDebugFfmpegLog(bool enabled) {
    debugFfmpegLog = enabled;
}

bool SettingsManager::isStreamingActive() const {
    return streamingActive;
}

void SettingsManager::setStreamingActive(bool active) {
    streamingActive = active;
}

bool SettingsManager::getTouchpadEnabled() const { return touchpadEnabled; }
void SettingsManager::setTouchpadEnabled(bool enabled) { touchpadEnabled = enabled; }

bool SettingsManager::getSwipeUpEnabled() const { return swipeUpEnabled; }
void SettingsManager::setSwipeUpEnabled(bool enabled) { swipeUpEnabled = enabled; }

bool SettingsManager::getSwipeDownEnabled() const { return swipeDownEnabled; }
void SettingsManager::setSwipeDownEnabled(bool enabled) { swipeDownEnabled = enabled; }

bool SettingsManager::getSwipeLeftEnabled() const { return swipeLeftEnabled; }
void SettingsManager::setSwipeLeftEnabled(bool enabled) { swipeLeftEnabled = enabled; }

bool SettingsManager::getSwipeRightEnabled() const { return swipeRightEnabled; }
void SettingsManager::setSwipeRightEnabled(bool enabled) { swipeRightEnabled = enabled; }

bool SettingsManager::isButtonEnabled(uint32_t chiakiButton) const {
    switch (chiakiButton) {
        case CHIAKI_CONTROLLER_BUTTON_TOUCHPAD: return touchpadEnabled;
        case SWIPE_TOUCHPAD_UP: return swipeUpEnabled;
        case SWIPE_TOUCHPAD_DOWN: return swipeDownEnabled;
        case SWIPE_TOUCHPAD_LEFT: return swipeLeftEnabled;
        case SWIPE_TOUCHPAD_RIGHT: return swipeRightEnabled;
        default: return true;
    }
}

const ButtonMapping& SettingsManager::getButtonMapping() const {
    return buttonMapping;
}

void SettingsManager::setButtonMapping(const ButtonMapping& mapping) {
    buttonMapping = mapping;
}

ButtonMapping SettingsManager::getDefaultButtonMapping() const {
    ButtonMapping defaults;
    defaults[CHIAKI_CONTROLLER_BUTTON_CROSS]    = {HidNpadButton_B};
    defaults[CHIAKI_CONTROLLER_BUTTON_MOON]     = {HidNpadButton_A};
    defaults[CHIAKI_CONTROLLER_BUTTON_BOX]      = {HidNpadButton_Y};
    defaults[CHIAKI_CONTROLLER_BUTTON_PYRAMID]  = {HidNpadButton_X};
    defaults[CHIAKI_CONTROLLER_BUTTON_L1]       = {HidNpadButton_L};
    defaults[CHIAKI_CONTROLLER_BUTTON_R1]       = {HidNpadButton_R};
    defaults[CHIAKI_CONTROLLER_ANALOG_BUTTON_L2] = {HidNpadButton_ZL};
    defaults[CHIAKI_CONTROLLER_ANALOG_BUTTON_R2] = {HidNpadButton_ZR};
    defaults[CHIAKI_CONTROLLER_BUTTON_L3]       = {HidNpadButton_StickL};
    defaults[CHIAKI_CONTROLLER_BUTTON_R3]       = {HidNpadButton_StickR};
    defaults[CHIAKI_CONTROLLER_BUTTON_OPTIONS]  = {HidNpadButton_Plus};
    defaults[CHIAKI_CONTROLLER_BUTTON_SHARE]    = {};
    defaults[CHIAKI_CONTROLLER_BUTTON_TOUCHPAD] = {};
    defaults[CHIAKI_CONTROLLER_BUTTON_PS]       = {HidNpadButton_Minus};
    defaults[SWIPE_TOUCHPAD_UP]    = {};
    defaults[SWIPE_TOUCHPAD_DOWN]  = {};
    defaults[SWIPE_TOUCHPAD_LEFT]  = {};
    defaults[SWIPE_TOUCHPAD_RIGHT] = {};
    return defaults;
}

std::string SettingsManager::getLogFilePath() {
    mkdir(LOG_DIR, 0755);

    DIR* dir = opendir(LOG_DIR);
    if (dir) {
        std::vector<std::string> logFiles;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name.size() > 4 && name.substr(name.size() - 4) == ".log") {
                logFiles.push_back(name);
            }
        }
        closedir(dir);

        if (logFiles.size() >= 10) {
            std::sort(logFiles.begin(), logFiles.end());
            size_t toDelete = logFiles.size() - 9;  // Keep 9, new one makes 10
            for (size_t i = 0; i < toDelete; i++) {
                std::string path = std::string(LOG_DIR) + "/" + logFiles[i];
                remove(path.c_str());
            }
        }
    }

    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    return std::format("{}/{:02}{:02}{:02}_{:02}{:02}{:02}.log",
             LOG_DIR, t->tm_mday, t->tm_mon + 1, t->tm_year % 100,
             t->tm_hour, t->tm_min, t->tm_sec);
}

std::string SettingsManager::getConnectionLogFilePath(const std::string& connType) {
    mkdir(LOG_DIR, 0755);

    DIR* dir = opendir(LOG_DIR);
    if (dir) {
        std::vector<std::string> logFiles;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name.size() > 4 && name.substr(name.size() - 4) == ".log") {
                logFiles.push_back(name);
            }
        }
        closedir(dir);

        if (logFiles.size() >= 10) {
            std::sort(logFiles.begin(), logFiles.end());
            size_t toDelete = logFiles.size() - 9;
            for (size_t i = 0; i < toDelete; i++) {
                std::string path = std::string(LOG_DIR) + "/" + logFiles[i];
                remove(path.c_str());
            }
        }
    }

    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    return std::format("{}/{:02}{:02}{:02}_{:02}{:02}{:02}_{}.log",
             LOG_DIR, t->tm_mday, t->tm_mon + 1, t->tm_year % 100,
             t->tm_hour, t->tm_min, t->tm_sec, connType);
}
