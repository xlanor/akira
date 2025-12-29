#include "views/network_utilities_tab.hpp"
#include "core/stun_client.hpp"
#include <vector>

NetworkUtilitiesTab::NetworkUtilitiesTab() {
    this->inflateFromXMLRes("xml/tabs/network_utilities_tab.xml");

    checkNatBtn->registerClickAction([this](brls::View* view) {
        onCheckNatClicked();
        return true;
    });
}

brls::View* NetworkUtilitiesTab::create() {
    return new NetworkUtilitiesTab();
}

void NetworkUtilitiesTab::onCheckNatClicked() {
    if (isChecking) {
        return;
    }

    isChecking = true;
    clearResults();

    StunResult result = StunClient::detectNATType();

    isChecking = false;

    if (!result.error.empty() && result.type == NATType::Unknown) {
        displayResult("Error", result.error, "", 0, true);
    } else {
        displayResult(
            StunClient::natTypeToString(result.type),
            StunClient::natTypeDescription(result.type),
            result.externalIP,
            result.externalPort,
            result.type == NATType::Symmetric || result.type == NATType::UDPBlocked
        );
    }
}

void NetworkUtilitiesTab::clearResults() {
    resultContainer->clearViews();
}

void NetworkUtilitiesTab::displayResult(const std::string& natType, const std::string& description,
                                         const std::string& externalIP, uint16_t externalPort,
                                         bool isError) {
    clearResults();

    NVGcolor typeColor;
    if (isError) {
        typeColor = nvgRGBA(239, 68, 68, 255);
    } else if (natType == "Endpoint-Independent Mapping" || natType == "Open Internet") {
        typeColor = nvgRGBA(16, 185, 129, 255);
    } else if (natType == "Address & Port-Dependent Mapping" || natType == "UDP Blocked") {
        typeColor = nvgRGBA(239, 68, 68, 255);
    } else {
        typeColor = nvgRGBA(245, 158, 11, 255);
    }

    addResultRow("NAT Type", natType, typeColor);

    auto* descLabel = new brls::Label();
    descLabel->setText(description);
    descLabel->setFontSize(18);
    descLabel->setTextColor(nvgRGBA(150, 150, 150, 255));
    descLabel->setMarginTop(5);
    descLabel->setMarginBottom(20);
    resultContainer->addView(descLabel);

    if (!externalIP.empty()) {
        addResultRow("External IP", externalIP);
    }

    if (externalPort > 0) {
        addResultRow("External Port", std::to_string(externalPort));
    }

    addCompatibilityTable();
}

void NetworkUtilitiesTab::addCompatibilityTable() {
    auto* spacer = new brls::Box();
    spacer->setHeight(30);
    resultContainer->addView(spacer);

    auto* header = new brls::Label();
    header->setText("Holepunch Compatibility");
    header->setFontSize(22);
    header->setTextColor(nvgRGBA(100, 180, 255, 255));
    header->setMarginBottom(15);
    resultContainer->addView(header);

    auto* note = new brls::Label();
    note->setText("Check PS5 network NAT using the companion app");
    note->setFontSize(16);
    note->setTextColor(nvgRGBA(150, 150, 150, 255));
    note->setMarginBottom(15);
    resultContainer->addView(note);

    struct CompatRow {
        std::string switchNat;
        std::string ps5Nat;
        std::string result;
        NVGcolor color;
    };

    std::vector<CompatRow> rows = {
        {"Endpoint-Independent", "Endpoint-Independent", "Works", nvgRGBA(16, 185, 129, 255)},
        {"Endpoint-Independent", "Addr & Port-Dependent", "May fail", nvgRGBA(245, 158, 11, 255)},
        {"Addr & Port-Dependent", "Endpoint-Independent", "May fail", nvgRGBA(245, 158, 11, 255)},
        {"Addr & Port-Dependent", "Addr & Port-Dependent", "Won't work", nvgRGBA(239, 68, 68, 255)},
    };

    auto* headerRow = new brls::Box();
    headerRow->setAxis(brls::Axis::ROW);
    headerRow->setMarginBottom(5);

    auto* col1 = new brls::Label();
    col1->setText("Switch");
    col1->setFontSize(16);
    col1->setTextColor(nvgRGBA(180, 180, 180, 255));
    col1->setWidth(200);
    headerRow->addView(col1);

    auto* col2 = new brls::Label();
    col2->setText("PS5 Network");
    col2->setFontSize(16);
    col2->setTextColor(nvgRGBA(180, 180, 180, 255));
    col2->setWidth(200);
    headerRow->addView(col2);

    auto* col3 = new brls::Label();
    col3->setText("Result");
    col3->setFontSize(16);
    col3->setTextColor(nvgRGBA(180, 180, 180, 255));
    headerRow->addView(col3);

    resultContainer->addView(headerRow);

    for (const auto& row : rows) {
        auto* tableRow = new brls::Box();
        tableRow->setAxis(brls::Axis::ROW);
        tableRow->setMarginTop(8);

        auto* c1 = new brls::Label();
        c1->setText(row.switchNat);
        c1->setFontSize(16);
        c1->setTextColor(nvgRGBA(200, 200, 200, 255));
        c1->setWidth(200);
        tableRow->addView(c1);

        auto* c2 = new brls::Label();
        c2->setText(row.ps5Nat);
        c2->setFontSize(16);
        c2->setTextColor(nvgRGBA(200, 200, 200, 255));
        c2->setWidth(200);
        tableRow->addView(c2);

        auto* c3 = new brls::Label();
        c3->setText(row.result);
        c3->setFontSize(16);
        c3->setTextColor(row.color);
        tableRow->addView(c3);

        resultContainer->addView(tableRow);
    }
}

void NetworkUtilitiesTab::addResultRow(const std::string& label, const std::string& value, NVGcolor valueColor) {
    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setMarginTop(10);

    auto* labelView = new brls::Label();
    labelView->setText(label + ":");
    labelView->setFontSize(20);
    labelView->setTextColor(nvgRGBA(150, 150, 150, 255));
    labelView->setWidth(150);
    row->addView(labelView);

    auto* valueView = new brls::Label();
    valueView->setText(value);
    valueView->setFontSize(20);
    valueView->setTextColor(valueColor);
    row->addView(valueView);

    resultContainer->addView(row);
}
