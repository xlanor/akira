#include "views/config_view_tab.hpp"
#include <fstream>
#include <vector>

static const std::vector<std::string> sensitiveKeys = {
    "global_duid", "psn_access_token", "psn_account_id",
    "psn_refresh_token", "remote_duid", "rp_key", "rp_regist_key"
};

ConfigViewTab::ConfigViewTab() {
    this->inflateFromXMLRes("xml/tabs/config_view_tab.xml");

    revealBtn->registerClickAction([this](brls::View* view) {
        credentialsRevealed = !credentialsRevealed;
        revealBtn->setText(credentialsRevealed ? "Hide Secrets" : "Reveal Secrets");
        configContainer->clearViews();
        loadConfigFromFile();
        return true;
    });

    loadConfigFromFile();
}

brls::View* ConfigViewTab::create() {
    return new ConfigViewTab();
}

std::string ConfigViewTab::censorValue(const std::string& value) {
    if (value.length() <= 5) return value;
    return "****" + value.substr(value.length() - 5);
}

std::string ConfigViewTab::processLine(const std::string& line) {
    if (credentialsRevealed) return line;

    for (const auto& key : sensitiveKeys) {
        size_t keyPos = line.find(key);
        if (keyPos == std::string::npos) continue;

        size_t eqPos = line.find('=', keyPos);
        if (eqPos == std::string::npos) continue;

        size_t valueStart = eqPos + 1;
        while (valueStart < line.length() && (line[valueStart] == ' ' || line[valueStart] == '"'))
            valueStart++;

        size_t valueEnd = line.length();
        if (line.back() == '"') valueEnd--;

        if (valueStart >= valueEnd) continue;

        std::string value = line.substr(valueStart, valueEnd - valueStart);
        std::string censored = censorValue(value);

        bool hasQuotes = line.find('"', eqPos) != std::string::npos;
        if (hasQuotes) {
            return line.substr(0, eqPos + 1) + " \"" + censored + "\"";
        } else {
            return line.substr(0, eqPos + 1) + " " + censored;
        }
    }
    return line;
}

void ConfigViewTab::loadConfigFromFile() {
    std::ifstream file("sdmc:/switch/akira/akira.toml");

    if (!file.is_open()) {
        addLine("Config file not found", false);
        addLine("sdmc:/switch/akira/akira.toml", false);
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            auto* spacer = new brls::Box();
            spacer->setHeight(8);
            configContainer->addView(spacer);
        } else {
            bool isHeader = !line.empty() && line.front() == '[' && line.back() == ']';
            std::string processedLine = processLine(line);
            addLine(processedLine, isHeader);
        }
    }
}

void ConfigViewTab::addLine(const std::string& text, bool isHeader) {
    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(18);

    if (isHeader) {
        label->setTextColor(nvgRGBA(6, 182, 212, 255));
        label->setMarginTop(12);
        label->setMarginBottom(4);
    } else {
        label->setTextColor(nvgRGBA(200, 200, 200, 255));
    }

    configContainer->addView(label);
}
