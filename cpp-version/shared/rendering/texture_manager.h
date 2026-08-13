#ifndef TEXTURE_MANAGER_H
#define TEXTURE_MANAGER_H

#include "raylib.h"
#include <array>
#include <string>

//------------------------------------------------------------------------------
// TextureManager — the single owner of the game's GPU textures, keyed by an enum.
//
// Rationale: raylib requires every UnloadTexture to run while the GL context is
// still alive (before CloseWindow). This manager gives one teardown point
// (unloadAll / ~TextureManager). It is deliberately NOT a static-duration
// singleton (whose destructor would run at process exit, AFTER CloseWindow):
// own a scoped instance via std::unique_ptr in main() and reset() it BEFORE
// CloseWindow. A global accessor gTextures() is provided for call-site ergonomics
// and is valid only while that instance exists.
//
// Slots are app-lifetime: loaded once, freed once by unloadAll(). Adding an
// animation later is just a contiguous run of TextureId frames.
//------------------------------------------------------------------------------

enum TextureId {
    TEX_TILE_ATLAS,     // tileset diffuse atlas (from loadTilesetTexture)
    TEX_TILE_BUMP,      // tileset bump/normal atlas (optional)
    TEX_FLARE,          // plasma projectile glow
    TEX_BLASTER_BLOB,   // laser projectile streak
    TEX_SHIP_MAP,       // ship-view side-on image (dim base)
    TEX_SHIP_MAP_LIT,   // ship-view lit overlay
    TEX_ASMD,           // weapon-3 blast: a 4x1 sprite sheet (see SpriteAnimation)
    TEX_RLBOOM,         // explosion effect: an 8x1 sprite sheet (see EffectManager)
    // Beam weapon frame sets: 3 vertical 32x64 frames each, tiled along the beam length
    // with TEXTURE_WRAP_REPEAT. Contiguous runs so `base + frame` indexes the frame.
    TEX_BEAM_PLASMA_0,    // weapon 1 (Gas Axe) frames 0..2
    TEX_BEAM_PLASMA_1,
    TEX_BEAM_PLASMA_2,
    TEX_BEAM_LIGHTNING_0, // weapon 8 (Exterminator) frames 0..2
    TEX_BEAM_LIGHTNING_1,
    TEX_BEAM_LIGHTNING_2,
    TEX_DECAL_BLASTMARK,  // floor scorch (destructible explosion) — see DecalManager
    TEX_DECAL_DRIP,       // floor drip (damaged moving droid)
    // Level-authored (permanent) floor decals — see load3DLevelDecals / docs/decals.md.
    TEX_DECAL_BIOHAZARD,      // biohazard symbol (feature type 29)
    TEX_DECAL_STORAGEAREA,    // "STORAGE AREA" strip (type 30)
    TEX_DECAL_PROCESSINGAREA, // "PROCESSING AREA" strip (type 31)
    TEX_DECAL_TEXT_BIOHAZARD, // biohazard warning text (type 32)
    TEX_DECAL_TEXT_DANGER,    // "DANGER" strip (type 33)
    TEX_COUNT
};

class TextureManager {
public:
    TextureManager();
    ~TextureManager();

    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;

    // Adopt an already-loaded handle into a slot, unloading any previous occupant.
    void set(TextureId id, Texture2D texture);

    // LoadTexture from disk into a slot. Returns true if the load succeeded.
    bool loadFile(TextureId id, const std::string& path);

    // Direct handle for raylib draw calls; default {0} handle if the slot is empty.
    const Texture2D& get(TextureId id) const { return tex_[id]; }
    bool loaded(TextureId id) const { return tex_[id].id > 0; }

    // Free every slot. Idempotent; also runs from the destructor.
    void unloadAll();

private:
    std::array<Texture2D, TEX_COUNT> tex_{};
};

// Access the one live TextureManager. Valid only while an instance exists
// (constructed in main() after InitWindow). Asserts otherwise.
TextureManager& gTextures();

#endif // TEXTURE_MANAGER_H
