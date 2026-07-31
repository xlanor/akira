#ifndef AKIRA_SETTINGS_FRAME_VIEW_HPP
#define AKIRA_SETTINGS_FRAME_VIEW_HPP

#include <borealis.hpp>
#include <functional>
#include <string>
#include <vector>

class SettingsFrameView : public brls::Box {
public:
    SettingsFrameView();
    ~SettingsFrameView() override;

private:
    struct SubTab {
        std::string name;
        std::function<brls::Box*()> make;
    };
    struct Menu {
        std::string label;
        std::vector<SubTab> subs;
    };

    std::vector<Menu> menus;
    int activeMenu = 0;
    int activeSub = 0;

    brls::Box* menuBar = nullptr;
    brls::Box* subBar = nullptr;
    brls::Box* contentHolder = nullptr;
    brls::Box* descPanel = nullptr;
    brls::Label* descTitle = nullptr;
    brls::Label* descBody = nullptr;
    brls::Image* descImage = nullptr;

    brls::GenericEvent::Subscription focusSub;

    brls::Animatable contentFade{1.0f};
    brls::Animatable entryAnim{1.0f};

    void buildMenus();
    void renderMenuBar();
    void renderSubBar();
    void loadContent(bool focus);
    void switchMenu(int delta);
    void switchSub(int delta);
    void updateDescriptionFromFocus();
    void showDescPanel(bool show);
};

#endif // AKIRA_SETTINGS_FRAME_VIEW_HPP
