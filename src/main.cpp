#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCParticleSystemQuad.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

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
        float bottom;
        float top;
    };

    struct BotSolid {
        float left;
        float right;
        float bottom;
        float top;
    };

    struct BotOrb {
        GameObject* object;
        bool activated;
        int cooldownFrames;
    };

    struct BotPad {
        float left;
        float right;
    };

    struct BotPrediction {
        bool safe = true;
        bool landed = false;
        int collisionObject = 0;
        int frames = 0;
        float landingX = 0.f;
    };

    static bool isBotPad(int id) {
        switch (id) {
            case 35:
            case 67:
            case 140:
            case 1332:
            case 3005:
                return true;
            default:
                return false;
        }
    }

    static bool isBotOrb(int id) {
        switch (id) {
            case 36:
            case 84:
            case 141:
            case 1022:
            case 1330:
            case 1333:
            case 3004:
                return true;
            default:
                return false;
        }
    }

    class MythixBotController {
    public:
        static bool start(PlayLayer* layer) {
            stop(layer, "Preparing live analysis...");
            if (!layer || !layer->m_player1 || !layer->m_objects) {
                s_status = "Enter a level first";
                return false;
            }

            s_hazards.clear();
            s_solids.clear();
            s_orbs.clear();
            s_pads.clear();
            s_hasPreviousPlayerSample = false;
            scanLevel(layer);

            s_enabled = true;
            s_buttonHeld = false;
            s_status = "LIVE SCAN active - cube physics calibrated";
            return true;
        }

        static void stop(PlayLayer* layer, std::string const& message = "Bot stopped") {
            if (s_buttonHeld && layer)
                layer->handleButton(false, static_cast<int>(PlayerButton::Jump), true);
            s_buttonHeld = false;
            s_enabled = false;
            s_hasPreviousPlayerSample = false;
            s_status = message;
        }

        static bool enabled() { return s_enabled; }
        static std::string const& status() { return s_status; }

        static void update(PlayLayer* layer) {
            if (!s_enabled || !layer)
                return;

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

            // Rebuild the scene from the live object array every update. This
            // keeps moving objects, disabled objects, and trigger changes fresh.
            scanLevel(layer);

            auto const& playerRect = player->getObjectRect();
            float playerLeft = playerRect.origin.x;
            float playerBottom = playerRect.origin.y;
            float playerRight = playerLeft + playerRect.size.width;
            float playerTop = playerBottom + playerRect.size.height;
            float playerX = player->getPositionX();
            float playerY = player->getPositionY();

            // Estimate the physics time scale from actual horizontal movement
            // divided by the game's current horizontal velocity. This avoids
            // assuming a fixed frame rate or hard-coding a jump duration.
            double currentVelocityX = std::fabs(player->getCurrentXVelocity());
            float observedStepX = 0.f;
            if (s_hasPreviousPlayerSample && currentVelocityX > 0.25)
                observedStepX = std::fabs(playerX - s_lastPlayerX);
            s_lastPlayerX = playerX;
            s_lastPlayerY = playerY;
            s_hasPreviousPlayerSample = true;

            // Orb input is still a one-frame tap, but live scanning preserves
            // each orb's cooldown/consumed state across scene refreshes.
            for (auto& orb : s_orbs) {
                if (orb.cooldownFrames > 0)
                    --orb.cooldownFrames;
                if (orb.activated || !orb.object || orb.object->m_isDisabled)
                    continue;

                auto const& orbRect = orb.object->getObjectRect();
                float orbLeft = orbRect.origin.x;
                float orbRight = orbLeft + orbRect.size.width;
                float orbBottom = orbRect.origin.y;
                float orbTop = orbBottom + orbRect.size.height;

                if (playerLeft > orbRight + 3.f) {
                    orb.activated = true;
                    continue;
                }

                bool overlapsOrb =
                    playerRight >= orbLeft &&
                    playerLeft <= orbRight &&
                    playerTop >= orbBottom &&
                    playerBottom <= orbTop;

                if (overlapsOrb && orb.cooldownFrames == 0) {
                    layer->handleButton(true, static_cast<int>(PlayerButton::Jump), true);
                    s_buttonHeld = true;
                    orb.cooldownFrames = 10;
                    s_status = "ORB CONTACT - one-frame input sent";
                    return;
                }
            }

            float speedX = static_cast<float>(currentVelocityX);
            if (speedX < 0.5f)
                speedX = std::max(0.5f, player->m_playerSpeed);
            // Use actual horizontal displacement from the previous game update.
            float stepX = observedStepX > 0.05f ? observedStepX : speedX / 60.f;
            stepX = std::clamp(stepX, 0.5f, 16.f);

            float gravity = static_cast<float>(std::fabs(player->m_gravity));
            if (gravity < 0.05f || !std::isfinite(gravity))
                gravity = 0.9582f;

            float jumpVelocity = static_cast<float>(std::fabs(player->m_yStart));
            if (jumpVelocity < 1.f || !std::isfinite(jumpVelocity))
                jumpVelocity = 11.18f;

            // Mini players have a reduced launch impulse in the game physics.
            if (player->getScale() < 0.9f)
                jumpVelocity *= 0.8f;

            float gravityDirection = player->m_isUpsideDown ? -1.f : 1.f;
            float liveYVelocity = static_cast<float>(player->getYVelocity());

            if (!player->m_isOnGround) {
                auto airborne = simulate(
                    playerLeft, playerBottom, playerRect.size.width, playerRect.size.height,
                    liveYVelocity, false, -1, stepX, gravity, jumpVelocity,
                    gravityDirection, 90
                );
                if (airborne.safe) {
                    s_status = "AIRBORNE - landing path clear (" +
                        std::to_string(airborne.frames) + "f)";
                }
                else {
                    s_status = "AIRBORNE - predicted collision ahead";
                }
                return;
            }

            // First ask whether continuing without input remains safe. If so,
            // wait and re-evaluate next update rather than jumping early.
            auto coast = simulate(
                playerLeft, playerBottom, playerRect.size.width, playerRect.size.height,
                0.f, true, -1, stepX, gravity, jumpVelocity,
                gravityDirection, 90
            );

            if (coast.safe) {
                s_status = "LIVE SCAN: clear - waiting for next obstacle";
                return;
            }

            // Search actual jump timings. The latest safe timing is selected,
            // which avoids the old fixed-distance guess that launched too early
            // or too late. Every candidate is checked against hazards AND solid
            // block sides/undersides, not only spike objects.
            int bestDelay = -1;
            BotPrediction bestPrediction;
            for (int delay = 0; delay <= 20; ++delay) {
                auto candidate = simulate(
                    playerLeft, playerBottom, playerRect.size.width, playerRect.size.height,
                    0.f, true, delay, stepX, gravity, jumpVelocity,
                    gravityDirection, 60
                );
                if (candidate.safe) {
                    bestDelay = delay;
                    bestPrediction = candidate;
                }
            }

            // Never launch a manual jump immediately before a nearby automatic pad.
            // Only defer when the pad is the next relevant event before the nearest hazard.
            float nearestHazardX = std::numeric_limits<float>::max();
            for (auto const& hazard : s_hazards) {
                if (hazard.right >= playerLeft && hazard.left > playerRight)
                    nearestHazardX = std::min(nearestHazardX, hazard.left);
            }
            float nearestPadX = std::numeric_limits<float>::max();
            for (auto const& pad : s_pads) {
                if (pad.right >= playerLeft && pad.left > playerRight)
                    nearestPadX = std::min(nearestPadX, pad.left);
            }
            if (nearestPadX < nearestHazardX &&
                nearestPadX - playerRight <= stepX * 24.f) {
                s_status = "PAD APPROACH - waiting for automatic bounce";
                return;
            }

            if (bestDelay == 0) {
                layer->handleButton(true, static_cast<int>(PlayerButton::Jump), true);
                s_buttonHeld = true;
                s_status = "JUMP NOW - trajectory safe, landing x " +
                    std::to_string(static_cast<int>(bestPrediction.landingX));
            }
            else if (bestDelay > 0) {
                s_status = "JUMP PREDICTION: safe in " + std::to_string(bestDelay) +
                    "f; landing x " + std::to_string(static_cast<int>(bestPrediction.landingX));
            }
            else {
                s_status = "NO SAFE JUMP - H:" + std::to_string(s_hazards.size()) +
                    " S:" + std::to_string(s_solids.size()) +
                    " P:" + std::to_string(s_pads.size()) +
                    (player->m_isOnGround ? " grounded" : " airborne");
            }
        }

    private:
        static BotPrediction simulate(
            float startLeft, float startBottom, float width, float height,
            float initialVelocityY, bool initiallyGrounded, int jumpDelay,
            float stepX, float gravity, float jumpVelocity,
            float gravityDirection, int maxFrames
        ) {
            BotPrediction result;
            float x = startLeft;
            float y = startBottom;
            float vy = initialVelocityY;
            bool grounded = initiallyGrounded;
            bool jumped = false;
            for (int frame = 0; frame < maxFrames; ++frame) {
                if (!jumped && jumpDelay >= 0 && frame == jumpDelay && grounded) {
                    vy = jumpVelocity * gravityDirection;
                    grounded = false;
                    jumped = true;
                }

                float oldBottom = y;
                float oldTop = y + height;
                x += stepX;

                // A landing is not permanent support. Re-check the platform
                // under the player's feet (or head when upside down) every
                // simulated frame so walking off a pillar starts a real fall.
                if (grounded) {
                    bool supported = false;
                    for (auto const& solid : s_solids) {
                        bool horizontalSupport = solid.right > x + 1.f && solid.left < x + width - 1.f;
                        bool verticalSupport = gravityDirection > 0.f
                            ? std::fabs(y - solid.top) <= 2.5f
                            : std::fabs((y + height) - solid.bottom) <= 2.5f;
                        if (horizontalSupport && verticalSupport) {
                            supported = true;
                            break;
                        }
                    }
                    if (!supported)
                        grounded = false;
                }

                if (!grounded && !(jumped && frame == jumpDelay)) {
                    // Gravity and launch velocity are per-physics-step values.
                    // Applying a second time scale shrank the predicted jump arc.
                    vy -= gravity * gravityDirection;
                    y += vy;
                }

                float newBottom = y;
                float newTop = y + height;
                float playerRight = x + width;

                // Lethal objects are checked before landing surfaces.
                for (auto const& hazard : s_hazards) {
                    if (hazard.right < x || hazard.left > playerRight ||
                        hazard.top < newBottom || hazard.bottom > newTop)
                        continue;
                    result.safe = false;
                    result.collisionObject = -1;
                    result.frames = frame + 1;
                    return result;
                }

                bool landedThisFrame = false;
                for (auto const& solid : s_solids) {
                    if (solid.right <= x || solid.left >= playerRight)
                        continue;

                    bool verticalOverlap = newTop > solid.bottom && newBottom < solid.top;
                    if (!verticalOverlap)
                        continue;

                    bool crossedTop = gravityDirection > 0.f &&
                        vy <= 0.f && oldBottom >= solid.top - 2.f && newBottom <= solid.top + 2.f;
                    bool crossedUnderside = gravityDirection < 0.f &&
                        vy >= 0.f && oldTop <= solid.bottom + 2.f && newTop >= solid.bottom - 2.f;

                    if (crossedTop || crossedUnderside) {
                        if (crossedTop)
                            y = solid.top;
                        else
                            y = solid.bottom - height;
                        vy = 0.f;
                        grounded = true;
                        landedThisFrame = true;
                        result.landed = true;
                        result.landingX = x;
                        break;
                    }

                    // A solid block intersecting the side or the player's head
                    // is not a valid landing and must be treated as a collision.
                    result.safe = false;
                    result.collisionObject = 1;
                    result.frames = frame + 1;
                    return result;
                }

                result.frames = frame + 1;
                // Continue the prediction after a landing. The next frames
                // must include walking across the platform, leaving its edge,
                // falling, and any following pillar or hazard. Returning here
                // was the multi-pillar blind spot.
            }

            result.landingX = x;
            return result;
        }

        static void scanLevel(PlayLayer* layer) {
            if (!layer || !layer->m_objects)
                return;

            auto previousOrbs = std::move(s_orbs);
            s_hazards.clear();
            s_solids.clear();
            s_orbs.clear();
            s_pads.clear();

            for (unsigned int i = 0; i < layer->m_objects->count(); ++i) {
                auto object = typeinfo_cast<GameObject*>(layer->m_objects->objectAtIndex(i));
                if (!object || object->m_isDisabled || !object->isVisible())
                    continue;

                int objectID = object->m_objectID;
                auto const& rect = object->getObjectRect();

                if (isBotPad(objectID)) {
                    s_pads.push_back({rect.origin.x, rect.origin.x + rect.size.width});
                    continue;
                }

                if (isBotOrb(objectID)) {
                    auto old = std::find_if(previousOrbs.begin(), previousOrbs.end(),
                        [object](BotOrb const& entry) { return entry.object == object; });
                    if (old != previousOrbs.end())
                        s_orbs.push_back(*old);
                    else
                        s_orbs.push_back({object, false, 0});
                    continue;
                }

                if (object->m_objectType == GameObjectType::Hazard) {
                    s_hazards.push_back({
                        rect.origin.x, rect.origin.x + rect.size.width,
                        rect.origin.y, rect.origin.y + rect.size.height
                    });
                }
                else if (object->m_objectType == GameObjectType::Solid) {
                    s_solids.push_back({
                        rect.origin.x, rect.origin.x + rect.size.width,
                        rect.origin.y, rect.origin.y + rect.size.height
                    });
                }
            }

            std::sort(s_hazards.begin(), s_hazards.end(),
                [](BotHazard const& a, BotHazard const& b) { return a.left < b.left; });
            std::sort(s_solids.begin(), s_solids.end(),
                [](BotSolid const& a, BotSolid const& b) { return a.left < b.left; });
        }

        inline static bool s_enabled = false;
        inline static bool s_buttonHeld = false;
        inline static bool s_hasPreviousPlayerSample = false;
        inline static float s_lastPlayerX = 0.f;
        inline static float s_lastPlayerY = 0.f;
        inline static std::string s_status = "Ready - enter a level";
        inline static std::vector<BotHazard> s_hazards;
        inline static std::vector<BotSolid> s_solids;
        inline static std::vector<BotOrb> s_orbs;
        inline static std::vector<BotPad> s_pads;
    };
}

