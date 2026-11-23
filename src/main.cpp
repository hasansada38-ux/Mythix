#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/CCParticleSystemQuad.hpp>
#include <Geode/modify/UILayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/binding/StartPosObject.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace {
    constexpr char const* NOCLIP_SETTING = "noclip";
    constexpr char const* NO_PARTICLES_SETTING = "no-particles";
    constexpr char const* PANEL_KEYBIND_SETTING = "panel-keybind";
    constexpr char const* STARTPOS_PREV_KEYBIND = "startpos-prev-keybind";
    constexpr char const* STARTPOS_NEXT_KEYBIND = "startpos-next-keybind";

    class WrenchIcon : public CCNode {
    public:
        static WrenchIcon* create() {
            auto ret = new WrenchIcon();
            if (ret && ret->init()) {
                ret->autorelease();
                return ret;
            }
            CC_SAFE_DELETE(ret);
            return nullptr;
        }

        bool init() override {
            if (!CCNode::init())
                return false;

            // A compact, centered wrench glyph. All strokes stay well inside
            // the 32x32 canvas; BasedButtonSprite applies an additional fit scale.
            this->setContentSize({32.f, 32.f});
            auto draw = CCDrawNode::create();
            if (!draw)
                return false;

            auto metal = ccc4f(1.f, 0.91f, 0.78f, 1.f);

            // Rounded handle and end cap.
            draw->drawSegment({9.5f, 9.5f}, {18.5f, 18.5f}, 2.2f, metal);
            draw->drawDot({9.5f, 9.5f}, 2.2f, metal);

            // Open jaw at the head of the wrench.
            draw->drawSegment({17.5f, 17.5f}, {23.5f, 23.5f}, 2.2f, metal);
            draw->drawSegment({17.5f, 17.5f}, {23.5f, 14.5f}, 2.2f, metal);

            this->addChild(draw);
            return true;
        }
    };

    CCLabelBMFont* makeToggleLabel(const char* text) {
        auto label = CCLabelBMFont::create(text, "bigFont.fnt");
        if (label) {
            label->setScale(0.48f);
            label->setColor(ccc3(255, 205, 160));
        }
        return label;
    }
}



class $modify(MythixPlayLayer, PlayLayer) {
    struct Fields {
        std::vector<Ref<StartPosObject>> startPositions;
        int selectedStartPos = 0;
    };

    void addObject(GameObject* object) {
        if (object && object->m_objectID == 31) {
            auto startPos = static_cast<StartPosObject*>(object);
            if (startPos && startPos->m_startSettings && !startPos->m_startSettings->m_disableStartPos)
                m_fields->startPositions.push_back(startPos);
        }
        PlayLayer::addObject(object);
    }

    void createObjectsFromSetupFinished() {
        PlayLayer::createObjectsFromSetupFinished();
        std::sort(m_fields->startPositions.begin(), m_fields->startPositions.end(),
            [](auto const& a, auto const& b) { return a->getPositionX() < b->getPositionX(); });
        if (m_startPosObject) {
            auto it = std::find(m_fields->startPositions.begin(), m_fields->startPositions.end(), m_startPosObject);
            if (it != m_fields->startPositions.end())
                m_fields->selectedStartPos = static_cast<int>(it - m_fields->startPositions.begin()) + 1;
        }
    }

    void switchStartPos(int direction) {
        int count = static_cast<int>(m_fields->startPositions.size());
        if (count <= 0) return;
        int next = (m_fields->selectedStartPos + direction) % (count + 1);
        if (next < 0) next += count + 1;
        m_fields->selectedStartPos = next;
        m_currentCheckpoint = nullptr;
        m_isTestMode = next != 0;
        updateTestModeLabel();
        setStartPosObject(next == 0 ? nullptr : m_fields->startPositions[next - 1]);
        if (m_isPracticeMode) resetLevelFromStart();
        resetLevel();
        startMusic();
    }

    // Preserve the existing Mythix noclip behavior.
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (Mod::get()->getSettingValue<bool>(NOCLIP_SETTING))
            return;

        PlayLayer::destroyPlayer(player, object);
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
    }
};

// Global panel shortcut is configurable in Geode settings; bot systems are intentionally removed.
// Suppress Cocos particle-system rendering while a level is active.
// This catches particle systems created by level objects and player effects.
class $modify(MythixParticleSystem, CCParticleSystemQuad) {
    void draw() {
        if (PlayLayer::get() && Mod::get()->getSettingValue<bool>(NO_PARTICLES_SETTING))
            return;

        CCParticleSystemQuad::draw();
    }
};


