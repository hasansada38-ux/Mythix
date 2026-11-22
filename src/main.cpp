#include <Geode/Geode.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

namespace {
    constexpr int MYTHIX_MENU_TAG = 459001;
}

class MythixMenu : public CCLayer {
protected:
    CCLayerColor* m_rootPanel = nullptr;
    CCLayerColor* m_introPanel = nullptr;
    CCLayerColor* m_featuresPanel = nullptr;
    CCLabelBMFont* m_noclipLabel = nullptr;

    void refreshNoclipLabel() {
        if (!m_noclipLabel)
            return;

        bool enabled = Mod::get()->getSettingValue<bool>("noclip");
        m_noclipLabel->setString(enabled ? "[X]  NOCLIP" : "[ ]  NOCLIP");
        m_noclipLabel->setColor(enabled ? ccc3(255, 125, 55) : ccc3(255, 205, 160));
    }

    CCMenuItemLabel* makeButton(
        const char* text,
        float x,
        float y,
        SEL_MenuHandler callback,
        float scale = 0.48f
    ) {
        auto label = CCLabelBMFont::create(text, "bigFont.fnt");
        if (!label)
            return nullptr;

        label->setScale(scale);
        label->setColor(ccc3(255, 174, 104));

        auto button = CCMenuItemLabel::create(label, this, callback);
        if (button)
            button->setPosition({x, y});
        return button;
    }

    void addFireHeader(CCLayerColor* panel, const char* subtitle) {
        auto header = CCLayerColor::create(ccc4(43, 17, 15, 255), 340.f, 54.f);
        panel->addChild(header);

        auto accent = CCLayerColor::create(ccc4(255, 105, 35, 255), 340.f, 3.f);
        panel->addChild(accent);

        auto gold = CCLayerColor::create(ccc4(255, 183, 66, 255), 84.f, 2.f);
        gold->setPosition({128.f, 52.f});
        panel->addChild(gold);

        auto title = CCLabelBMFont::create("MYTHIX", "bigFont.fnt");
        if (title) {
            title->setScale(0.82f);
            title->setColor(ccc3(255, 139, 55));
            title->setPosition({170.f, 190.f});
            panel->addChild(title);
        }

        auto sub = CCLabelBMFont::create(subtitle, "bigFont.fnt");
        if (sub) {
            sub->setScale(0.29f);
            sub->setColor(ccc3(255, 194, 115));
            sub->setPosition({170.f, 165.f});
            panel->addChild(sub);
        }
    }

public:
    bool init() override {
        if (!CCLayer::init())
            return false;

        auto size = CCDirector::sharedDirector()->getWinSize();

        auto frame = CCLayerColor::create(ccc4(190, 55, 24, 255), 348.f, 228.f);
        if (!frame)
            return false;

        frame->setPosition({(size.width - 348.f) / 2.f, (size.height - 228.f) / 2.f});
        this->addChild(frame);

        m_rootPanel = frame;

        m_introPanel = CCLayerColor::create(ccc4(17, 10, 12, 250), 340.f, 220.f);
        if (!m_introPanel)
            return false;
        m_introPanel->setPosition({4.f, 4.f});
        frame->addChild(m_introPanel);

        addFireHeader(m_introPanel, "F I R E   M O D   M E N U");

        auto introLine = CCLabelBMFont::create("WELCOME TO MYTHIX", "bigFont.fnt");
        if (introLine) {
            introLine->setScale(0.48f);
            introLine->setColor(ccc3(255, 210, 165));
            introLine->setPosition({170.f, 112.f});
            m_introPanel->addChild(introLine);
        }

        auto introMenuButton = makeButton("MOD MENU", 170.f, 72.f, menu_selector(MythixMenu::onOpenFeatures), 0.62f);
        if (introMenuButton) {
            auto menu = CCMenu::create(introMenuButton, nullptr);
            menu->setPosition({0.f, 0.f});
            m_introPanel->addChild(menu);
        }

        auto introClose = makeButton("CLOSE", 170.f, 27.f, menu_selector(MythixMenu::onClose));
        if (introClose) {
            auto menu = CCMenu::create(introClose, nullptr);
            menu->setPosition({0.f, 0.f});
            m_introPanel->addChild(menu);
        }

        m_featuresPanel = CCLayerColor::create(ccc4(17, 10, 12, 250), 340.f, 220.f);
        if (!m_featuresPanel)
            return false;
        m_featuresPanel->setPosition({4.f, 4.f});
        frame->addChild(m_featuresPanel);
        addFireHeader(m_featuresPanel, "M Y T H I X   F E A T U R E S");

        auto divider = CCLayerColor::create(ccc4(115, 48, 31, 255), 292.f, 1.f);
        divider->setPosition({24.f, 143.f});
        m_featuresPanel->addChild(divider);

        auto featuresTitle = CCLabelBMFont::create("GAMEPLAY", "bigFont.fnt");
        if (featuresTitle) {
            featuresTitle->setScale(0.42f);
            featuresTitle->setColor(ccc3(255, 160, 85));
            featuresTitle->setAnchorPoint({0.f, 0.5f});
            featuresTitle->setPosition({28.f, 119.f});
            m_featuresPanel->addChild(featuresTitle);
        }

        m_noclipLabel = CCLabelBMFont::create("[ ]  NOCLIP", "bigFont.fnt");
        if (m_noclipLabel) {
            // Keep the checkbox label compact; position the menu item itself
            // because CCMenuItemLabel controls its child label's position.
            m_noclipLabel->setScale(0.40f);
            m_noclipLabel->setAnchorPoint({0.5f, 0.5f});

            auto noclipButton = CCMenuItemLabel::create(
                m_noclipLabel,
                this,
                menu_selector(MythixMenu::onToggleNoclip)
            );

            if (noclipButton) {
                noclipButton->setPosition({112.f, 82.f});
                auto menu = CCMenu::create(noclipButton, nullptr);
                menu->setPosition({0.f, 0.f});
                m_featuresPanel->addChild(menu);
            }
        }
        refreshNoclipLabel();

        auto backButton = makeButton("BACK", 112.f, 28.f, menu_selector(MythixMenu::onBack));
        auto closeButton = makeButton("CLOSE", 228.f, 28.f, menu_selector(MythixMenu::onClose));
        auto footerMenu = CCMenu::create(backButton, closeButton, nullptr);
        footerMenu->setPosition({0.f, 0.f});
        m_featuresPanel->addChild(footerMenu);

        m_featuresPanel->setVisible(false);

#ifdef GEODE_IS_ANDROID
        // Android keeps the overlay alive so the cube launcher remains tappable.
        m_rootPanel->setVisible(false);

        // Cube 459 is the mobile-only launcher. No flame badge or extra artwork.
        auto cubeSprite = CCSprite::createWithSpriteFrameName("player_459.png");
        if (cubeSprite) {
            cubeSprite->setScale(1.25f);
            auto cubeButton = CCMenuItemSpriteExtra::create(
                cubeSprite,
                this,
                menu_selector(MythixMenu::onMobileToggle)
            );

            if (cubeButton) {
                auto mobileMenu = CCMenu::create(cubeButton, nullptr);
                mobileMenu->setPosition({0.f, 0.f});
                this->addChild(mobileMenu, 10001);

                auto screenSize = CCDirector::sharedDirector()->getWinSize();
                cubeButton->setPosition({
                    screenSize.width - 42.f,
                    screenSize.height - 42.f
                });
            }
        }
        else {
            log::error("Mythix Android launcher: Cube 459 sprite frame player_459.png was not found.");
        }
#endif
        return true;
    }

