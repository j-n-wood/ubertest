#ifndef OBJECT_MANAGER_H
#define OBJECT_MANAGER_H

#include "raylib.h"
#include "box2d/box2d.h"
#include "physics/body_user_data.h"
#include <string>
#include <unordered_map>
#include <vector>

//------------------------------------------------------------------------------
// Data-driven scenery objects (docs/scenery_entities.md). Object *types* are defined once in
// assets/objects/*.json (model, physics, health, shader), exactly parallel to unit types; each
// level's entities.json places *instances* referencing a definition by its uber `renderIndex`.
// ObjectManager loads the catalog, resolves instances per level, and advances spin. Rendering
// (Object3DRenderer) and collision (game) read the instances; sim-only otherwise.
//------------------------------------------------------------------------------

enum class ObjectDrawType {
    Diffuse,     // plain lit model
    Glow,        // emissive/glow material — self-illuminated via the shader's emissive term (glowIntensity)
    ShadowOnly,  // mesh never drawn — contributes only to the shadow pass (Phase 3)
};

// Where a Glow def's colour comes from (uber's eGlowSource). Static = the model's own material
// colour, steady. Alert = the ship's alert-band colour, pulsed (the alert lights) — driven each
// frame by the game from the alert level. See docs/scenery_entities.md.
enum class GlowSource {
    Static,
    Alert,
};

struct ObjectDefinition {
    std::string id;
    int renderIndex = -1;              // uber renderIndex this def represents (instance key)
    std::string model;                 // gltf path relative to assetPath ("" = no visible mesh)
    float scale = 1.0f;
    bool floating = true;              // true = no physics footprint; false = solid, blocks movement
    float collisionRadius = 0.0f;      // grounded footprint half-size (metres), used when !floating
    bool destructible = false;
    int health = 50;
    float explodeSize = 0.0f;
    ObjectDrawType drawType = ObjectDrawType::Diffuse;
    float glowIntensity = 1.0f;        // emissive strength when drawType == Glow (self-illumination)
    GlowSource glowSource = GlowSource::Static;  // Glow colour source (Static = material, Alert = pulsing band colour)
    bool castsShadow = false;
    Vector3 spin = {0, 0, 0};          // default spin (rad/s about up); an instance's own spin wins
};

// One placed instance in a level (from entities.json objects[]).
struct ObjectSpec {
    int renderIndex = 0;
    Vector3 position = {0, 0, 0};      // render-space (X, Y up/height, Z)
    Vector3 rotation = {0, 0, 0};      // raw game frame (rotation.z = facing)
    Vector3 spin = {0, 0, 0};          // rad/s about up
};

struct ObjectInstance {
    const ObjectDefinition* def = nullptr;
    Vector3 position = {0, 0, 0};
    float facingRad = 0.0f;            // rotation.z (game frame)
    float spinRad = 0.0f;              // rad/s about up
    float spinAngle = 0.0f;            // accumulated spin
    float health = 0.0f;               // remaining hit points (destructible defs); Phase 2
    bool alive = true;                 // cleared on destruction (stops render/shadow/collision)
    // Continuous-damage accumulator (explosions, beams): raw damage sums into pendingDamage and is
    // flushed into health once per REALTIME_DAMAGE_INTERVAL in ObjectManager::update — mirrors the
    // unit realtime-damage path. Single-hit projectile damage still subtracts immediately.
    float pendingDamage = 0.0f;
    float damageAccumTimer = 0.0f;
    // Physics footprint for a grounded object (b2_nullBodyId when floating). The game owns creation
    // and sets bodyUserData so a projectile contact can find this instance. See docs/scenery_entities.md.
    b2BodyId bodyId = b2_nullBodyId;
    BodyUserData bodyUserData;         // {BodyTag::Object, this} once the game wires the body
};

// Accumulate continuous (explosion/beam) raw damage onto a destructible object; flushed on the
// shared realtime tick in ObjectManager::update (mirrors unit accumulateRealtimeDamage). No-op for
// floating/cosmetic/dead instances, so callers can pass any hit body blindly.
inline void accumulateObjectDamage(ObjectInstance& o, float raw) {
    if (raw > 0.0f && o.alive && o.def && o.def->destructible) o.pendingDamage += raw;
}

class ObjectManager {
public:
    // Load every assets/objects/*.json into the catalog (idempotent-ish; clears first).
    void loadDefinitions(const std::string& dir);
    const ObjectDefinition* defForRenderIndex(int renderIndex) const;
    const std::vector<ObjectDefinition>& definitions() const { return defs_; }

    // Build this level's instances from bundle specs (unknown renderIndices are skipped).
    void setInstances(const std::vector<ObjectSpec>& specs);
    void update(float dt);                       // advance spin animation
    const std::vector<ObjectInstance>& instances() const { return instances_; }
    // Mutable view for the game: wiring collision bodies (game_create_objects) and reaping
    // destroyed destructibles (game_reap_objects). Instance addresses are stable after setInstances
    // (the vector isn't grown or compacted), so pointers held in body userData stay valid.
    std::vector<ObjectInstance>& instancesMut() { return instances_; }
    void clear() { instances_.clear(); }

private:
    std::vector<ObjectDefinition> defs_;
    std::unordered_map<int, int> byRenderIndex_;   // renderIndex -> index into defs_
    std::vector<ObjectInstance> instances_;
};

#endif // OBJECT_MANAGER_H
