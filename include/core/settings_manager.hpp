#ifndef AKIRA_SETTINGS_MANAGER_HPP
#define AKIRA_SETTINGS_MANAGER_HPP

#include <map>
#include <regex>
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
    static constexpr const char* CONFIG_FILE = "sdmc:/switch/akira/akira.conf";

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

    // Companion server settings
    std::string companionHost;
    int companionPort = 8080;

    enum class ConfigItem {
        Unknown,
        HostName,
        HostAddr,
        PsnOnlineId,
        PsnAccountId,
        PsnRefreshToken,
        PsnAccessToken,
        ConsolePIN,
        RpKey,
        RpKeyType,
        RpRegistKey,
        VideoResolution,
        VideoFps,
        Target,
        Haptic,
        RemoteDuid,
        CompanionHost,
        CompanionPort,
        PsnTokenExpiresAt,
        GlobalDuid,
        InvertAB
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
        {ConfigItem::GlobalDuid, std::regex("^\\s*global_duid\\s*=\\s*\"?([0-9a-fA-F]+)\"?")},
        {ConfigItem::InvertAB, std::regex("^\\s*invert_ab\\s*=\\s*\"?(true|false|1|0)\"?")}
    };

    ConfigItem parseLine(const std::string& line, std::string& value);
    static size_t getB64EncodeSize(size_t inputSize);

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

    static std::string resolutionToString(ChiakiVideoResolutionPreset resolution);
    static int resolutionToInt(ChiakiVideoResolutionPreset resolution);
    static ChiakiVideoResolutionPreset stringToResolution(const std::string& value);

    static std::string fpsToString(ChiakiVideoFPSPreset fps);
    static int fpsToInt(ChiakiVideoFPSPreset fps);
    static ChiakiVideoFPSPreset stringToFps(const std::string& value);

    std::string getHostName(Host* host);
    std::string getHostAddr(Host* host);
    void setHostAddr(Host* host, const std::string& addr);

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
