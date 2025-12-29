#ifndef AKIRA_SETTINGS_MANAGER_HPP
#define AKIRA_SETTINGS_MANAGER_HPP

#include <map>
#include <string>

#include <chiaki/common.h>
#include <chiaki/session.h>
#include <chiaki/log.h>

// Forward declaration
class Host;

enum class HapticPreset {
    Disabled = 0,
    Weak = 1,
    Strong = 2
};

class SettingsManager {
protected:
    SettingsManager();
    static SettingsManager* instance;

private:
    static constexpr const char* CONFIG_DIR = "sdmc:/switch/akira";
    static constexpr const char* TOML_CONFIG_FILE = "sdmc:/switch/akira/akira.toml";
    static constexpr const char* LEGACY_CONFIG_FILE = "sdmc:/switch/akira/akira.conf";

    ChiakiLog* log = nullptr;
    std::map<std::string, Host*> hosts;

    // Global settings (defaults)
    ChiakiVideoResolutionPreset globalVideoResolution = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
    ChiakiVideoFPSPreset globalVideoFPS = CHIAKI_VIDEO_FPS_PRESET_60;
    std::string globalPsnOnlineId;
    std::string globalPsnAccountId;
    std::string globalPsnRefreshToken;
    std::string globalPsnAccessToken;
    int64_t globalPsnTokenExpiresAt = 0;
    std::string globalDuid;
    HapticPreset globalHaptic = HapticPreset::Disabled;
    bool globalInvertAB = false;
    int globalVideoBitrate = 5000;

    // Companion server settings
    std::string companionHost;
    int companionPort = 8080;

    void parseTomlFile();
    void parseLegacyFile();
    static size_t getB64EncodeSize(size_t inputSize);
    static bool fileExists(const char* path);

public:
    SettingsManager(const SettingsManager&) = delete;
    void operator=(const SettingsManager&) = delete;
    static SettingsManager* getInstance();

    void setLogger(ChiakiLog* logger);
    ChiakiLog* getLogger() const { return log; }

    void parseFile();
    int writeFile();
    void ensureConfigDir();

    std::map<std::string, Host*>* getHostsMap();
    Host* getOrCreateHost(const std::string& hostName);
    Host* findHostByDuid(const std::string& duid);
    void removeHost(const std::string& hostName);
    void renameHost(const std::string& oldName, const std::string& newName);

    static std::string resolutionToString(ChiakiVideoResolutionPreset resolution);
    static int resolutionToInt(ChiakiVideoResolutionPreset resolution);
    static ChiakiVideoResolutionPreset stringToResolution(const std::string& value);

    static std::string fpsToString(ChiakiVideoFPSPreset fps);
    static int fpsToInt(ChiakiVideoFPSPreset fps);
    static ChiakiVideoFPSPreset stringToFps(const std::string& value);

    static int getDefaultBitrateForResolution(ChiakiVideoResolutionPreset res);
    static int getMaxBitrateForResolution(ChiakiVideoResolutionPreset res);

    std::string getHostName(Host* host);
    std::string getHostAddr(Host* host);
    void setHostAddr(Host* host, const std::string& addr);
    void setDiscovered(Host* host, bool value);

    std::string getPsnOnlineId(Host* host);
    void setPsnOnlineId(Host* host, const std::string& id);

    std::string getPsnAccountId(Host* host);
    void setPsnAccountId(Host* host, const std::string& id);

    std::string getPsnRefreshToken() const;
    void setPsnRefreshToken(const std::string& token);

    std::string getPsnAccessToken() const;
    void setPsnAccessToken(const std::string& token);

    int64_t getPsnTokenExpiresAt() const;
    void setPsnTokenExpiresAt(int64_t expiresAt);
    void clearPsnTokenData();

    std::string getGlobalDuid() const;
    void setGlobalDuid(const std::string& duid);

    std::string getConsolePIN(Host* host);
    void setConsolePIN(Host* host, const std::string& pin);

    ChiakiVideoResolutionPreset getVideoResolution(Host* host);
    void setVideoResolution(Host* host, ChiakiVideoResolutionPreset value);
    void setVideoResolution(Host* host, const std::string& value);

    ChiakiVideoFPSPreset getVideoFPS(Host* host);
    void setVideoFPS(Host* host, ChiakiVideoFPSPreset value);
    void setVideoFPS(Host* host, const std::string& value);

    int getVideoBitrate() const;
    void setVideoBitrate(int value);

    HapticPreset getHaptic(Host* host);
    void setHaptic(Host* host, HapticPreset value);
    void setHaptic(Host* host, const std::string& value);

    ChiakiTarget getChiakiTarget(Host* host);
    bool setChiakiTarget(Host* host, ChiakiTarget target);
    bool setChiakiTarget(Host* host, const std::string& value);

    std::string getHostRpKey(Host* host);
    bool setHostRpKey(Host* host, const std::string& rpKeyB64);

    std::string getHostRpRegistKey(Host* host);
    bool setHostRpRegistKey(Host* host, const std::string& rpRegistKeyB64);

    int getHostRpKeyType(Host* host);
    bool setHostRpKeyType(Host* host, const std::string& value);

    std::string getCompanionHost() const;
    void setCompanionHost(const std::string& host);
    int getCompanionPort() const;
    void setCompanionPort(int port);

    bool getInvertAB() const;
    void setInvertAB(bool invert);
};

#endif // AKIRA_SETTINGS_MANAGER_HPP
