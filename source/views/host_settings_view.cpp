#include "views/host_settings_view.hpp"
#include "core/discovery_manager.hpp"

// Custom button style with colored background, no border
static const brls::ButtonStyle BUTTONSTYLE_BLUE = {
    .shadowType              = brls::ShadowType::GENERIC,
    .hideHighlightBackground = true,
    .highlightPadding = "",
    .borderThickness  = "",
    .enabledBackgroundColor = "",
    .enabledLabelColor      = "brls/button/primary_enabled_text",
    .enabledBorderColor     = "",
    .disabledBackgroundColor = "",
    .disabledLabelColor      = "brls/button/primary_disabled_text",
    .disabledBorderColor     = "",
};

HostSettingsView::HostSettingsView(Host* host)
    : host(host)
{
    this->inflateFromXMLRes("xml/views/host_settings.xml");

    settings = SettingsManager::getInstance();
    originalHostName = host->getHostName();

    titleLabel->setText("Settings for " + host->getHostName());

    initHostNameInput();
    initHostAddrInput();
    initPsnAccountIdInput();
    initLookupButton();
    initConsolePINInput();
    initHapticSelector();

    cancelBtn->registerClickAction([this](brls::View* view) {
        onCancelClicked();
        return true;
    });

    saveBtn->registerClickAction([this](brls::View* view) {
        onSaveClicked();
        return true;
    });

    this->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View* view) {
        onCancelClicked();
        return true;
    });
}

HostSettingsView::~HostSettingsView()
{
}

brls::View* HostSettingsView::create()
{
    return nullptr;
}

void HostSettingsView::initHostNameInput()
{
    std::string displayName = host->getHostName();
    if (host->isRemote() && displayName.length() > 9 && displayName.substr(displayName.length() - 9) == " (Remote)") {
        displayName = displayName.substr(0, displayName.length() - 9);
    }

    hostNameInput->init(
        "Host Name",
        displayName,
        [](std::string text) {},
        "e.g., Living Room PS5",
        "Name to identify this console"
    );
}

void HostSettingsView::initHostAddrInput()
{
    hostAddrInput->init(
        "IP Address",
        host->getHostAddr(),
        [](std::string text) {},
        "e.g., 192.168.1.100",
        "PlayStation's IP address"
    );

    if (host->isRemote()) {
        hostAddrInput->setVisibility(brls::Visibility::GONE);
    }
}

void HostSettingsView::initPsnAccountIdInput()
{
    std::string currentId = host->getPerHostPsnAccountId();

    psnAccountIdInput->init(
        "PSN Account ID",
        currentId,
        [](std::string text) {},
        "Leave empty for global",
        "Base64 encoded account ID (per-host override)"
    );
}

void HostSettingsView::initLookupButton()
{
    psnOnlineIdInput->init(
        "Look up by Online ID",
        "",
        [](std::string text) {},
        "Enter PSN username",
        "Look up account ID from PSN username"
    );

    // Style lookup button with blue
    lookupBtn->setStyle(&BUTTONSTYLE_BLUE);
    lookupBtn->setBackgroundColor(nvgRGBA(92, 157, 255, 255));

    lookupBtn->registerClickAction([this](brls::View* view) {
        std::string onlineId = psnOnlineIdInput->getValue();
        if (onlineId.empty()) {
            brls::Application::notify("Please enter a PSN Online ID first");
            return true;
        }

        brls::Application::notify("Looking up account ID...");

        DiscoveryManager::getInstance()->lookupPsnAccountId(
            onlineId,
            [this](const std::string& accountId) {
                brls::sync([this, accountId]() {
                    psnAccountIdInput->setValue(accountId);
                    psnOnlineIdInput->setValue("");
                    brls::Application::notify("Account ID found!");
                    brls::Logger::info("Found account ID for PSN user");
                });
            },
            [](const std::string& error) {
                brls::sync([error]() {
                    brls::Application::notify("Lookup failed: " + error);
                    brls::Logger::error("PSN account lookup failed: {}", error);
                });
            }
        );

        return true;
    });
}

void HostSettingsView::initConsolePINInput()
{
    std::string currentPin = settings->getConsolePIN(host);

    consolePINInput->init(
        "Console PIN",
        currentPin,
        [](std::string text) {},
        "4-digit PIN",
        "For auto-login (optional)"
    );
}

void HostSettingsView::initHapticSelector()
{
    std::vector<std::string> options = {"Inherit", "Disabled", "Weak", "Strong"};

    // Map host haptic to selector index:
    // -1 (inherit) -> 0, 0 (disabled) -> 1, 1 (weak) -> 2, 2 (strong) -> 3
    int hostHaptic = host->getHapticRaw();
    int currentIndex = 0;
    if (hostHaptic >= 0) {
        currentIndex = hostHaptic + 1;  // +1 because Inherit is at index 0
    }
    selectedHaptic = hostHaptic;

    hapticSelector->init(
        "Haptic Feedback",
        options,
        currentIndex,
        [](int selected) {},
        [this](int selected) {
            selectedHaptic = selected - 1;
            brls::Logger::info("Haptic selection changed to {}", selectedHaptic);
        }
    );
}

void HostSettingsView::onSaveClicked()
{
    std::string newHostName = hostNameInput->getValue();
    std::string newHostAddr = hostAddrInput->getValue();
    std::string psnAccountId = psnAccountIdInput->getValue();
    std::string consolePIN = consolePINInput->getValue();

    if (newHostName.empty()) {
        brls::Application::notify("Host name cannot be empty");
        return;
    }

    if (host->isRemote() && newHostName != originalHostName) {
        if (originalHostName.length() > 9 && originalHostName.substr(originalHostName.length() - 9) == " (Remote)") {
            newHostName = newHostName + " (Remote)";
        }
    }

    if (!host->isRemote()) {
        if (newHostAddr.empty()) {
            brls::Application::notify("IP address cannot be empty");
            return;
        }
        if (!SettingsManager::isValidHostAddress(newHostAddr)) {
            brls::Application::notify("Invalid IP address or hostname");
            return;
        }
    }

    if (!consolePIN.empty()) {
        if (consolePIN.length() != 4) {
            brls::Application::notify("Console PIN must be 4 digits");
            return;
        }
        for (char c : consolePIN) {
            if (!std::isdigit(c)) {
                brls::Application::notify("Console PIN must be digits only");
                return;
            }
        }
    }

    if (newHostName != originalHostName) {
        auto* hostsMap = settings->getHostsMap();
        if (hostsMap->find(newHostName) != hostsMap->end()) {
            brls::Application::notify("A host with this name already exists");
            return;
        }
        settings->renameHost(originalHostName, newHostName);
    }

    if (!host->isRemote()) {
        settings->setHostAddr(host, newHostAddr);
    }
    settings->setPsnAccountId(host, psnAccountId);
    settings->setConsolePIN(host, consolePIN);
    host->setHapticRaw(selectedHaptic);
    settings->writeFile();

    brls::Logger::info("Saved settings for {}: psnAccountId={}, consolePIN={}, haptic={}",
        host->getHostName(),
        psnAccountId.empty() ? "(global)" : psnAccountId,
        consolePIN.empty() ? "(none)" : "****",
        selectedHaptic);

    brls::Application::notify("Settings saved");

    if (onSaved) {
        onSaved();
    }

    brls::Application::popActivity();
}

void HostSettingsView::onCancelClicked()
{
    brls::Application::popActivity();
}
