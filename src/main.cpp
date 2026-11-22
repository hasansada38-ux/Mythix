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
    CCLayerColor* m_panel = nullptr;
    CCLabelBMFont* m_noclipLabel = nullptr;

    void refreshNoclipLabel() {
        if (!m_noclipLabel)
            return;

        bool enabled = Mod::get()->getSettingValue<bool>("noclip");
        m_noclipLabel->setString(enabled ? "[X]  NOCLIP" : "[ ]  NOCLIP");
        m_noclipLabel->setColor(enabled ? ccc3(255, 125, 55) : ccc3(255, 205, 160));
    }

public:
    bool init() override {
        if (!CCLayer::init())
            return false;

        auto size = CCDirector::sharedDirector()->getWinSize();

        // Main fire-themed panel.
        auto frame = CCLayerColor::create(ccc4(190, 55, 24, 255), 348.f, 228.f);
        if (!frame)
            return false;

        frame->setPosition({(size.width - 348.f) / 2.f, (size.height - 228.f) / 2.f});
        this->addChild(frame);

        m_panel = CCLayerColor::create(ccc4(17, 10, 12, 250), 340.f, 220.f);
        if (!m_panel)
            return false;

        m_panel->setPosition({4.f, 4.f});
        frame->addChild(m_panel);

        auto header = CCLayerColor::create(ccc4(43, 17, 15, 255), 340.f, 54.f);
        m_panel->addChild(header);

        auto orangeAccent = CCLayerColor::create(ccc4(255, 105, 35, 255), 340.f, 3.f);
        m_panel->addChild(orangeAccent);

        auto goldAccent = CCLayerColor::create(ccc4(255, 183, 66, 255), 84.f, 2.f);
        goldAccent->setPosition({128.f, 52.f});
        m_panel->addChild(goldAccent);

        auto title = CCLabelBMFont::create("MYTHIX", "bigFont.fnt");
        if (title) {
            title->setScale(0.82f);
            title->setColor(ccc3(255, 139, 55));
            title->setPosition({170.f, 190.f});
            m_panel->addChild(title);
        }

        auto subtitle = CCLabelBMFont::create("F I R E   M O D   M E N U", "bigFont.fnt");
        if (subtitle) {
            subtitle->setScale(0.29f);
            subtitle->setColor(ccc3(255, 194, 115));
            subtitle->setPosition({170.f, 165.f});
            m_panel->addChild(subtitle);
        }

        auto divider = CCLayerColor::create(ccc4(115, 48, 31, 255), 292.f, 1.f);
        divider->setPosition({24.f, 143.f});
        m_panel->addChild(divider);

        auto featuresTitle = CCLabelBMFont::create("FEATURES", "bigFont.fnt");
        if (featuresTitle) {
            featuresTitle->setScale(0.42f);
            featuresTitle->setColor(ccc3(255, 160, 85));
            featuresTitle->setAnchorPoint({0.f, 0.5f});
            featuresTitle->setPosition({28.f, 119.f});
            m_panel->addChild(featuresTitle);
        }

        m_noclipLabel = CCLabelBMFont::create("[ ]  NOCLIP", "bigFont.fnt");
        if (m_noclipLabel) {
            m_noclipLabel->setScale(0.48f);
            m_noclipLabel->setAnchorPoint({0.f, 0.5f});
            m_noclipLabel->setPosition({38.f, 82.f});

            auto noclipButton = CCMenuItemLabel::create(
                m_noclipLabel,
                this,
                menu_selector(MythixMenu::onToggleNoclip)
            );

            if (noclipButton) {
                auto menu = CCMenu::create(noclipButton, nullptr);
                menu->setPosition({0.f, 0.f});
                m_panel->addChild(menu);
            }
        }
        refreshNoclipLabel();

        auto closeLabel = CCLabelBMFont::create("CLOSE", "bigFont.fnt");
        if (closeLabel) {
            closeLabel->setScale(0.48f);
            closeLabel->setColor(ccc3(255, 174, 104));

            auto closeButton = CCMenuItemLabel::create(
                closeLabel,
                this,
                menu_selector(MythixMenu::onClose)
            );

            if (closeButton) {
                auto menu = CCMenu::create(closeButton, nullptr);
                menu->setPosition({0.f, 0.f});
                closeButton->setPosition({170.f, 28.f});
                m_panel->addChild(menu);
            }
        }

        // Persistent side tab. The panel starts closed; this button opens it.
        auto tabBackground = CCLayerColor::create(ccc4(105, 35, 20, 245), 104.f, 38.f);
        tabBackground->setPosition({size.width - 104.f, (size.height - 38.f) / 2.f});
        this->addChild(tabBackground, 1);

        auto tabLabel = CCLabelBMFont::create("MOD MENU", "bigFont.fnt");
        if (tabLabel) {
            tabLabel->setScale(0.38f);
            tabLabel->setColor(ccc3(255, 190, 115));

            auto tabButton = CCMenuItemLabel::create(
                tabLabel,
                this,
                menu_selector(MythixMenu::onTogglePanel)
            );

            if (tabButton) {
                auto tabMenu = CCMenu::create(tabButton, nullptr);
                tabMenu->setPosition({0.f, 0.f});
                tabButton->setPosition({size.width - 52.f, size.height / 2.f});
                this->addChild(tabMenu, 2);
            }
        }

        // The side tab remains visible while the main panel is closed.
        m_panel->getParent()->setVisible(false);
        return true;
    }

    void onToggleNoclip(CCObject*) {
        bool enabled = !Mod::get()->getSettingValue<bool>("noclip");
        Mod::get()->setSettingValue<bool>("noclip", enabled);
        refreshNoclipLabel();
    }

    void onTogglePanel(CCObject*) {
        if (m_panel && m_panel->getParent())
            m_panel->getParent()->setVisible(!m_panel->getParent()->isVisible());
    }

    void onClose(CCObject*) {
        if (m_panel && m_panel->getParent())
            m_panel->getParent()->setVisible(false);
    }

    void togglePanel() {
        onTogglePanel(nullptr);
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

// Add the side tab automatically during gameplay.
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
    }

    // First noclip implementation: prevent death while the option is enabled.
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
