#ifndef AKIRA_NETWORK_UTILITIES_TAB_HPP
#define AKIRA_NETWORK_UTILITIES_TAB_HPP

#include <borealis.hpp>

class NetworkUtilitiesTab : public brls::Box {
public:
    NetworkUtilitiesTab();
    ~NetworkUtilitiesTab() override = default;

    static brls::View* create();

private:
    BRLS_BIND(brls::Box, resultContainer, "network/resultContainer");
    BRLS_BIND(brls::Button, checkNatBtn, "network/checkNatBtn");

    bool isChecking = false;

    void onCheckNatClicked();
    void displayResult(const std::string& natType, const std::string& description,
                       const std::string& externalIP, uint16_t externalPort,
                       bool isError = false);
    void clearResults();
    void addResultRow(const std::string& label, const std::string& value,
                      NVGcolor valueColor = nvgRGBA(200, 200, 200, 255));
    void addCompatibilityTable();
};

#endif
