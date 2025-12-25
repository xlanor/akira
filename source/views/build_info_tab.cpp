#include "views/build_info_tab.hpp"
#include <fstream>
#include <sstream>

BuildInfoTab::BuildInfoTab() {
    this->inflateFromXMLRes("xml/tabs/build_info_tab.xml");
    loadBuildInfo();
}

brls::View* BuildInfoTab::create() {
    return new BuildInfoTab();
}

void BuildInfoTab::loadBuildInfo() {
    std::ifstream file("romfs:/build_info.txt");

    if (!file.is_open()) {
        addInfoRow("Could not load build info", true);
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            // Add spacer for empty lines
            auto* spacer = new brls::Box();
            spacer->setHeight(10);
            infoContainer->addView(spacer);
        } else if (line.find("===") != std::string::npos) {
            // Header line
            addInfoRow(line, true);
        } else {
            addInfoRow(line, false);
        }
    }
}

void BuildInfoTab::addInfoRow(const std::string& text, bool isHeader) {
    auto* label = new brls::Label();
    label->setText(text);

    if (isHeader) {
        label->setFontSize(18);
        label->setTextColor(nvgRGBA(100, 180, 255, 255));
        label->setMarginTop(10);
        label->setMarginBottom(5);
    } else {
        label->setFontSize(14);
        label->setTextColor(nvgRGBA(200, 200, 200, 255));
    }

    infoContainer->addView(label);
}