class MythixGlobalPanel : public CCLayer {
    CCLabelBMFont* m_noclipLabel = nullptr;
    CCLabelBMFont* m_particlesLabel = nullptr;
public:
    static void toggle() {
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;
        auto existing = scene->getChildByID("mythix-global-panel");
        if (existing) {
            existing->setVisible(!existing->isVisible());
            return;
        }
        auto panel = new MythixGlobalPanel();
        if (panel && panel->init()) {
            panel->setID("mythix-global-panel");
            scene->addChild(panel, 100000);
            panel->autorelease();
        } else {
            CC_SAFE_DELETE(panel);
        }
    }

    bool init() override {
        if (!CCLayer::init()) return false;
        auto win = CCDirector::sharedDirector()->getWinSize();
        auto backdrop = CCLayerColor::create(ccc4(0, 0, 0, 90), win.width, win.height);
        if (backdrop) this->addChild(backdrop);

        auto panel = CCLayerColor::create(ccc4(19, 11, 13, 248), 300.f, 190.f);
        if (!panel) return false;
        panel->setPosition({(win.width - 300.f) / 2.f, (win.height - 190.f) / 2.f});
        this->addChild(panel);

        auto header = CCLayerColor::create(ccc4(72, 27, 19, 255), 300.f, 38.f);
        header->setPosition({0.f, 152.f});
        panel->addChild(header);
        auto title = CCLabelBMFont::create("MYTHIX UTILITIES", "bigFont.fnt");
        if (title) { title->setScale(0.62f); title->setColor(ccc3(255,145,65)); title->setPosition({150.f,19.f}); header->addChild(title); }

        m_noclipLabel = makeToggleLabel(Mod::get()->getSettingValue<bool>(NOCLIP_SETTING) ? "[X]  NOCLIP" : "[ ]  NOCLIP");
        m_particlesLabel = makeToggleLabel(Mod::get()->getSettingValue<bool>(NO_PARTICLES_SETTING) ? "[X]  NO PARTICLES" : "[ ]  NO PARTICLES");
        auto menu = CCMenu::create();
        if (!menu) return false;
        menu->setPosition({0.f,0.f});
        auto n = CCMenuItemLabel::create(m_noclipLabel, this, menu_selector(MythixGlobalPanel::onNoclip));
        auto p = CCMenuItemLabel::create(m_particlesLabel, this, menu_selector(MythixGlobalPanel::onParticles));
        auto closeText = CCLabelBMFont::create("CLOSE", "bigFont.fnt");
        if (closeText) { closeText->setScale(0.42f); closeText->setColor(ccc3(255,174,104)); }
        auto close = closeText ? CCMenuItemLabel::create(closeText, this, menu_selector(MythixGlobalPanel::onClose)) : nullptr;
        if (n) { n->setPosition({150.f,112.f}); menu->addChild(n); }
        if (p) { p->setPosition({150.f,72.f}); menu->addChild(p); }
        if (close) { close->setPosition({265.f,170.f}); menu->addChild(close); }
        panel->addChild(menu);
        return true;
    }

    void onNoclip(CCObject*) {
        bool value = !Mod::get()->getSettingValue<bool>(NOCLIP_SETTING);
        Mod::get()->setSettingValue<bool>(NOCLIP_SETTING, value);
        if (m_noclipLabel) {
            m_noclipLabel->setString(value ? "[X]  NOCLIP" : "[ ]  NOCLIP");
            m_noclipLabel->setColor(value ? ccc3(255,125,55) : ccc3(255,205,160));
        }
    }
    void onParticles(CCObject*) {
        bool value = !Mod::get()->getSettingValue<bool>(NO_PARTICLES_SETTING);
        Mod::get()->setSettingValue<bool>(NO_PARTICLES_SETTING, value);
        if (m_particlesLabel) {
            m_particlesLabel->setString(value ? "[X]  NO PARTICLES" : "[ ]  NO PARTICLES");
            m_particlesLabel->setColor(value ? ccc3(255,125,55) : ccc3(255,205,160));
        }
    }
    void onClose(CCObject*) { this->setVisible(false); }
};

$on_game(Loaded) {
    listenForKeybindSettingPresses(PANEL_KEYBIND_SETTING,
        [](Keybind const&, bool down, bool repeat, double) {
            if (down && !repeat)
                MythixGlobalPanel::toggle();
        }
    );
}

class $modify(MythixPauseLayer, PauseLayer) {
    struct Fields {
        CCLayerColor* panel = nullptr;
        CCLabelBMFont* noclipLabel = nullptr;
        CCLabelBMFont* particlesLabel = nullptr;
    };

