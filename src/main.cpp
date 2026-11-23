#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCParticleSystemQuad.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace {
    constexpr char const* NOCLIP_SETTING = "noclip";
    constexpr char const* NO_PARTICLES_SETTING = "no-particles";

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



namespace {
    struct BotHazard {
        float left;
        float right;
    };

    struct BotOrb {
        GameObject* object;
        bool activated;
    };

    // Geometry Dash object IDs for automatic pads and click-activated orbs.
    // Keep these separate from lethal hazards: pads activate on contact,
    // while orbs require a one-frame player input during contact.
    static bool isBotPad(int id) {
        switch (id) {
            case 35:   // Yellow jump pad
            case 67:   // Blue gravity pad
            case 140:  // Pink jump pad
            case 1332: // Red jump pad
            case 3005: // Spider pad
                return true;
            default:
                return false;
        }
    }

    static bool isBotOrb(int id) {
        switch (id) {
            case 36:   // Yellow jump orb
            case 84:   // Blue gravity orb
            case 141:  // Pink jump orb
            case 1022: // Green gravity orb
            case 1330: // Black drop orb
            case 1333: // Red jump orb
            case 3004: // Spider orb
                return true;
            default:
                return false;
        }
    }

    // First controller revision with explicit pad/orb classification. It uses
    // live player geometry and native input; it does not mutate a second world.
    class MythixBotController {
    public:
        static bool start(PlayLayer* layer) {
            stop(layer, "Preparing bot...");
            if (!layer || !layer->m_player1 || !layer->m_objects) {
                s_status = "Enter a level first";
                return false;
            }

            s_hazards.clear();
            s_orbs.clear();
            for (unsigned int i = 0; i < layer->m_objects->count(); ++i) {
                auto object = typeinfo_cast<GameObject*>(layer->m_objects->objectAtIndex(i));
                if (!object || object->m_isDisabled)
                    continue;

                int objectID = object->m_objectID;
                if (isBotPad(objectID))
                    continue; // Pads are automatic; never jump just because one is nearby.

                if (isBotOrb(objectID)) {
                    s_orbs.push_back({object, false});
                    continue;
                }

                if (object->m_objectType != GameObjectType::Hazard)
                    continue;

                auto const& rect = object->getObjectRect();
                s_hazards.push_back({rect.origin.x, rect.origin.x + rect.size.width});
            }
            std::sort(s_hazards.begin(), s_hazards.end(), [](BotHazard const& a, BotHazard const& b) {
                return a.left < b.left;
            });

            s_enabled = true;
            s_buttonHeld = false;
            s_status = "Scanned " + std::to_string(s_hazards.size()) + " hazards, " +
                std::to_string(s_orbs.size()) + " orbs - running";
            return true;
        }

        static void stop(PlayLayer* layer, std::string const& message = "Bot stopped") {
            if (s_buttonHeld && layer)
                layer->handleButton(false, static_cast<int>(PlayerButton::Jump), true);
            s_buttonHeld = false;
            s_enabled = false;
            s_status = message;
        }

        static bool enabled() { return s_enabled; }
        static std::string const& status() { return s_status; }

        static void update(PlayLayer* layer) {
            if (!s_enabled || !layer)
                return;

            // A one-frame press is enough for a normal cube jump. Always release
            // before deciding whether another jump is needed.
            if (s_buttonHeld) {
                layer->handleButton(false, static_cast<int>(PlayerButton::Jump), true);
                s_buttonHeld = false;
            }

            auto player = layer->m_player1;
            if (!player) {
                stop(layer, "Player unavailable - stopped");
                return;
            }
            if (player->m_isDead) {
                stop(layer, "Died - bot stopped");
                return;
            }

            bool cubeMode = !player->m_isShip && !player->m_isBall && !player->m_isBird &&
                !player->m_isDart && !player->m_isRobot && !player->m_isSpider && !player->m_isSwing;
            if (!cubeMode) {
                stop(layer, "Unsupported mode: cube bot only");
                return;
            }

            auto const& playerRect = player->getObjectRect();
            float playerLeft = playerRect.origin.x;
            float playerRight = playerLeft + playerRect.size.width;
            float playerBottom = playerRect.origin.y;
            float playerTop = playerBottom + playerRect.size.height;

            // Orbs are optional and only activate on a click while the player
            // overlaps their activation area. Check them even while airborne.
            // Each placed orb is tapped at most once per bot run.
            for (auto& orb : s_orbs) {
                if (orb.activated || !orb.object || orb.object->m_isDisabled)
                    continue;

                auto const& orbRect = orb.object->getObjectRect();
                bool overlapsOrb =
                    playerRight >= orbRect.origin.x - 3.f &&
                    playerLeft <= orbRect.origin.x + orbRect.size.width + 3.f &&
                    playerTop >= orbRect.origin.y - 3.f &&
                    playerBottom <= orbRect.origin.y + orbRect.size.height + 3.f;

                if (overlapsOrb) {
                    layer->handleButton(true, static_cast<int>(PlayerButton::Jump), true);
                    s_buttonHeld = true;
                    orb.activated = true;
                    s_status = "Activated orb " + std::to_string(orb.object->m_objectID);
                    return;
                }
            }

            // Pads activate automatically when touched. The cube jump planner
            // should only evaluate normal ground jumps when the player is grounded.
            if (!player->m_isOnGround || s_hazards.empty())
                return;
            BotHazard const* next = nullptr;
            for (auto const& hazard : s_hazards) {
                if (hazard.right >= playerRight - 12.f) {
                    next = &hazard;
                    break;
                }
            }
            if (!next)
                return;

            float speed = static_cast<float>(std::fabs(player->getCurrentXVelocity()));
            if (speed < 1.f)
                speed = std::max(1.f, player->m_playerSpeed);
            if (speed < 1.f)
                speed = 300.f;

            // Estimate the width of a contiguous spike/saw cluster so doubles
            // and triples receive an earlier takeoff than a single obstacle.
            float clusterRight = next->right;
            for (auto const& hazard : s_hazards) {
                if (hazard.left <= next->left)
                    continue;
                if (hazard.left > clusterRight + 28.f)
                    break;
                clusterRight = std::max(clusterRight, hazard.right);
            }
            float clusterWidth = clusterRight - next->left;
            float triggerDistance = std::clamp(
                speed * 0.27f + std::max(0.f, clusterWidth - 30.f) * 0.35f,
                65.f, 135.f
            );
            float distance = next->left - playerRight;
            if (distance <= triggerDistance && distance >= -20.f) {
                layer->handleButton(true, static_cast<int>(PlayerButton::Jump), true);
                s_buttonHeld = true;
            }
        }