    void onToggleNoclip(CCObject*) {
        bool enabled = !Mod::get()->getSettingValue<bool>("noclip");
        Mod::get()->setSettingValue<bool>("noclip", enabled);
        refreshNoclipLabel();
    }

    void onOpenFeatures(CCObject*) {
        if (m_introPanel)
            m_introPanel->setVisible(false);
        if (m_featuresPanel)
            m_featuresPanel->setVisible(true);
    }

    void onBack(CCObject*) {
        if (m_featuresPanel)
            m_featuresPanel->setVisible(false);
        if (m_introPanel)
            m_introPanel->setVisible(true);
    }

    void onClose(CCObject*) {
#ifdef GEODE_IS_ANDROID
        if (m_rootPanel)
            m_rootPanel->setVisible(false);
#else
        this->setVisible(false);
#endif
    }

#ifdef GEODE_IS_ANDROID
    void onMobileToggle(CCObject*) {
        if (m_rootPanel)
            m_rootPanel->setVisible(!m_rootPanel->isVisible());
    }
#endif

    void togglePanel() {
#ifdef GEODE_IS_ANDROID
        if (m_rootPanel)
            m_rootPanel->setVisible(!m_rootPanel->isVisible());
#else
        this->setVisible(!this->isVisible());
#endif
    }

    CREATE_FUNC(MythixMenu);
};

namespace {
    void toggleMythixMenu() {
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene)
            return;

        auto existing = typeinfo_cast<MythixMenu*>(scene->getChildByTag(MYTHIX_MENU_TAG));
        if (existing) {
            existing->togglePanel();
            return;
        }

        auto menu = MythixMenu::create();
        if (!menu)
            return;

        menu->setTag(MYTHIX_MENU_TAG);
        scene->addChild(menu, 10000);
        menu->togglePanel();
    }
}

// Create the menu overlay during gameplay, but keep it hidden until A is pressed.
class $modify(MythixPlayLayer, PlayLayer) {
    void onEnter() {
        PlayLayer::onEnter();

        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene || scene->getChildByTag(MYTHIX_MENU_TAG))
            return;

        auto menu = MythixMenu::create();
        if (!menu)
            return;

        menu->setTag(MYTHIX_MENU_TAG);
        scene->addChild(menu, 10000);
#ifndef GEODE_IS_ANDROID
        menu->setVisible(false);
#endif
    }

    // Initial noclip implementation: prevent death while enabled.
    // This does not yet disable every collision response.
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (Mod::get()->getSettingValue<bool>("noclip"))
            return;

        PlayLayer::destroyPlayer(player, object);
    }
};

$on_game(Loaded) {
    listenForKeybindSettingPresses(
        "toggle-menu",
        [](Keybind const&, bool down, bool repeat, double) {
            if (down && !repeat)
                toggleMythixMenu();
        }
    );
}