    void refreshNoclipLabel() {
        if (!m_fields->noclipLabel)
            return;
        bool enabled = Mod::get()->getSettingValue<bool>(NOCLIP_SETTING);
        m_fields->noclipLabel->setString(enabled ? "[X]  NOCLIP" : "[ ]  NOCLIP");
        m_fields->noclipLabel->setColor(enabled ? ccc3(255, 125, 55) : ccc3(255, 205, 160));
    }

    void refreshParticlesLabel() {
        if (!m_fields->particlesLabel)
            return;
        bool enabled = Mod::get()->getSettingValue<bool>(NO_PARTICLES_SETTING);
        m_fields->particlesLabel->setString(enabled ? "[X]  NO PARTICLES" : "[ ]  NO PARTICLES");
        m_fields->particlesLabel->setColor(enabled ? ccc3(255, 125, 55) : ccc3(255, 205, 160));
    }

    void onToggleNoclip(CCObject*) {
        bool enabled = !Mod::get()->getSettingValue<bool>(NOCLIP_SETTING);
        Mod::get()->setSettingValue<bool>(NOCLIP_SETTING, enabled);
        refreshNoclipLabel();
    }

    void onToggleParticles(CCObject*) {
        bool enabled = !Mod::get()->getSettingValue<bool>(NO_PARTICLES_SETTING);
        Mod::get()->setSettingValue<bool>(NO_PARTICLES_SETTING, enabled);
        refreshParticlesLabel();
    }

    void onCloseUtilities(CCObject*) {
        if (m_fields->panel)
            m_fields->panel->setVisible(false);
    }

    void onOpenUtilities(CCObject*) {
        if (m_fields->panel) {
            refreshNoclipLabel();
            refreshParticlesLabel();
            m_fields->panel->setVisible(true);
        }
    }

    void createUtilitiesPanel() {
        if (m_fields->panel)
            return;

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto panel = CCLayerColor::create(ccc4(19, 11, 13, 248), 300.f, 190.f);
        if (!panel)
            return;

        panel->setPosition({
            (winSize.width - 300.f) / 2.f,
            (winSize.height - 190.f) / 2.f
        });
        panel->setZOrder(10000);
        this->addChild(panel);
        m_fields->panel = panel;

        auto top = CCLayerColor::create(ccc4(72, 27, 19, 255), 300.f, 38.f);
        top->setPosition({0.f, 152.f});
        panel->addChild(top);

        auto title = CCLabelBMFont::create("MYTHIX UTILITIES", "bigFont.fnt");
        if (title) {
            title->setScale(0.62f);
            title->setColor(ccc3(255, 145, 65));
            title->setPosition({150.f, 19.f});
            top->addChild(title);
        }

        m_fields->noclipLabel = makeToggleLabel("[ ]  NOCLIP");
        m_fields->particlesLabel = makeToggleLabel("[ ]  NO PARTICLES");

        auto noclipButton = CCMenuItemLabel::create(
            m_fields->noclipLabel, this, menu_selector(MythixPauseLayer::onToggleNoclip)
        );
        auto particlesButton = CCMenuItemLabel::create(
            m_fields->particlesLabel, this, menu_selector(MythixPauseLayer::onToggleParticles)
        );


        if (noclipButton)
            noclipButton->setPosition({150.f, 112.f});
        if (particlesButton)
            particlesButton->setPosition({150.f, 72.f});

        auto closeLabel = CCLabelBMFont::create("CLOSE", "bigFont.fnt");
        if (closeLabel) {
            closeLabel->setScale(0.42f);
            closeLabel->setColor(ccc3(255, 174, 104));
        }
        auto closeButton = closeLabel
            ? CCMenuItemLabel::create(closeLabel, this, menu_selector(MythixPauseLayer::onCloseUtilities))
            : nullptr;
        if (closeButton)
            closeButton->setPosition({265.f, 170.f});

        auto menu = CCMenu::create();
        if (menu) {
            menu->setPosition({0.f, 0.f});
            if (noclipButton) menu->addChild(noclipButton);
            if (particlesButton) menu->addChild(particlesButton);
            if (closeButton) menu->addChild(closeButton);
            panel->addChild(menu);
        }

        refreshNoclipLabel();
        refreshParticlesLabel();
        panel->setVisible(false);
    }

    void onMythixUtilities(CCObject*) {
        createUtilitiesPanel();
        if (m_fields->panel) {
            refreshNoclipLabel();
            refreshParticlesLabel();
            m_fields->panel->setVisible(true);
        }
    }

