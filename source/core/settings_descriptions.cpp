#include "core/settings_descriptions.hpp"

#include <borealis/extern/nlohmann/json.hpp>

#include <array>
#include <fstream>
#include <string>
#include <unordered_map>

namespace akira {

static const std::array<const char*, 6> kHelpFiles = {
    "general", "quality", "controls", "debug", "account", "poweruser"
};

static const std::unordered_map<std::string, SettingDescription>& table()
{
    static const std::unordered_map<std::string, SettingDescription> t = []() {
        std::unordered_map<std::string, SettingDescription> m;

        for (const char* name : kHelpFiles) {
            std::ifstream file(std::string("romfs:/help/") + name + ".json");
            if (!file.is_open())
                continue;

            nlohmann::json j;
            try {
                file >> j;
            } catch (...) {
                continue;
            }

            for (auto it = j.begin(); it != j.end(); ++it) {
                const auto& v = it.value();
                SettingDescription d;
                d.title = v.value("title", std::string());
                d.body  = v.value("body", std::string());
                d.image = v.value("image", std::string());
                m[it.key()] = d;
            }
        }
        return m;
    }();
    return t;
}

bool lookupSettingDescription(const std::string& id, SettingDescription& out)
{
    const auto& t = table();
    auto it = t.find(id);
    if (it == t.end())
        return false;
    out = it->second;
    return true;
}

}
