#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/CCParticleSystemQuad.hpp>
#include <Geode/modify/UILayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/binding/StartPosObject.hpp>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCControlSlider.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <sstream>

using namespace geode::prelude;

namespace {
    constexpr char const* NOCLIP_SETTING = "noclip";
    constexpr char const* NO_PARTICLES_SETTING = "no-particles";
    constexpr char const* PANEL_KEYBIND_SETTING = "panel-keybind";
    constexpr char const* STARTPOS_PREV_KEYBIND = "startpos-prev-keybind";
    constexpr char const* STARTPOS_NEXT_KEYBIND = "startpos-next-keybind";
    constexpr char const* PANEL_FADE_SETTING = "panel-fade-animation";
    constexpr char const* PANEL_ACCENT_HUE_SETTING = "panel-accent-hue";
    constexpr char const* RATE_LEVELS_SETTING = "rate-levels";
    constexpr char const* FAKE_RATINGS_SAVE_KEY = "fake-level-ratings";

    struct FakeLevelRating { int stars = 0; int tier = 0; };

    std::map<int, FakeLevelRating> loadFakeRatings() {
        std::map<int, FakeLevelRating> ratings;
        std::stringstream input(Mod::get()->getSavedValue<std::string>(FAKE_RATINGS_SAVE_KEY, ""));
        std::string entry;
        while (std::getline(input, entry, ';')) {
            std::stringstream fields(entry);
            std::string id, stars, tier;
            if (std::getline(fields, id, ',') && std::getline(fields, stars, ',') && std::getline(fields, tier, ',')) {
                try { ratings[std::stoi(id)] = {std::stoi(stars), std::stoi(tier)}; } catch (...) {}
            }
        }
        return ratings;
    }

    void saveFakeRating(int levelID, FakeLevelRating rating) {
        auto ratings = loadFakeRatings();
        ratings[levelID] = rating;
        std::string output;
        for (auto const& [id, value] : ratings) {
            if (!output.empty()) output += ';';
            output += fmt::format("{},{},{}", id, value.stars, value.tier);
        }
        Mod::get()->setSavedValue<std::string>(FAKE_RATINGS_SAVE_KEY, output);
    }

    char const* tierName(int tier) {
        static char const* names[] = {"STANDARD", "FEATURED", "EPIC", "LEGENDARY", "MYTHIC"};
        return names[std::clamp(tier, 0, 4)];
    }

    char const* difficultyName(int stars) {
        if (stars <= 1) return "AUTO";
        if (stars == 2) return "EASY";
        if (stars == 3) return "NORMAL";
        if (stars <= 5) return "HARD";
        if (stars <= 7) return "HARDER";
        if (stars <= 9) return "INSANE";
        return "DEMON";
    }

    ccColor3B accentColor() {
        int hue = (Mod::get()->getSettingValue<int>(PANEL_ACCENT_HUE_SETTING) % 360 + 360) % 360;
        float h = hue / 60.f, x = 1.f - std::abs(std::fmod(h, 2.f) - 1.f);
        float r=0.f, g=0.f, b=0.f;
        if (h < 1.f) { r=1.f; g=x; } else if (h < 2.f) { r=x; g=1.f; }
        else if (h < 3.f) { g=1.f; b=x; } else if (h < 4.f) { g=x; b=1.f; }
        else if (h < 5.f) { r=x; b=1.f; } else { r=1.f; b=x; }
        return ccc3(static_cast<GLubyte>(r*255), static_cast<GLubyte>(g*255), static_cast<GLubyte>(b*255));
    }

    void fadeLayer(CCLayerColor* layer, bool enabled) {
        if (!layer) return;
        layer->stopAllActions();
        layer->setCascadeOpacityEnabled(true);
        if (enabled) {
            layer->setOpacity(0);
            layer->runAction(CCFadeTo::create(.30f, 255));
        } else {
            layer->setOpacity(255);
        }
    }

