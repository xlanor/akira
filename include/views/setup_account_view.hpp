#ifndef AKIRA_SETUP_ACCOUNT_VIEW_HPP
#define AKIRA_SETUP_ACCOUNT_VIEW_HPP

#include <borealis.hpp>

class SetupAccountView : public brls::Box {
public:
    SetupAccountView();

    void willAppear(bool resetState) override;
    brls::View* getDefaultFocus() override;
    static brls::View* create();

private:
    BRLS_BIND(brls::Label, glyph, "setup/glyph");
    BRLS_BIND(brls::Button, startBtn, "setup/startBtn");
};

#endif // AKIRA_SETUP_ACCOUNT_VIEW_HPP