// Mythix macro recorder: frame-timed input capture and deterministic event replay.
// Sub-frame integrations (CoS/CBS/CBF) are intentionally not claimed by this
// baseline until their respective APIs are detected and integrated.
namespace {
    struct MacroInput {
        int frame;
        bool down;
        int button;
        bool player1;
    };
    struct SavedMacro {
        std::string name;
        std::vector<MacroInput> inputs;
        int duration = 0;
    };

    class MythixMacroController {
    public:
        static bool recording() { return s_recording; }
        static bool playing() { return s_playing; }
        static std::string status() { return s_status; }
        static int count() { return static_cast<int>(s_macros.size()); }

        static void startRecording() {
            s_current = {};
            s_current.name = "Recording " + std::to_string(s_macros.size() + 1);
            s_frame = 0;
            s_recording = true;
            s_playing = false;
            s_status = "RECORDING - play normally";
        }

        static void stopRecording() {
            if (!s_recording) return;
            s_recording = false;
            s_current.duration = s_frame;
            if (!s_current.inputs.empty()) {
                s_macros.push_back(s_current);
                save();
                s_status = "Recording Saved!";
            } else {
                s_status = "No inputs captured";
            }
        }

        static void capture(bool down, int button, bool player1) {
            if (!s_recording || s_playing) return;
            s_current.inputs.push_back({s_frame, down, button, player1});
        }

