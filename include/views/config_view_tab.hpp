#ifndef AKIRA_CONFIG_VIEW_TAB_HPP
#define AKIRA_CONFIG_VIEW_TAB_HPP

#include <borealis.hpp>

class ConfigViewTab : public brls::Box {
public:
    ConfigViewTab();
    ~ConfigViewTab() override = default;

    static brls::View* create();

private:
    BRLS_BIND(brls::Box, configContainer, "config/container");

    void loadConfigFromFile();
    void addLine(const std::string& text, bool isHeader = false);
};

#endif
