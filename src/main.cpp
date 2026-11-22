#include <Geode/Geode.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/loader/SettingV3.hpp>

using namespace geode::prelude;

namespace {
    constexpr int MYTHIX_MENU_TAG = 459001;
}

class MythixMenu : public CCLayer {
public:
    bool init() override {
        if (!CCLayer::init())
            return false;

        auto size = CCDirector::sharedDirector()->getWinSize();

        auto panel = CCLayerColor::create(
            ccc4(10, 17, 38, 240),
            300.f,
            180.f
        );

        if (!panel)
            return false;

        panel->setPosition({
            (size.width - 300.f) / 2.f,
            (size.height - 180.f) / 2.f
        });

        this->addChild(panel);

        auto title = CCLabelBMFont::create(
            "MYTHIX MOD MENU",
            "bigFont.fnt"
        );

        if (title) {
            title->setScale(0.55f);
            title->setPosition({150.f, 145.f});
            panel->addChild(title);
        }

        auto closeLabel = CCLabelBMFont::create(
            "CLOSE",
            "bigFont.fnt"
        );

        if (closeLabel) {
            closeLabel->setScale(0.5f);

            auto closeButton = CCMenuItemLabel::create(
                closeLabel,
                this,
                menu_selector(MythixMenu::onClose)
            );

            if (closeButton) {
                auto menu = CCMenu::create(closeButton, nullptr);
                menu->setPosition({0.f, 0.f});
                closeButton->setPosition({150.f, 35.f});
                panel->addChild(menu);
            }
        }

        return true;
    }

    void onClose(CCObject*) {
        this->removeFromParentAndCleanup(true);
    }

    CREATE_FUNC(MythixMenu);
};

namespace {
    void toggleMythixMenu() {
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene)
            return;

        auto existing = scene->getChildByTag(MYTHIX_MENU_TAG);

        if (existing) {
            existing->removeFromParentAndCleanup(true);
            return;
        }

        auto menu = MythixMenu::create();
        if (!menu)
            return;

        menu->setTag(MYTHIX_MENU_TAG);
        scene->addChild(menu, 10000);
    }
}

$on_game(Loaded) {
    listenForKeybindSettingPresses(
        "toggle-menu",
        [](Keybind const&, bool down, bool repeat, double) {
            if (down && !repeat)
                toggleMythixMenu();
        }
    );
}