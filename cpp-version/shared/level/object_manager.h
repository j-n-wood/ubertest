#ifndef OBJECT_MANAGER_H
#define OBJECT_MANAGER_H

#include "raylib.h"
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
    Glow,        // emissive/glow material (Phase 4; rendered as Diffuse until then)
    ShadowOnly,  // mesh never drawn — contributes only to the shadow pass (Phase 3)
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
    int health = 0;                    // Phase 2
    bool alive = true;
};

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
    void clear() { instances_.clear(); }

private:
    std::vector<ObjectDefinition> defs_;
    std::unordered_map<int, int> byRenderIndex_;   // renderIndex -> index into defs_
    std::vector<ObjectInstance> instances_;
};

#endif // OBJECT_MANAGER_H
