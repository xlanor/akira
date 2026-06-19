#include "views/account_tab.hpp"
#include "views/account_edit_view.hpp"

#include <borealis/core/i18n.hpp>
using namespace brls::literals;

class AccountItemView : public brls::Box {
public:
    AccountItemView(AccountTab* tab, const Account& account, bool isDefault)
        : tab(tab), accountId(account.accountId)
    {
        this->setAxis(brls::Axis::ROW);
        this->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setPadding(15);
        this->setMarginBottom(10);
        this->setBackgroundColor(brls::Application::getTheme().getColor("color/card"));
        this->setCornerRadius(8);

        auto* infoBox = new brls::Box();
        infoBox->setAxis(brls::Axis::COLUMN);
        infoBox->setGrow(1);
        infoBox->setShrink(1);

        auto* nameRow = new brls::Box();
        nameRow->setAxis(brls::Axis::ROW);
        nameRow->setAlignItems(brls::AlignItems::CENTER);
        nameRow->setMarginBottom(5);

        auto* nameLabel = new brls::Label();
        std::string label = account.label();
        if (label.empty()) label = "akira/account/unnamed"_i18n;
        nameLabel->setText(label);
        nameLabel->setFontSize(18);
        nameRow->addView(nameLabel);

        auto* typeBadge = new brls::Box();
        bool remote = account.isRemote();
        typeBadge->setBackgroundColor(remote ? nvgRGBA(59, 130, 246, 255) : nvgRGBA(107, 114, 128, 255));
        typeBadge->setCornerRadius(4);
        typeBadge->setPaddingTop(2);
        typeBadge->setPaddingBottom(2);
        typeBadge->setPaddingLeft(6);
        typeBadge->setPaddingRight(6);
        typeBadge->setMarginLeft(8);
        auto* typeBadgeLabel = new brls::Label();
        typeBadgeLabel->setText(remote ? "akira/account/remote"_i18n : "akira/account/local"_i18n);
        typeBadgeLabel->setFontSize(11);
        typeBadgeLabel->setTextColor(nvgRGBA(255, 255, 255, 255));
        typeBadge->addView(typeBadgeLabel);
        nameRow->addView(typeBadge);

        if (isDefault) {
            auto* defBadge = new brls::Box();
            defBadge->setBackgroundColor(nvgRGBA(74, 222, 128, 255));
            defBadge->setCornerRadius(4);
            defBadge->setPaddingTop(2);
            defBadge->setPaddingBottom(2);
            defBadge->setPaddingLeft(6);
            defBadge->setPaddingRight(6);
            defBadge->setMarginLeft(8);
            auto* defBadgeLabel = new brls::Label();
            defBadgeLabel->setText("akira/account/default_badge"_i18n);
            defBadgeLabel->setFontSize(11);
            defBadgeLabel->setTextColor(nvgRGBA(0, 0, 0, 255));
            defBadge->addView(defBadgeLabel);
            nameRow->addView(defBadge);
        }

        infoBox->addView(nameRow);

        auto* subLabel = new brls::Label();
        subLabel->setText(account.accountId.empty() ? "akira/account/no_account_id"_i18n : account.accountId);
        subLabel->setFontSize(13);
        subLabel->setTextColor(nvgRGBA(150, 150, 150, 255));
        infoBox->addView(subLabel);

        this->addView(infoBox);

        auto* buttonBox = new brls::Box();
        buttonBox->setAxis(brls::Axis::ROW);
        buttonBox->setShrink(0);

        if (!isDefault && !accountId.empty()) {
            auto* defaultBtn = new brls::Button();
            defaultBtn->setText("akira/account/set_default"_i18n);
            defaultBtn->setStyle(&brls::BUTTONSTYLE_BORDERED);
            defaultBtn->setShrink(0);
            defaultBtn->setMarginRight(10);
            std::string id = accountId;
            AccountTab* t = tab;
            defaultBtn->registerClickAction([t, id](brls::View* view) {
                auto* settings = SettingsManager::getInstance();
                settings->setDefaultAccountId(id);
                settings->writeFile();
                brls::Application::notify("akira/account/default_set"_i18n);
                if (t) t->syncList();
                return true;
            });
            buttonBox->addView(defaultBtn);
        }

        auto* editBtn = new brls::Button();
        editBtn->setText("akira/account/edit"_i18n);
        editBtn->setStyle(&brls::BUTTONSTYLE_BORDERED);
        editBtn->setShrink(0);
        editBtn->setMarginRight(10);
        {
            std::string id = accountId;
            AccountTab* t = tab;
            editBtn->registerClickAction([t, id](brls::View* view) {
                if (t) t->openEditor(id);
                return true;
            });
        }
        buttonBox->addView(editBtn);

        auto* deleteBtn = new brls::Button();
        deleteBtn->setText("akira/common/delete"_i18n);
        deleteBtn->setStyle(&brls::BUTTONSTYLE_BORDERED);
        deleteBtn->setShrink(0);
        {
            std::string id = accountId;
            std::string name = label;
            AccountTab* t = tab;
            deleteBtn->registerClickAction([t, id, name](brls::View* view) {
                auto* dialog = new brls::Dialog(brls::getStr("akira/account/delete_confirm", name));
                dialog->addButton("akira/common/cancel"_i18n, []() {});
                dialog->addButton("akira/common/delete"_i18n, [t, id]() {
                    auto* settings = SettingsManager::getInstance();
                    settings->removeAccount(id);
                    settings->writeFile();
                    brls::Application::notify("akira/account/deleted"_i18n);
                    if (t) t->syncList();
                });
                dialog->open();
                return true;
            });
        }
        buttonBox->addView(deleteBtn);

        this->addView(buttonBox);
    }

private:
    AccountTab* tab;
    std::string accountId;
};

AccountTab::AccountTab() {
    this->inflateFromXMLRes("xml/tabs/account.xml");

    settings = SettingsManager::getInstance();

    addBtn->registerClickAction([this](brls::View* view) {
        openEditor("");
        return true;
    });

    syncList();
}

brls::View* AccountTab::create() {
    return new AccountTab();
}

void AccountTab::willAppear(bool resetState) {
    Box::willAppear(resetState);
    syncList();
}

void AccountTab::openEditor(const std::string& accountId) {
    auto* editView = new AccountEditView(accountId);
    editView->setOnSaved([this]() {
        brls::sync([this]() {
            syncList();
            if (addBtn) brls::Application::giveFocus(addBtn);
        });
    });
    brls::Application::pushActivity(new brls::Activity(editView));
}

void AccountTab::syncList() {
    if (!container) return;

    brls::Application::giveFocus(this);

    container->clearViews();

    auto& accounts = settings->getAccounts();
    std::string defaultId = settings->getDefaultAccountId();

    for (const auto& account : accounts) {
        bool isDefault = !account.accountId.empty() && account.accountId == defaultId;
        auto* item = new AccountItemView(this, account, isDefault);
        container->addView(item);
    }

    bool hasAccounts = !accounts.empty();
    if (emptyMessage) {
        emptyMessage->setVisibility(hasAccounts ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    }

    if (hasAccounts) {
        brls::Application::giveFocus(container);
    } else if (addBtn) {
        brls::Application::giveFocus(addBtn);
    }
}