    void showPanel(CCNode* node) {
        if (!node) return;
        node->stopAllActions();
        node->setVisible(true);
        const bool fade = Mod::get()->getSettingValue<bool>(PANEL_FADE_SETTING);

        // CCLayerColor implements CCRGBAProtocol directly; it is not a
        // CCNodeRGBA. Fade the actual color layer and cascade to its contents.
        if (auto colorLayer = typeinfo_cast<CCLayerColor*>(node)) {
            fadeLayer(colorLayer, fade);
            return;
        }

        // The global shortcut panel is a plain CCLayer containing color layers.
        // Animate those visible layers rather than trying to fade the CCLayer.
        auto children = node->getChildren();
        if (!children) return;
        for (unsigned int i = 0; i < children->count(); ++i) {
            auto child = typeinfo_cast<CCLayerColor*>(children->objectAtIndex(i));
            if (child) fadeLayer(child, fade);
        }
    }

    class MythixFakeRatePopup : public CCLayer {
    int m_levelID = 0;
    int m_stars = 0;
    int m_tier = 0;
    CCLabelBMFont* m_faceLabel = nullptr;
    CCLabelBMFont* m_tierLabel = nullptr;
    CCSprite* m_faceSprite = nullptr;
    CCSprite* m_glowSprite = nullptr;
public:
    static MythixFakeRatePopup* create(int levelID) {
        auto ret = new MythixFakeRatePopup();
        if (ret && ret->initWithLevel(levelID)) { ret->autorelease(); return ret; }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool initWithLevel(int levelID) {
        if (!CCLayer::init()) return false;
        m_levelID = levelID;
        auto saved = loadFakeRatings();
        if (saved.count(levelID)) { m_stars = saved[levelID].stars; m_tier = saved[levelID].tier; }

        auto win = CCDirector::sharedDirector()->getWinSize();
        auto shade = CCLayerColor::create(ccc4(0, 0, 0, 170), win.width, win.height);
        if (shade) this->addChild(shade);

        auto panel = CCLayerColor::create(ccc4(104, 55, 35, 255), 440.f, 300.f);
        if (!panel) return false;
        panel->setPosition({(win.width - 440.f) / 2.f, (win.height - 300.f) / 2.f});
        this->addChild(panel, 1);

        auto title = CCLabelBMFont::create("MYTHIX: SET FAKE RATING", "bigFont.fnt");
        if (title) { title->setScale(.62f); title->setPosition({220.f, 265.f}); panel->addChild(title); }

        auto starsTitle = CCLabelBMFont::create("SELECT STARS", "bigFont.fnt");
        if (starsTitle) { starsTitle->setScale(.48f); starsTitle->setPosition({220.f, 225.f}); panel->addChild(starsTitle); }

        auto numbers = CCMenu::create();
        if (!numbers) return false;
        numbers->setPosition({0.f, 0.f});
        for (int i = 1; i <= 10; ++i) {
            auto label = CCLabelBMFont::create(fmt::format("{}", i).c_str(), "bigFont.fnt");
            if (!label) continue;
            label->setScale(.52f);
            auto item = CCMenuItemLabel::create(label, this, menu_selector(MythixFakeRatePopup::onChooseStars));
            if (!item) continue;
            item->setTag(i);
            float x = 72.f + ((i - 1) % 5) * 74.f;
            float y = 180.f - ((i - 1) / 5) * 55.f;
            item->setPosition({x, y});
            numbers->addChild(item);
        }
        panel->addChild(numbers, 2);

        m_faceLabel = CCLabelBMFont::create("", "bigFont.fnt");
        if (m_faceLabel) { m_faceLabel->setScale(.40f); m_faceLabel->setPosition({220.f, 62.f}); panel->addChild(m_faceLabel); }
        m_tierLabel = CCLabelBMFont::create("", "bigFont.fnt");
        if (m_tierLabel) { m_tierLabel->setScale(.40f); m_tierLabel->setPosition({220.f, 37.f}); panel->addChild(m_tierLabel); }
        m_glowSprite = CCSprite::createWithSpriteFrameName("difficultySelected_001.png");
        if (m_glowSprite) { m_glowSprite->setPosition({220.f, 115.f}); m_glowSprite->setScale(1.75f); m_glowSprite->setOpacity(190); panel->addChild(m_glowSprite, 1); }
        auto faceFrame = CCSprite::createWithSpriteFrameName("difficulty_02_btn_001.png");
        if (faceFrame) {
            faceFrame->setScale(1.15f);
            auto faceMenu = CCMenu::create(CCMenuItemSpriteExtra::create(faceFrame, this, menu_selector(MythixFakeRatePopup::onCycleTier)), nullptr);
            if (faceMenu) { faceMenu->setPosition({0.f, 0.f}); auto item = static_cast<CCMenuItem*>(faceMenu->getChildren()->objectAtIndex(0)); item->setPosition({220.f, 115.f}); panel->addChild(faceMenu, 3); m_faceSprite = faceFrame; }
        }

        auto actions = CCMenu::create();
        if (!actions) return false;
        actions->setPosition({0.f, 0.f});
        auto cancelText = CCLabelBMFont::create("CANCEL", "bigFont.fnt");
        auto submitText = CCLabelBMFont::create("SUBMIT", "bigFont.fnt");
        if (cancelText) {
            cancelText->setScale(.48f);
            auto cancel = CCMenuItemLabel::create(cancelText, this, menu_selector(MythixFakeRatePopup::onCancel));
            if (cancel) { cancel->setPosition({125.f, 15.f}); actions->addChild(cancel); }
        }
        if (submitText) {
            submitText->setScale(.48f);
            auto submit = CCMenuItemLabel::create(submitText, this, menu_selector(MythixFakeRatePopup::onSubmit));
            if (submit) { submit->setPosition({315.f, 15.f}); actions->addChild(submit); }
        }
        panel->addChild(actions, 3);
        refreshPreview();
        return true;
    }

    void refreshPreview() {
        if (m_faceLabel) m_faceLabel->setString(m_stars ? fmt::format("{}  ({} STAR{})", difficultyName(m_stars), m_stars, m_stars == 1 ? "" : "S").c_str() : "SELECT A STAR AMOUNT");
        if (m_faceSprite) {
            int diff = m_stars <= 1 ? 0 : m_stars == 2 ? 1 : m_stars == 3 ? 2 : m_stars <= 5 ? 3 : m_stars <= 7 ? 4 : m_stars <= 9 ? 5 : 6;
            auto frame = CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(fmt::format("difficulty_{:02}_btn_001.png", diff).c_str());
            if (frame) m_faceSprite->setDisplayFrame(frame);
        }
        if (m_tierLabel) {
            m_tierLabel->setString(tierName(m_tier));
            static ccColor3B colors[] = {ccc3(255,255,255), ccc3(255,205,65), ccc3(190,105,255), ccc3(255,105,220), ccc3(85,225,255)};
            m_tierLabel->setColor(colors[std::clamp(m_tier, 0, 4)]);
            if (m_glowSprite) {
                m_glowSprite->setColor(colors[std::clamp(m_tier, 0, 4)]);
                m_glowSprite->setOpacity(m_tier == 0 ? 0 : 220);
            }
        }
    }
    void onChooseStars(CCObject* sender) {
        auto item = typeinfo_cast<CCMenuItem*>(sender);
        if (!item) return;
        m_stars = item->getTag();
        refreshPreview();
    }
    void onCycleTier(CCObject*) { m_tier = (m_tier + 1) % 5; refreshPreview(); }
    void onCancel(CCObject*) { this->removeFromParentAndCleanup(true); }
    void onSubmit(CCObject*) {
        if (m_stars > 0) saveFakeRating(m_levelID, {m_stars, m_tier});
        this->removeFromParentAndCleanup(true);
    }
};

class BunnyIcon : public CCNode {
    public:
        static BunnyIcon* create() {
            auto ret = new BunnyIcon();
            if (ret && ret->init()) {
                ret->autorelease();
                return ret;
            }
            CC_SAFE_DELETE(ret);
            return nullptr;
        }

