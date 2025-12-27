#include "core/settings_manager.hpp"
#include "core/host.hpp"

#include <borealis.hpp>
#include <chiaki/base64.h>

#include <cstdio>
#include <fstream>
#include <sys/stat.h>

SettingsManager* SettingsManager::instance = nullptr;

SettingsManager::SettingsManager() {}

void SettingsManager::setLogger(ChiakiLog* logger) {
    this->log = logger;
    for (auto& [name, host] : hosts) {
        host->setLogger(logger);
    }
}

SettingsManager* SettingsManager::getInstance() {
    if (instance == nullptr) {
        instance = new SettingsManager();
        instance->ensureConfigDir();
        instance->parseFile();
    }
    return instance;
}

void SettingsManager::ensureConfigDir() {
    mkdir(CONFIG_DIR, 0755);
}

SettingsManager::ConfigItem SettingsManager::parseLine(const std::string& line, std::string& value) {
    std::smatch match;
    for (const auto& [item, regex] : regexMap) {
        if (std::regex_search(line, match, regex)) {
            value = match[1];
            return item;
        }
    }
    return ConfigItem::Unknown;
}

size_t SettingsManager::getB64EncodeSize(size_t inputSize) {
    return ((4 * inputSize / 3) + 3) & ~3;
}

std::map<std::string, Host*>* SettingsManager::getHostsMap() {
    return &hosts;
}

Host* SettingsManager::getOrCreateHost(const std::string& hostName) {
    bool created = false;

    if (hosts.find(hostName) == hosts.end()) {
        hosts[hostName] = new Host(hostName);
        if (log) {
            hosts[hostName]->setLogger(log);
        }
        created = true;
    }

    Host* host = hosts.at(hostName);

    if (created) {
        setPsnOnlineId(host, globalPsnOnlineId);
        setPsnAccountId(host, globalPsnAccountId);
        setVideoResolution(host, globalVideoResolution);
        setVideoFPS(host, globalVideoFPS);
    }

    return host;
}

void SettingsManager::removeHost(const std::string& hostName) {
    auto it = hosts.find(hostName);
    if (it != hosts.end()) {
        delete it->second;
        hosts.erase(it);
    }
}

Host* SettingsManager::findHostByDuid(const std::string& duid) {
    if (duid.empty()) {
        return nullptr;
    }
    for (auto& [name, host] : hosts) {
        if (host && host->getRemoteDuid() == duid) {
            return host;
        }
    }
    return nullptr;
}

void SettingsManager::parseFile() {
    brls::Logger::info("Parsing config file: {}", CONFIG_FILE);

    std::ifstream configFile(CONFIG_FILE);
    if (!configFile.is_open()) {
        brls::Logger::info("Config file not found, using defaults");
        return;
    }

    std::string line;
    std::string value;
    Host* currentHost = nullptr;
    bool rpKeySet = false, rpRegistKeySet = false, rpKeyTypeSet = false;

    while (std::getline(configFile, line)) {
        ConfigItem item = parseLine(line, value);

        switch (item) {
            case ConfigItem::Unknown:
                break;

            case ConfigItem::HostName:
                brls::Logger::debug("Found host: {}", value);
                currentHost = getOrCreateHost(value);
                rpKeySet = rpRegistKeySet = rpKeyTypeSet = false;
                break;

            case ConfigItem::HostAddr:
                if (currentHost) {
                    currentHost->hostAddr = value;
                }
                break;

            case ConfigItem::PsnOnlineId:
                setPsnOnlineId(currentHost, value);
                break;

            case ConfigItem::PsnAccountId:
                setPsnAccountId(currentHost, value);
                break;

            case ConfigItem::PsnRefreshToken:
                globalPsnRefreshToken = value;
                break;

            case ConfigItem::PsnAccessToken:
                globalPsnAccessToken = value;
                break;

            case ConfigItem::ConsolePIN:
                if (currentHost) {
                    currentHost->consolePIN = value;
                }
                break;

            case ConfigItem::RpKey:
                if (currentHost) {
                    rpKeySet = setHostRpKey(currentHost, value);
                }
                break;

            case ConfigItem::RpKeyType:
                if (currentHost) {
                    rpKeyTypeSet = setHostRpKeyType(currentHost, value);
                }
                break;

            case ConfigItem::RpRegistKey:
                if (currentHost) {
                    rpRegistKeySet = setHostRpRegistKey(currentHost, value);
                }
                break;

            case ConfigItem::VideoResolution:
                setVideoResolution(currentHost, value);
                break;

            case ConfigItem::VideoFps:
                setVideoFPS(currentHost, value);
                break;

            case ConfigItem::Haptic:
                setHaptic(currentHost, value);
                break;

            case ConfigItem::RemoteDuid:
                if (currentHost) {
                    currentHost->remoteDuid = value;
                    if (!value.empty() && currentHost->getHostName().rfind("[Remote] ", 0) == 0) {
                        currentHost->isRemoteHost = true;
                    }
                }
                break;

            case ConfigItem::Target:
                if (currentHost) {
                    setChiakiTarget(currentHost, value);
                }
                break;

            case ConfigItem::CompanionHost:
                companionHost = value;
                break;

            case ConfigItem::CompanionPort:
                companionPort = std::atoi(value.c_str());
                if (companionPort <= 0 || companionPort > 65535) {
                    companionPort = 8080;
                }
                break;

            case ConfigItem::PsnTokenExpiresAt:
                globalPsnTokenExpiresAt = std::atoll(value.c_str());
                break;

            case ConfigItem::GlobalDuid:
                globalDuid = value;
                break;

            case ConfigItem::InvertAB:
                globalInvertAB = (value == "true" || value == "1");
                break;
        }

        if (rpKeySet && rpRegistKeySet && rpKeyTypeSet && currentHost) {
            currentHost->rpKeyData = true;
        }
    }

    configFile.close();
    brls::Logger::info("Loaded {} host(s) from config", hosts.size());
}

