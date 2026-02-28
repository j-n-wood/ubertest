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
    Static
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

inline constexpr uint16_t MASK_UNIT       = CATEGORY_UNIT | CATEGORY_PROJECTILE | CATEGORY_STATIC | CATEGORY_DEBRIS;
inline constexpr uint16_t MASK_PROJECTILE = CATEGORY_UNIT | CATEGORY_STATIC;
inline constexpr uint16_t MASK_STATIC     = 0xFFFF;
inline constexpr uint16_t MASK_DEBRIS     = CATEGORY_UNIT | CATEGORY_STATIC | CATEGORY_DEBRIS;

#endif // BODY_USER_DATA_H
