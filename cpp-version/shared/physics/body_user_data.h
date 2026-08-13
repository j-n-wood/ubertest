#ifndef BODY_USER_DATA_H
#define BODY_USER_DATA_H

#include <cstdint>

//------------------------------------------------------------------------------
// Body identification tags for Box2D user data
//------------------------------------------------------------------------------

enum class BodyTag : uint8_t {
    None,
    Unit,
    Projectile,
    Debris,
    Static,
    Door,
    Object      // destructible scenery (owner = ObjectInstance*); see docs/scenery_entities.md
};

struct BodyUserData {
    BodyTag tag = BodyTag::None;
    void* owner = nullptr;
};

//------------------------------------------------------------------------------
// Collision category bits
//------------------------------------------------------------------------------

inline constexpr uint16_t CATEGORY_UNIT       = 0x0001;
inline constexpr uint16_t CATEGORY_PROJECTILE = 0x0002;
inline constexpr uint16_t CATEGORY_STATIC     = 0x0004;
inline constexpr uint16_t CATEGORY_DEBRIS     = 0x0008;
inline constexpr uint16_t CATEGORY_DOOR       = 0x0010;  // conditional: solid only when closed
// A "glass wall" (material drawtype 5, e.g. the glass tunnel): physically SOLID to units, projectiles,
// beams and the disruptor (all collision masks below include it), but deliberately ABSENT from the
// sight/line-of-sight raycast masks so visibility passes through it. See wall_mesh (drawtype 5) →
// game_create_level_collision, and the raycast masks in ai_manager/beam_manager/disruptor/game.
inline constexpr uint16_t CATEGORY_GLASS      = 0x0020;

inline constexpr uint16_t MASK_UNIT       = CATEGORY_UNIT | CATEGORY_PROJECTILE | CATEGORY_STATIC | CATEGORY_DEBRIS | CATEGORY_DOOR | CATEGORY_GLASS;
inline constexpr uint16_t MASK_PROJECTILE = CATEGORY_UNIT | CATEGORY_STATIC | CATEGORY_DOOR | CATEGORY_GLASS;
inline constexpr uint16_t MASK_STATIC     = 0xFFFF;
inline constexpr uint16_t MASK_DEBRIS     = CATEGORY_UNIT | CATEGORY_STATIC | CATEGORY_DEBRIS | CATEGORY_GLASS;

// A closed door blocks units and projectiles; when open its collision shape's mask
// is cleared to 0 (collides with nothing). The sensor shape that detects units uses
// MASK_DOOR_SENSOR so only units trigger opening.
inline constexpr uint16_t MASK_DOOR_SOLID  = CATEGORY_UNIT | CATEGORY_PROJECTILE;
inline constexpr uint16_t MASK_DOOR_SENSOR = CATEGORY_UNIT;

#endif // BODY_USER_DATA_H