        bool init() override {
            if (!CCNode::init()) return false;
            this->setContentSize({48.f, 48.f});
            // The sprite is registered in mod.json and Geode prefixes it with this mod's ID.
            auto bunny = CCSprite::create("mythix-bunny.png"_spr);
            if (!bunny) {
                log::error("Failed to load Mythix bunny sprite resource");
                return false;
            }
            auto size = bunny->getContentSize();
            if (size.width <= 0.f || size.height <= 0.f) return false;
            // Leave a generous inset so the bunny stays well inside the circular button.
            float fit = std::min(36.f / size.width, 36.f / size.height);
            bunny->setScale(fit);
            bunny->setPosition({24.f, 24.f});
            this->addChild(bunny);
            return true;
        }
    };

    CCLabelBMFont* makeToggleLabel(const char* text) {
        auto label = CCLabelBMFont::create(text, "bigFont.fnt");
        if (label) {
            label->setScale(0.48f);
            label->setColor(accentColor());
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
        if (m_uiLayer) {
            auto switcher = m_uiLayer->getChildByID("mythix-startpos-switcher");
            auto label = switcher ? typeinfo_cast<CCLabelBMFont*>(switcher->getChildByID("mythix-startpos-count")) : nullptr;
            if (label)
                label->setString(fmt::format("{}/{}", m_fields->selectedStartPos, m_fields->startPositions.size()).c_str());
            if (switcher)
                switcher->setVisible(!m_fields->startPositions.empty());
        }
    }

    void switchStartPos(int direction) {
        int count = static_cast<int>(m_fields->startPositions.size());
        if (count <= 0) return;
        int next = (m_fields->selectedStartPos + direction) % (count + 1);
        if (next < 0) next += count + 1;
        m_fields->selectedStartPos = next;
        // Geode 6 / GD 2.2091 no longer exposes m_currentCheckpoint or
        // setStartPosObject(). The selected start object remains available
        // through PlayLayer's m_startPosObject field.
        m_isTestMode = next != 0;
        updateTestModeLabel();
        m_startPosObject = next == 0 ? nullptr : m_fields->startPositions[next - 1];
        if (m_isPracticeMode) resetLevelFromStart();
        resetLevel();
        startMusic();

        // Keep the on-screen counter synchronized after keyboard and touch input.
        if (m_uiLayer) {
            auto switcher = m_uiLayer->getChildByID("mythix-startpos-switcher");
            auto label = switcher ? typeinfo_cast<CCLabelBMFont*>(switcher->getChildByID("mythix-startpos-count")) : nullptr;
            if (label)
                label->setString(fmt::format("{}/{}", m_fields->selectedStartPos, m_fields->startPositions.size()).c_str());
        }
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
            if (existing->isVisible()) existing->setVisible(false); else showPanel(existing);
            return;
        }
        auto panel = new MythixGlobalPanel();
        if (panel && panel->init()) {
            panel->setID("mythix-global-panel");
            scene->addChild(panel, 100000);
            panel->autorelease();
            showPanel(panel);
        } else {
            CC_SAFE_DELETE(panel);
        }
    }

