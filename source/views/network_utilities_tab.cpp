#include "views/network_utilities_tab.hpp"
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
    displayResult(result);
}

void NetworkUtilitiesTab::clearResults() {
    resultContainer->clearViews();
}

void NetworkUtilitiesTab::displayResult(const StunResult& result) {
    clearResults();

    if (!result.error.empty() && result.type == NATType::Unknown) {
        addResultRow("Error", result.error, nvgRGBA(239, 68, 68, 255));
        return;
    }

    std::string mappingStr = StunClient::natTypeToString(result.type);
    NVGcolor mappingColor;
    if (result.type == NATType::FullCone || result.type == NATType::OpenInternet ||
        result.type == NATType::RestrictedCone || result.type == NATType::PortRestrictedCone) {
        mappingColor = nvgRGBA(16, 185, 129, 255);
    } else if (result.type == NATType::Symmetric || result.type == NATType::UDPBlocked) {
        mappingColor = nvgRGBA(239, 68, 68, 255);
    } else {
        mappingColor = nvgRGBA(245, 158, 11, 255);
    }

    addResultRow("Mapping", mappingStr, mappingColor);

    std::string filteringStr = StunClient::filteringTypeToString(result.filtering);
    NVGcolor filteringColor;
    if (result.filtering == FilteringType::EndpointIndependent) {
        filteringColor = nvgRGBA(16, 185, 129, 255);
    } else if (result.filtering == FilteringType::AddressDependent ||
               result.filtering == FilteringType::AddressPortDependent) {
        filteringColor = nvgRGBA(245, 158, 11, 255);
    } else {
        filteringColor = nvgRGBA(150, 150, 150, 255);
    }

    addResultRow("Filtering", filteringStr, filteringColor);

    if (!result.externalIP.empty()) {
        addResultRow("External IP", result.externalIP);
    }

    if (result.externalPort > 0) {
        addResultRow("External Port", std::to_string(result.externalPort));
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
    header->setMarginBottom(10);
    resultContainer->addView(header);

    auto* note = new brls::Label();
    note->setText("Check PS5 network NAT using the companion app");
    note->setFontSize(16);
    note->setTextColor(nvgRGBA(150, 150, 150, 255));
    note->setMarginBottom(15);
    resultContainer->addView(note);

    auto* mappingHeader = new brls::Label();
    mappingHeader->setText("Mapping");
    mappingHeader->setFontSize(18);
    mappingHeader->setTextColor(nvgRGBA(150, 150, 150, 255));
    mappingHeader->setMarginBottom(8);
    resultContainer->addView(mappingHeader);

    struct CompatRow {
        std::string switchNat;
        std::string ps5Nat;
        std::string result;
        NVGcolor color;
    };

    std::vector<CompatRow> mappingRows = {
        {"Endpoint-Independent", "Endpoint-Independent", "Works", nvgRGBA(16, 185, 129, 255)},
        {"Endpoint-Independent", "Addr and Port-Dep", "Won't work", nvgRGBA(239, 68, 68, 255)},
        {"Addr and Port-Dep", "Endpoint-Independent", "Won't work", nvgRGBA(239, 68, 68, 255)},
        {"Addr and Port-Dep", "Addr and Port-Dep", "Won't work", nvgRGBA(239, 68, 68, 255)},
    };

    auto* headerRow = new brls::Box();
    headerRow->setAxis(brls::Axis::ROW);
    headerRow->setMarginBottom(5);
    headerRow->setWidthPercentage(100);

    auto* col1 = new brls::Label();
    col1->setText("Switch");
    col1->setFontSize(14);
    col1->setTextColor(nvgRGBA(180, 180, 180, 255));
    col1->setWidthPercentage(40);
    headerRow->addView(col1);

    auto* col2 = new brls::Label();
    col2->setText("PS5");
    col2->setFontSize(14);
    col2->setTextColor(nvgRGBA(180, 180, 180, 255));
    col2->setWidthPercentage(40);
    headerRow->addView(col2);

    auto* col3 = new brls::Label();
    col3->setText("Result");
    col3->setFontSize(14);
    col3->setTextColor(nvgRGBA(180, 180, 180, 255));
    col3->setWidthPercentage(20);
    headerRow->addView(col3);

    resultContainer->addView(headerRow);

    for (const auto& row : mappingRows) {
        auto* tableRow = new brls::Box();
        tableRow->setAxis(brls::Axis::ROW);
        tableRow->setMarginTop(6);
        tableRow->setWidthPercentage(100);

        auto* c1 = new brls::Label();
        c1->setText(row.switchNat);
        c1->setFontSize(14);
        c1->setTextColor(nvgRGBA(200, 200, 200, 255));
        c1->setWidthPercentage(40);
        tableRow->addView(c1);

        auto* c2 = new brls::Label();
        c2->setText(row.ps5Nat);
        c2->setFontSize(14);
        c2->setTextColor(nvgRGBA(200, 200, 200, 255));
        c2->setWidthPercentage(40);
        tableRow->addView(c2);

        auto* c3 = new brls::Label();
        c3->setText(row.result);
        c3->setFontSize(14);
        c3->setTextColor(row.color);
        c3->setWidthPercentage(20);
        tableRow->addView(c3);

        resultContainer->addView(tableRow);
    }

    auto* spacer2 = new brls::Box();
    spacer2->setHeight(20);
    resultContainer->addView(spacer2);

    auto* filteringHeader = new brls::Label();
    filteringHeader->setText("Filtering (if mapping is Endpoint-Independent)");
    filteringHeader->setFontSize(18);
    filteringHeader->setTextColor(nvgRGBA(150, 150, 150, 255));
    filteringHeader->setMarginBottom(8);
    resultContainer->addView(filteringHeader);

    std::vector<CompatRow> filteringRows = {
        {"Endpoint-Independent", "Endpoint-Independent", "Works", nvgRGBA(16, 185, 129, 255)},
        {"Endpoint-Independent", "Address-Dependent", "Works", nvgRGBA(16, 185, 129, 255)},
        {"Address-Dependent", "Endpoint-Independent", "Works", nvgRGBA(16, 185, 129, 255)},
        {"Address-Dependent", "Address-Dependent", "May fail", nvgRGBA(245, 158, 11, 255)},
    };

    auto* headerRow2 = new brls::Box();
    headerRow2->setAxis(brls::Axis::ROW);
    headerRow2->setMarginBottom(5);
    headerRow2->setWidthPercentage(100);

    auto* col1b = new brls::Label();
    col1b->setText("Switch");
    col1b->setFontSize(14);
    col1b->setTextColor(nvgRGBA(180, 180, 180, 255));
    col1b->setWidthPercentage(40);
    headerRow2->addView(col1b);

    auto* col2b = new brls::Label();
    col2b->setText("PS5");
    col2b->setFontSize(14);
    col2b->setTextColor(nvgRGBA(180, 180, 180, 255));
    col2b->setWidthPercentage(40);
    headerRow2->addView(col2b);

    auto* col3b = new brls::Label();
    col3b->setText("Result");
    col3b->setFontSize(14);
    col3b->setTextColor(nvgRGBA(180, 180, 180, 255));
    col3b->setWidthPercentage(20);
    headerRow2->addView(col3b);

    resultContainer->addView(headerRow2);

    for (const auto& row : filteringRows) {
        auto* tableRow = new brls::Box();
        tableRow->setAxis(brls::Axis::ROW);
        tableRow->setMarginTop(6);
        tableRow->setWidthPercentage(100);

        auto* c1 = new brls::Label();
        c1->setText(row.switchNat);
        c1->setFontSize(14);
        c1->setTextColor(nvgRGBA(200, 200, 200, 255));
        c1->setWidthPercentage(40);
        tableRow->addView(c1);

        auto* c2 = new brls::Label();
        c2->setText(row.ps5Nat);
        c2->setFontSize(14);
        c2->setTextColor(nvgRGBA(200, 200, 200, 255));
        c2->setWidthPercentage(40);
        tableRow->addView(c2);

        auto* c3 = new brls::Label();
        c3->setText(row.result);
        c3->setFontSize(14);
        c3->setTextColor(row.color);
        c3->setWidthPercentage(20);
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