    private:
        inline static bool s_enabled = false;
        inline static bool s_buttonHeld = false;
        inline static std::string s_status = "Ready - enter a level";
        inline static std::vector<BotHazard> s_hazards;
        inline static std::vector<BotOrb> s_orbs;
    };
}

class $modify(MythixPlayLayer, PlayLayer) {
    // Preserve the existing Mythix noclip behavior.
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (Mod::get()->getSettingValue<bool>(NOCLIP_SETTING))
            return;

        PlayLayer::destroyPlayer(player, object);
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        MythixBotController::update(this);
    }
};

// Suppress Cocos particle-system rendering while a level is active.
// This catches particle systems created by level objects and player effects.
class $modify(MythixParticleSystem, CCParticleSystemQuad) {
    void draw() {
        if (PlayLayer::get() && Mod::get()->getSettingValue<bool>(NO_PARTICLES_SETTING))
            return;

        CCParticleSystemQuad::draw();
    }
};

class $modify(MythixPauseLayer, PauseLayer) {
    struct Fields {
        CCLayerColor* panel = nullptr;
        CCLabelBMFont* noclipLabel = nullptr;
        CCLabelBMFont* particlesLabel = nullptr;
        CCLabelBMFont* botLabel = nullptr;
        CCLabelBMFont* botStatusLabel = nullptr;
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

    void refreshBotLabel() {
        if (m_fields->botLabel) {
            bool enabled = MythixBotController::enabled();
            m_fields->botLabel->setString(enabled ? "[X]  BOT LEVEL" : "[ ]  BOT LEVEL");
            m_fields->botLabel->setColor(enabled ? ccc3(255, 125, 55) : ccc3(255, 205, 160));
        }
        if (m_fields->botStatusLabel)
            m_fields->botStatusLabel->setString(MythixBotController::status().c_str());
    }

    void onToggleBot(CCObject*) {
        auto layer = PlayLayer::get();
        if (MythixBotController::enabled())
            MythixBotController::stop(layer);
        else
            MythixBotController::start(layer);
        refreshBotLabel();
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
            refreshBotLabel();
            m_fields->panel->setVisible(true);
        }
    }

    void createUtilitiesPanel() {
        if (m_fields->panel)
            return;

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto panel = CCLayerColor::create(ccc4(19, 11, 13, 248), 300.f, 230.f);
        if (!panel)
            return;

        panel->setPosition({
            (winSize.width - 300.f) / 2.f,
            (winSize.height - 230.f) / 2.f
        });
        panel->setZOrder(10000);
        this->addChild(panel);
        m_fields->panel = panel;

        auto top = CCLayerColor::create(ccc4(72, 27, 19, 255), 300.f, 38.f);
        top->setPosition({0.f, 192.f});
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

        m_fields->botLabel = makeToggleLabel("[ ]  BOT LEVEL");
        auto botButton = CCMenuItemLabel::create(
            m_fields->botLabel, this, menu_selector(MythixPauseLayer::onToggleBot)
        );
        m_fields->botStatusLabel = CCLabelBMFont::create("Ready - enter a level", "bigFont.fnt");
        if (m_fields->botStatusLabel) {
            m_fields->botStatusLabel->setScale(0.30f);
            m_fields->botStatusLabel->setColor(ccc3(210, 185, 165));
            m_fields->botStatusLabel->setPosition({150.f, 53.f});
            panel->addChild(m_fields->botStatusLabel);
        }

        if (noclipButton)
            noclipButton->setPosition({150.f, 153.f});
        if (particlesButton)
            particlesButton->setPosition({150.f, 119.f});
        if (botButton)
            botButton->setPosition({150.f, 85.f});

        auto closeLabel = CCLabelBMFont::create("CLOSE", "bigFont.fnt");
        if (closeLabel) {
            closeLabel->setScale(0.42f);
            closeLabel->setColor(ccc3(255, 174, 104));
        }
        auto closeButton = closeLabel
            ? CCMenuItemLabel::create(closeLabel, this, menu_selector(MythixPauseLayer::onCloseUtilities))
            : nullptr;
        if (closeButton)
            closeButton->setPosition({150.f, 23.f});

        auto menu = CCMenu::create();
        if (menu) {
            menu->setPosition({0.f, 0.f});
            if (noclipButton) menu->addChild(noclipButton);
            if (particlesButton) menu->addChild(particlesButton);
            if (botButton) menu->addChild(botButton);
            if (closeButton) menu->addChild(closeButton);
            panel->addChild(menu);
        }

        refreshNoclipLabel();
        refreshParticlesLabel();
        refreshBotLabel();
        panel->setVisible(false);
    }

    void onMythixUtilities(CCObject*) {
        createUtilitiesPanel();
        if (m_fields->panel) {
            refreshNoclipLabel();
            refreshParticlesLabel();
            refreshBotLabel();
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
