#include "views/host_settings_view.hpp"
#include "core/discovery_manager.hpp"

HostSettingsView::HostSettingsView(Host* host)
    : host(host)
{
    this->inflateFromXMLRes("xml/views/host_settings.xml");

    settings = SettingsManager::getInstance();

    titleLabel->setText("Settings for " + host->getHostName());

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
    std::string psnAccountId = psnAccountIdInput->getValue();
    std::string consolePIN = consolePINInput->getValue();

    // Validate Console PIN (must be 4 digits or empty)
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

    // Save per-host settings
    settings->setPsnAccountId(host, psnAccountId);
    settings->setConsolePIN(host, consolePIN);
    host->setHapticRaw(selectedHaptic);  // Save haptic directly since it can be -1
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
