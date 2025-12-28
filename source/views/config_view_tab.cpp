#include "views/config_view_tab.hpp"
#include <fstream>

ConfigViewTab::ConfigViewTab() {
    this->inflateFromXMLRes("xml/tabs/config_view_tab.xml");
    loadConfigFromFile();
}

brls::View* ConfigViewTab::create() {
    return new ConfigViewTab();
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
            addLine(line, isHeader);
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
