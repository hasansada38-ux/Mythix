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

        // Dark outer frame with a warm ember-colored edge.
        auto frame = CCLayerColor::create(
            ccc4(190, 55, 24, 255),
            308.f,
            188.f
        );
        if (!frame)
            return false;

        frame->setPosition({
            (size.width - 308.f) / 2.f,
            (size.height - 188.f) / 2.f
        });
        this->addChild(frame);

        auto panel = CCLayerColor::create(
            ccc4(17, 10, 12, 250),
            300.f,
            180.f
        );
        if (!panel)
            return false;

        panel->setPosition({4.f, 4.f});
        frame->addChild(panel);

        // Header band and layered fire-colored accents.
        auto header = CCLayerColor::create(
            ccc4(43, 17, 15, 255),
            300.f,
            48.f
        );
        panel->addChild(header);

        auto orangeAccent = CCLayerColor::create(
            ccc4(255, 105, 35, 255),
            300.f,
            3.f
        );
        orangeAccent->setPosition({0.f, 0.f});
        panel->addChild(orangeAccent);

        auto goldAccent = CCLayerColor::create(
            ccc4(255, 183, 66, 255),
            72.f,
            2.f
        );
        goldAccent->setPosition({114.f, 46.f});
        panel->addChild(goldAccent);

        // Small ember-like bars decorate the header without external assets.
        auto emberLeft = CCLayerColor::create(
            ccc4(255, 91, 35, 255),
            3.f,
            18.f
        );
        emberLeft->setPosition({18.f, 15.f});
        panel->addChild(emberLeft);

        auto emberRight = CCLayerColor::create(
            ccc4(255, 160, 54, 255),
            3.f,
            18.f
        );
        emberRight->setPosition({279.f, 15.f});
        panel->addChild(emberRight);

        auto title = CCLabelBMFont::create(
            "MYTHIX",
            "bigFont.fnt"
        );
        if (title) {
            title->setScale(0.78f);
            title->setColor(ccc3(255, 139, 55));
            title->setPosition({150.f, 153.f});
            panel->addChild(title);
        }

        auto subtitle = CCLabelBMFont::create(
            "F I R E   M O D   M E N U",
            "bigFont.fnt"
        );
        if (subtitle) {
            subtitle->setScale(0.28f);
            subtitle->setColor(ccc3(255, 194, 115));
            subtitle->setPosition({150.f, 130.f});
            panel->addChild(subtitle);
        }

        auto divider = CCLayerColor::create(
            ccc4(115, 48, 31, 255),
            250.f,
            1.f
        );
        divider->setPosition({25.f, 105.f});
        panel->addChild(divider);

        auto comingSoon = CCLabelBMFont::create(
            "THE FLAMES ARE JUST BEGINNING",
            "bigFont.fnt"
        );
        if (comingSoon) {
            comingSoon->setScale(0.30f);
            comingSoon->setColor(ccc3(205, 155, 130));
            comingSoon->setPosition({150.f, 78.f});
            panel->addChild(comingSoon);
        }

        auto closeLabel = CCLabelBMFont::create(
            "CLOSE",
            "bigFont.fnt"
        );

        if (closeLabel) {
            closeLabel->setScale(0.5f);
            closeLabel->setColor(ccc3(255, 174, 104));

            auto closeButton = CCMenuItemLabel::create(
                closeLabel,
                this,
                menu_selector(MythixMenu::onClose)
            );

            if (closeButton) {
                auto menu = CCMenu::create(closeButton, nullptr);
                menu->setPosition({0.f, 0.f});
                closeButton->setPosition({150.f, 30.f});
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