    void customSetup() {
        PauseLayer::customSetup();

        createUtilitiesPanel();

        auto icon = WrenchIcon::create();
        if (!icon)
            return;

        auto buttonSprite = CircleButtonSprite::create(
            icon, CircleBaseColor::Green, CircleBaseSize::MediumAlt
        );
        if (!buttonSprite)
            return;
        // Explicitly constrain the wrench glyph to the safe inner area of the circle.
        buttonSprite->setTopRelativeScale(0.45f);
        buttonSprite->setScale(0.6f);

        auto button = CCMenuItemSpriteExtra::create(
            buttonSprite, this, menu_selector(MythixPauseLayer::onMythixUtilities)
        );
        if (!button)
            return;
        button->setID("mythix-utilities-button");

        if (auto menu = this->getChildByID("right-button-menu")) {
            menu->addChild(button);
            menu->updateLayout();
        }
        else {
            // Fallback for layouts where the game's right-side menu ID is unavailable.
            auto fallbackMenu = CCMenu::create(button, nullptr);
            if (!fallbackMenu)
                return;
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            fallbackMenu->setPosition({winSize.width - 38.f, winSize.height - 42.f});
            this->addChild(fallbackMenu, 10001);
        }
    }
};


class $modify(MythixUILayer, UILayer) {
    bool init(GJBaseGameLayer* baseGame) {
        if (!UILayer::init(baseGame)) return false;
        if (!PlayLayer::get()) return true;
        auto menu = CCMenu::create();
        if (!menu) return true;
        menu->setID("mythix-startpos-switcher");
        auto win = CCDirector::sharedDirector()->getWinSize();
        menu->setPosition({win.width / 2.f, 42.f});
        this->addChild(menu, 1000);

        auto prevText = CCLabelBMFont::create("<", "bigFont.fnt");
        auto nextText = CCLabelBMFont::create(">", "bigFont.fnt");
        auto gearText = CCLabelBMFont::create("G", "bigFont.fnt");
        auto count = CCLabelBMFont::create("0/0", "bigFont.fnt");
        auto note = CCLabelBMFont::create("This will be removed when geode 2.2091 comes.", "chatFont.fnt");
        if (prevText) {
            prevText->setScale(.6f);
            menu->addChild(CCMenuItemSpriteExtra::create(prevText, this, menu_selector(MythixUILayer::onPrevious)));
            menu->getChildren();
            static_cast<CCNode*>(menu->getChildren()->objectAtIndex(menu->getChildrenCount()-1))->setPosition({-58.f,0.f});
        }
        if (count) { count->setScale(.42f); count->setID("mythix-startpos-count"); count->setPosition({0.f,0.f}); menu->addChild(count); }
        if (nextText) {
            nextText->setScale(.6f);
            auto item=CCMenuItemSpriteExtra::create(nextText,this,menu_selector(MythixUILayer::onNext));
            item->setPosition({58.f,0.f}); menu->addChild(item);
        }
        if (gearText) {
            gearText->setScale(.38f);
            auto item=CCMenuItemSpriteExtra::create(gearText,this,menu_selector(MythixUILayer::onSettings));
            item->setPosition({88.f,0.f}); menu->addChild(item);
        }
        if (note) { note->setScale(.30f); note->setOpacity(180); note->setPosition({0.f,-17.f}); menu->addChild(note); }
        updateCount();
        return true;
    }
    void onPrevious(CCObject*) { if(auto p=PlayLayer::get()) static_cast<MythixPlayLayer*>(p)->switchStartPos(-1); updateCount(); }
    void onNext(CCObject*) { if(auto p=PlayLayer::get()) static_cast<MythixPlayLayer*>(p)->switchStartPos(1); updateCount(); }
    void onSettings(CCObject*) { openSettingsPopup(Mod::get()); }
    void updateCount() {
        auto menu=this->getChildByID("mythix-startpos-switcher");
        auto label=menu ? typeinfo_cast<CCLabelBMFont*>(menu->getChildByID("mythix-startpos-count")) : nullptr;
        auto p=PlayLayer::get();
        if(!label || !p) return;
        auto layer=static_cast<MythixPlayLayer*>(p);
        label->setString(fmt::format("{}/{}",layer->m_fields->selectedStartPos,layer->m_fields->startPositions.size()).c_str());
    }
};

$on_game(Loaded) {
    listenForKeybindSettingPresses(STARTPOS_PREV_KEYBIND, [](Keybind const&, bool down, bool repeat, double) {
        if(down && !repeat) if(auto p=PlayLayer::get()) static_cast<MythixPlayLayer*>(p)->switchStartPos(-1);
    });
    listenForKeybindSettingPresses(STARTPOS_NEXT_KEYBIND, [](Keybind const&, bool down, bool repeat, double) {
        if(down && !repeat) if(auto p=PlayLayer::get()) static_cast<MythixPlayLayer*>(p)->switchStartPos(1);
    });
}
