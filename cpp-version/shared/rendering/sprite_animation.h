#ifndef SPRITE_ANIMATION_H
#define SPRITE_ANIMATION_H

#include "rendering/texture_manager.h"

//------------------------------------------------------------------------------
// SpriteAnimation — a frame-animated sprite backed by a single **sprite sheet**
// texture. Frames are a `columns` x `rows` grid within one texture, cycled at a
// fixed rate. Animating advances the *source rectangle*, not the bound texture,
// so no per-frame texture rebinds.
//
// It holds no mutable cursor: one config drives many independent instances, each
// passing its own elapsed time (`age`) — so instances animate out of sync.
//
// (We use sheets, not per-frame texture-id advancement. If both are ever needed,
// support for the index approach can be added alongside; it is not assumed here.)
//------------------------------------------------------------------------------

struct SpriteAnimation {
    TextureId sheet   = TEX_COUNT;  // the sprite-sheet texture
    int       columns = 1;          // frames per row
    int       rows    = 1;          // rows of frames
    float     fps     = 10.0f;      // frames per second

    int frameCount() const { return (columns > 0 && rows > 0) ? columns * rows : 1; }

    // Frame index [0, frameCount) for an instance alive `age` seconds (loops).
    int frameIndexAt(float age) const {
        int n = frameCount();
        if (n <= 1 || fps <= 0.0f) return 0;
        int f = static_cast<int>(age * fps) % n;
        return (f < 0) ? 0 : f;
    }

    // Pixel source rectangle of the current frame within a `texW`x`texH` sheet.
    Rectangle sourceRect(float age, int texW, int texH) const {
        int f = frameIndexAt(age);
        float fw = (columns > 0) ? static_cast<float>(texW) / columns : static_cast<float>(texW);
        float fh = (rows > 0) ? static_cast<float>(texH) / rows : static_cast<float>(texH);
        int cx = (columns > 0) ? f % columns : 0;
        int cy = (columns > 0) ? f / columns : 0;
        return {cx * fw, cy * fh, fw, fh};
    }
};

#endif // SPRITE_ANIMATION_H