        static void tick(PlayLayer* layer) {
            if (!layer) return;
            ++s_frame;
            if (!s_playing) return;
            if (s_playIndex >= s_playMacro.inputs.size()) {
                s_playing = false;
                s_status = "Playback finished";
                return;
            }
            while (s_playIndex < s_playMacro.inputs.size() &&
                   s_playMacro.inputs[s_playIndex].frame <= s_playFrame) {
                auto const event = s_playMacro.inputs[s_playIndex++];
                s_injecting = true;
                layer->handleButton(event.down, event.button, event.player1);
                s_injecting = false;
            }
            ++s_playFrame;
        }

        static bool injecting() { return s_injecting; }

        static void play(size_t index) {
            load();
            if (index >= s_macros.size() || s_macros[index].inputs.empty()) {
                s_status = "Recording unavailable";
                return;
            }
            s_playMacro = s_macros[index];
            s_playIndex = 0;
            s_playFrame = 0;
            s_frame = 0;
            s_recording = false;
            s_playing = true;
            s_status = "PLAYING: " + s_playMacro.name;
        }

        static void showLibrary() {
            load();
            if (s_macros.empty()) {
                FLAlertLayer::create("Bot Recordings", "No saved recordings yet. Press BOT LEVEL to record, then Stop Recording to save.", "OK")->show();
                return;
            }
            std::string message;
            for (size_t i = 0; i < s_macros.size(); ++i) {
                auto const& macro = s_macros[i];
                message += std::to_string(i + 1) + ". " + macro.name +
                    " - " + std::to_string(macro.inputs.size()) + " inputs, " +
                    std::to_string(macro.duration) + " frames\n";
            }
            message += "\nUse the Play Latest button to replay the newest recording.";
            FLAlertLayer::create("Bot Recordings", message, "OK")->show();
        }