    bool init() override {
        if (!CCLayer::init()) return false;
        auto win = CCDirector::sharedDirector()->getWinSize();
        auto backdrop = CCLayerColor::create(ccc4(0, 0, 0, 90), win.width, win.height);
        if (backdrop) this->addChild(backdrop);

        auto panel = CCLayerColor::create(ccc4(19, 11, 13, 248), 320.f, 250.f);
        if (!panel) return false;
        panel->setPosition({(win.width - 300.f) / 2.f, (win.height - 190.f) / 2.f});
        this->addChild(panel);

        auto header = CCLayerColor::create(ccc4(72, 27, 19, 255), 320.f, 38.f);
        header->setPosition({0.f, 152.f});
        panel->addChild(header);
        auto title = CCLabelBMFont::create("MYTHIX UTILITIES", "bigFont.fnt");
        if (title) { title->setScale(0.62f); title->setColor(accentColor()); title->setPosition({150.f,19.f}); header->addChild(title); }

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
            m_noclipLabel->setColor(accentColor());
        }
    }
    void onParticles(CCObject*) {
        bool value = !Mod::get()->getSettingValue<bool>(NO_PARTICLES_SETTING);
        Mod::get()->setSettingValue<bool>(NO_PARTICLES_SETTING, value);
        if (m_particlesLabel) {
            m_particlesLabel->setString(value ? "[X]  NO PARTICLES" : "[ ]  NO PARTICLES");
            m_particlesLabel->setColor(accentColor());
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
        m_fields->noclipLabel->setColor(accentColor());
    }

    void refreshParticlesLabel() {
        if (!m_fields->particlesLabel)
            return;
        bool enabled = Mod::get()->getSettingValue<bool>(NO_PARTICLES_SETTING);
        m_fields->particlesLabel->setString(enabled ? "[X]  NO PARTICLES" : "[ ]  NO PARTICLES");
        m_fields->particlesLabel->setColor(accentColor());
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

    void onOpenStartPosSettings(CCObject*) {
        openSettingsPopup(Mod::get());
    }

    void onToggleAccentSlider(CCObject*) {
        if (!m_fields->panel) return;
        auto slider = typeinfo_cast<CCControlSlider*>(m_fields->panel->getChildByTag(771));
        if (slider) slider->setVisible(!slider->isVisible());
    }

    void onAccentHueChanged(CCObject* sender, CCControlEvent) {
        auto slider = typeinfo_cast<CCControlSlider*>(sender);
        if (!slider) return;
        Mod::get()->setSettingValue<int>(PANEL_ACCENT_HUE_SETTING, static_cast<int>(std::round(slider->getValue())));
        auto color = accentColor();
        if (m_fields->noclipLabel) m_fields->noclipLabel->setColor(color);
        if (m_fields->particlesLabel) m_fields->particlesLabel->setColor(color);
        auto top = m_fields->panel ? m_fields->panel->getChildByTag(770) : nullptr;
        auto title = top ? typeinfo_cast<CCLabelBMFont*>(top->getChildByTag(772)) : nullptr;
        if (title) title->setColor(color);
        auto accent = m_fields->panel ? typeinfo_cast<CCLabelBMFont*>(m_fields->panel->getChildByTag(773)) : nullptr;
        if (accent) accent->setColor(color);
    }

    void onCloseUtilities(CCObject*) {
        if (m_fields->panel)
            m_fields->panel->setVisible(false);
    }

    void onOpenUtilities(CCObject*) {
        if (m_fields->panel) {
            refreshNoclipLabel();
            refreshParticlesLabel();
            showPanel(m_fields->panel);
        }
    }

    void createUtilitiesPanel() {
        if (m_fields->panel)
            return;

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto panel = CCLayerColor::create(ccc4(19, 11, 13, 248), 340.f, 240.f);
        if (!panel)
            return;

        panel->setPosition({
            (winSize.width - 340.f) / 2.f,
            (winSize.height - 240.f) / 2.f
        });
        panel->setZOrder(10000);
        this->addChild(panel);
        m_fields->panel = panel;

        auto top = CCLayerColor::create(ccc4(72, 27, 19, 255), 340.f, 38.f);
        top->setTag(770);
        top->setPosition({0.f, 202.f});
        panel->addChild(top);

        auto title = CCLabelBMFont::create("MYTHIX UTILITIES", "bigFont.fnt");
        if (title) {
            title->setScale(0.62f);
            title->setColor(accentColor());
            title->setTag(772);
            title->setPosition({170.f, 19.f});
            top->addChild(title);
        }

        m_fields->noclipLabel = makeToggleLabel("[ ]  NOCLIP");
        m_fields->particlesLabel = makeToggleLabel("[ ]  NO PARTICLES");

        auto prevKey = CCLabelBMFont::create("START POS KEYBINDS", "bigFont.fnt");
        auto nextKey = CCLabelBMFont::create("Previous: F     Next: G (defaults)", "bigFont.fnt");
        auto keyHint = CCLabelBMFont::create("Use the gear to customize both keys", "chatFont.fnt");
        if (prevKey) { prevKey->setScale(.34f); prevKey->setColor(accentColor()); prevKey->setPosition({170.f, 25.f}); panel->addChild(prevKey); }
        if (nextKey) { nextKey->setScale(.34f); nextKey->setColor(accentColor()); nextKey->setPosition({170.f, 10.f}); panel->addChild(nextKey); }
        if (keyHint) { keyHint->setScale(.30f); keyHint->setColor(accentColor()); keyHint->setPosition({170.f, -5.f}); panel->addChild(keyHint); }

        auto gearSprite = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
        if (gearSprite) {
            gearSprite->setScale(.42f);
            auto gearButton = CCMenuItemSpriteExtra::create(gearSprite, this, menu_selector(MythixPauseLayer::onOpenStartPosSettings));
            gearButton->setPosition({315.f, 25.f});
            auto gearMenu = CCMenu::create(gearButton, nullptr);
            if (gearMenu) { gearMenu->setPosition({0.f,0.f}); panel->addChild(gearMenu, 2); }
        }

        auto noclipButton = CCMenuItemLabel::create(
            m_fields->noclipLabel, this, menu_selector(MythixPauseLayer::onToggleNoclip)
        );
        auto particlesButton = CCMenuItemLabel::create(
            m_fields->particlesLabel, this, menu_selector(MythixPauseLayer::onToggleParticles)
        );


        if (noclipButton)
            noclipButton->setPosition({170.f, 162.f});
        if (particlesButton)
            particlesButton->setPosition({170.f, 122.f});

        auto closeLabel = CCLabelBMFont::create("CLOSE", "bigFont.fnt");
        if (closeLabel) {
            closeLabel->setScale(0.42f);
            closeLabel->setColor(ccc3(255, 174, 104));
        }
        auto closeButton = closeLabel
            ? CCMenuItemLabel::create(closeLabel, this, menu_selector(MythixPauseLayer::onCloseUtilities))
            : nullptr;
        if (closeButton)
            closeButton->setPosition({305.f, 220.f});

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

        auto accentLabel = CCLabelBMFont::create("ACCENT COLOR", "bigFont.fnt");
        if (accentLabel) {
            accentLabel->setScale(.40f); accentLabel->setColor(accentColor()); accentLabel->setTag(773);
            auto accentButton = CCMenuItemLabel::create(accentLabel, this, menu_selector(MythixPauseLayer::onToggleAccentSlider));
            if (accentButton) { accentButton->setPosition({170.f, 82.f}); menu->addChild(accentButton); }
        }
        auto slider = CCControlSlider::create("slidergroove.png", "sliderprogress.png", "sliderthumb.png");
        if (slider) {
            slider->setPosition({170.f, 52.f}); slider->setMinimumValue(0.f); slider->setMaximumValue(359.f);
            slider->setValue(static_cast<float>(Mod::get()->getSettingValue<int>(PANEL_ACCENT_HUE_SETTING)));
            slider->setTag(771); slider->setVisible(false);
            slider->addTargetWithActionForControlEvents(this, cccontrol_selector(MythixPauseLayer::onAccentHueChanged), CCControlEventValueChanged);
            panel->addChild(slider, 3);
        } else log::error("Could not create Mythix accent color slider");
        panel->setVisible(false);
    }

    void onMythixUtilities(CCObject*) {
        createUtilitiesPanel();
        if (m_fields->panel) {
            refreshNoclipLabel();
            refreshParticlesLabel();
            showPanel(m_fields->panel);
        }
    }

    void customSetup() {
        PauseLayer::customSetup();

        createUtilitiesPanel();

        auto icon = BunnyIcon::create();
        if (!icon)
            return;

        auto buttonSprite = CircleButtonSprite::create(
            icon, CircleBaseColor::Green, CircleBaseSize::MediumAlt
        );
        if (!buttonSprite)
            return;
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
        if (!typeinfo_cast<PlayLayer*>(baseGame)) return true;
        auto menu = CCMenu::create();
        if (!menu) return true;
        menu->setID("mythix-startpos-switcher");
        auto win = CCDirector::sharedDirector()->getWinSize();
        menu->setPosition({win.width / 2.f, 42.f});
        this->addChild(menu, 1000);

        auto prevText = CCLabelBMFont::create("<", "bigFont.fnt");
        auto nextText = CCLabelBMFont::create(">", "bigFont.fnt");
        auto gearSprite = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
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
        if (gearSprite) {
            gearSprite->setScale(.55f);
            auto item=CCMenuItemSpriteExtra::create(gearSprite,this,menu_selector(MythixUILayer::onSettings));
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


class $modify(MythixLevelInfoLayer, LevelInfoLayer) {
    void setupLevelInfo() {
        LevelInfoLayer::setupLevelInfo();
        if (!Mod::get()->getSettingValue<bool>(RATE_LEVELS_SETTING) || !m_level) return;
        if (this->getChildByID("mythix-fake-rate-button")) return;

        auto label = CCLabelBMFont::create("★ RATE", "bigFont.fnt");
        if (!label) return;
        label->setScale(.42f);
        label->setColor(ccc3(255, 220, 90));
        auto item = CCMenuItemLabel::create(label, this, menu_selector(MythixLevelInfoLayer::onMythixFakeRate));
        if (!item) return;
        item->setID("mythix-fake-rate-button");
        auto menu = CCMenu::create(item, nullptr);
        if (!menu) return;
        auto win = CCDirector::sharedDirector()->getWinSize();
        menu->setPosition({win.width - 95.f, 75.f});
        this->addChild(menu, 1000);
    }

    void onMythixFakeRate(CCObject*) {
        if (!m_level) return;
        auto popup = MythixFakeRatePopup::create(static_cast<int>(m_level->m_levelID));
        if (popup) this->addChild(popup, 10000);
    }
};