int SettingsManager::writeFile() {
    brls::Logger::info("Writing config file: {}", CONFIG_FILE);

    ensureConfigDir();

    std::ofstream configFile(CONFIG_FILE, std::ios::out | std::ios::trunc);
    if (!configFile.is_open()) {
        brls::Logger::error("Failed to open config file for writing");
        return -1;
    }

    if (globalVideoResolution) {
        configFile << "video_resolution = \"" << resolutionToString(globalVideoResolution) << "\"\n";
    }
    if (globalVideoFPS) {
        configFile << "video_fps = " << fpsToString(globalVideoFPS) << "\n";
    }
    if (globalHaptic != HapticPreset::Disabled) {
        configFile << "haptic = " << static_cast<int>(globalHaptic) << "\n";
    }
    if (!globalPsnOnlineId.empty()) {
        configFile << "psn_online_id = \"" << globalPsnOnlineId << "\"\n";
    }
    if (!globalPsnAccountId.empty()) {
        configFile << "psn_account_id = \"" << globalPsnAccountId << "\"\n";
    }
    if (!globalPsnRefreshToken.empty()) {
        configFile << "psn_refresh_token = \"" << globalPsnRefreshToken << "\"\n";
    }
    if (!globalPsnAccessToken.empty()) {
        configFile << "psn_access_token = \"" << globalPsnAccessToken << "\"\n";
    }
    if (globalPsnTokenExpiresAt > 0) {
        configFile << "psn_token_expires_at = " << globalPsnTokenExpiresAt << "\n";
    }
    if (!globalDuid.empty()) {
        configFile << "global_duid = \"" << globalDuid << "\"\n";
    }

    if (!companionHost.empty()) {
        configFile << "companion_host = \"" << companionHost << "\"\n";
    }
    if (companionPort != 8080) {
        configFile << "companion_port = " << companionPort << "\n";
    }

    if (globalInvertAB) {
        configFile << "invert_ab = true\n";
    }

    for (const auto& [name, host] : hosts) {
        brls::Logger::debug("Writing host config: {}", name);

        configFile << "\n[" << name << "]\n";
        configFile << "host_addr = \"" << host->getHostAddr() << "\"\n";
        configFile << "target = \"" << static_cast<int>(host->getChiakiTarget()) << "\"\n";

        if (host->videoResolution) {
            configFile << "video_resolution = \"" << resolutionToString(host->videoResolution) << "\"\n";
        }
        if (host->videoFps) {
            configFile << "video_fps = " << fpsToString(host->videoFps) << "\n";
        }
        if (!host->psnOnlineId.empty()) {
            configFile << "psn_online_id = \"" << host->psnOnlineId << "\"\n";
        }
        if (!host->psnAccountId.empty()) {
            configFile << "psn_account_id = \"" << host->psnAccountId << "\"\n";
        }
        if (!host->consolePIN.empty()) {
            configFile << "console_pin = \"" << host->consolePIN << "\"\n";
        }

        if (host->rpKeyData || host->registered) {
            configFile << "rp_key = \"" << getHostRpKey(host) << "\"\n";
            configFile << "rp_regist_key = \"" << getHostRpRegistKey(host) << "\"\n";
            configFile << "rp_key_type = " << host->rpKeyType << "\n";
        }

        if (!host->remoteDuid.empty()) {
            configFile << "remote_duid = \"" << host->remoteDuid << "\"\n";
        }

        if (host->haptic >= 0) {
            configFile << "haptic = " << host->haptic << "\n";
        }
    }

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

std::string SettingsManager::getPsnOnlineId(Host* host) {
    if (!host || host->psnOnlineId.empty()) {
        return globalPsnOnlineId;
    }
    return host->psnOnlineId;
}

void SettingsManager::setPsnOnlineId(Host* host, const std::string& id) {
    if (host) {
        host->psnOnlineId = id;
    } else {
        globalPsnOnlineId = id;
    }
}

std::string SettingsManager::getPsnAccountId(Host* host) {
    if (!host || host->psnAccountId.empty()) {
        return globalPsnAccountId;
    }
    return host->psnAccountId;
}

void SettingsManager::setPsnAccountId(Host* host, const std::string& id) {
    if (host) {
        host->psnAccountId = id;
    } else {
        globalPsnAccountId = id;
    }
}

std::string SettingsManager::getConsolePIN(Host* host) {
    if (host) return host->consolePIN;
    return "";
}

void SettingsManager::setConsolePIN(Host* host, const std::string& pin) {
    if (host) {
        host->consolePIN = pin;
    } else {
        brls::Logger::error("Cannot setConsolePIN on nullptr");
    }
}

ChiakiVideoResolutionPreset SettingsManager::getVideoResolution(Host* host) {
    if (host) return host->videoResolution;
    return globalVideoResolution;
}

void SettingsManager::setVideoResolution(Host* host, ChiakiVideoResolutionPreset value) {
    if (host) {
        host->videoResolution = value;
    } else {
        globalVideoResolution = value;
    }
}

void SettingsManager::setVideoResolution(Host* host, const std::string& value) {
    setVideoResolution(host, stringToResolution(value));
}

ChiakiVideoFPSPreset SettingsManager::getVideoFPS(Host* host) {
    if (host) return host->videoFps;
    return globalVideoFPS;
}

void SettingsManager::setVideoFPS(Host* host, ChiakiVideoFPSPreset value) {
    if (host) {
        host->videoFps = value;
    } else {
        globalVideoFPS = value;
    }
}

void SettingsManager::setVideoFPS(Host* host, const std::string& value) {
    setVideoFPS(host, stringToFps(value));
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
    return globalPsnRefreshToken;
}

void SettingsManager::setPsnRefreshToken(const std::string& token) {
    globalPsnRefreshToken = token;
}

std::string SettingsManager::getPsnAccessToken() const {
    return globalPsnAccessToken;
}

void SettingsManager::setPsnAccessToken(const std::string& token) {
    globalPsnAccessToken = token;
}

int64_t SettingsManager::getPsnTokenExpiresAt() const {
    return globalPsnTokenExpiresAt;
}

void SettingsManager::setPsnTokenExpiresAt(int64_t expiresAt) {
    globalPsnTokenExpiresAt = expiresAt;
}

void SettingsManager::clearPsnTokenData() {
    globalPsnAccessToken.clear();
    globalPsnRefreshToken.clear();
    globalPsnTokenExpiresAt = 0;
    brls::Logger::info("PSN token data cleared");
}

std::string SettingsManager::getGlobalDuid() const {
    return globalDuid;
}

void SettingsManager::setGlobalDuid(const std::string& duid) {
    globalDuid = duid;
}

bool SettingsManager::getInvertAB() const {
    return globalInvertAB;
}

void SettingsManager::setInvertAB(bool invert) {
    globalInvertAB = invert;
}