        static void playLatest() {
            load();
            if (!s_macros.empty()) play(s_macros.size() - 1);
            else s_status = "No recordings to play";
        }

    private:
        static std::string filePath() {
            return (Mod::get()->getSaveDir() / "mythix-recordings.txt").string();
        }
        static void save() {
            std::ofstream out(filePath(), std::ios::trunc);
            if (!out) { s_status = "Could not save recording"; return; }
            for (auto const& macro : s_macros) {
                out << "M|" << macro.name << "|" << macro.duration << "\n";
                for (auto const& event : macro.inputs)
                    out << "I|" << event.frame << "|" << event.down << "|" << event.button << "|" << event.player1 << "\n";
                out << "E\n";
            }
        }
        static void load() {
            if (s_loaded) return;
            s_loaded = true;
            std::ifstream in(filePath());
            if (!in) return;
            std::string line;
            SavedMacro macro;
            while (std::getline(in, line)) {
                std::istringstream row(line);
                char kind = 0; row >> kind;
                if (kind == 'M') {
                    if (!macro.name.empty()) s_macros.push_back(macro);
                    macro = {};
                    std::getline(row, macro.name, '|');
                    if (!macro.name.empty() && macro.name[0] == '|') macro.name.erase(0, 1);
                    row >> macro.duration;
                } else if (kind == 'I') {
                    MacroInput event{};
                    char sep;
                    row >> sep >> event.frame >> sep >> event.down >> sep >> event.button >> sep >> event.player1;
                    macro.inputs.push_back(event);
                } else if (kind == 'E') {
                    if (!macro.name.empty()) s_macros.push_back(macro);
                    macro = {};
                }
            }
            if (!macro.name.empty()) s_macros.push_back(macro);
        }

        inline static bool s_recording = false;
        inline static bool s_playing = false;
        inline static bool s_injecting = false;
        inline static bool s_loaded = false;
        inline static int s_frame = 0;
        inline static int s_playFrame = 0;
        inline static size_t s_playIndex = 0;
        inline static std::string s_status = "Ready - enter a level";
        inline static SavedMacro s_current;
        inline static SavedMacro s_playMacro;
        inline static std::vector<SavedMacro> s_macros;
    };
}

class $modify(MythixInputLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
        auto layer = PlayLayer::get();
        if (layer && static_cast<GJBaseGameLayer*>(layer) == this && !MythixMacroController::injecting())
            MythixMacroController::capture(down, button, isPlayer1);
    }
};

class $modify(MythixPlayLayer, PlayLayer) {
    // Preserve the existing Mythix noclip behavior.
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (Mod::get()->getSettingValue<bool>(NOCLIP_SETTING))
            return;

        PlayLayer::destroyPlayer(player, object);
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        MythixMacroController::tick(this);
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
            bool enabled = MythixMacroController::recording();
            m_fields->botLabel->setString(enabled ? "[REC]  BOT LEVEL" : "[ ]  BOT LEVEL");
            m_fields->botLabel->setColor(enabled ? ccc3(255, 125, 55) : ccc3(255, 205, 160));
        }
        if (m_fields->botStatusLabel)
            m_fields->botStatusLabel->setString(MythixMacroController::status().c_str());
    }

    void onToggleBot(CCObject*) {
        if (MythixMacroController::recording()) MythixMacroController::stopRecording();
        else MythixMacroController::startRecording();
        refreshBotLabel();
    }
    void onStopRecording(CCObject*) { MythixMacroController::stopRecording(); refreshBotLabel(); }
    void onViewRecordings(CCObject*) { MythixMacroController::showLibrary(); }
    void onPlayLatest(CCObject*) { MythixMacroController::playLatest(); refreshBotLabel(); }

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
        auto panel = CCLayerColor::create(ccc4(19, 11, 13, 248), 300.f, 280.f);
        if (!panel)
            return;

        panel->setPosition({
            (winSize.width - 300.f) / 2.f,
            (winSize.height - 280.f) / 2.f
        });
        panel->setZOrder(10000);
        this->addChild(panel);
        m_fields->panel = panel;

        auto top = CCLayerColor::create(ccc4(72, 27, 19, 255), 300.f, 38.f);
        top->setPosition({0.f, 242.f});
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
            m_fields->botStatusLabel->setPosition({150.f, 17.f});
            panel->addChild(m_fields->botStatusLabel);
        }

        if (noclipButton)
            noclipButton->setPosition({150.f, 205.f});
        if (particlesButton)
            particlesButton->setPosition({150.f, 170.f});
        if (botButton)
            botButton->setPosition({150.f, 135.f});

        auto closeLabel = CCLabelBMFont::create("CLOSE", "bigFont.fnt");
        if (closeLabel) {
            closeLabel->setScale(0.42f);
            closeLabel->setColor(ccc3(255, 174, 104));
        }
        auto closeButton = closeLabel
            ? CCMenuItemLabel::create(closeLabel, this, menu_selector(MythixPauseLayer::onCloseUtilities))
            : nullptr;
        if (closeButton)
            closeButton->setPosition({265.f, 260.f});

        auto menu = CCMenu::create();
        if (menu) {
            menu->setPosition({0.f, 0.f});
            if (noclipButton) menu->addChild(noclipButton);
            if (particlesButton) menu->addChild(particlesButton);
            if (botButton) menu->addChild(botButton);
            auto stopLabel = makeToggleLabel("STOP RECORDING");
            auto stopButton = CCMenuItemLabel::create(stopLabel, this, menu_selector(MythixPauseLayer::onStopRecording));
            if (stopButton) { stopButton->setPosition({150.f, 100.f}); menu->addChild(stopButton); }
            auto viewLabel = makeToggleLabel("VIEW BOT RECORDINGS");
            auto viewButton = CCMenuItemLabel::create(viewLabel, this, menu_selector(MythixPauseLayer::onViewRecordings));
            if (viewButton) { viewButton->setPosition({150.f, 65.f}); menu->addChild(viewButton); }
            auto playLabel = makeToggleLabel("PLAY LATEST");
            auto playButton = CCMenuItemLabel::create(playLabel, this, menu_selector(MythixPauseLayer::onPlayLatest));
            if (playButton) { playButton->setPosition({150.f, 35.f}); menu->addChild(playButton); }
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
