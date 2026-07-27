#include "rendering/texture_manager.h"

#include <cassert>

namespace {
// The one live instance. Set in the ctor, cleared in the dtor — so gTextures()
// resolves to whatever scoped TextureManager is currently alive.
TextureManager* g_instance = nullptr;
}  // namespace

TextureManager::TextureManager() {
    assert(g_instance == nullptr && "only one TextureManager may exist at a time");
    g_instance = this;
}

TextureManager::~TextureManager() {
    unloadAll();
    if (g_instance == this) g_instance = nullptr;
}

void TextureManager::set(TextureId id, Texture2D texture) {
    Texture2D& slot = tex_[id];
    if (slot.id > 0 && slot.id != texture.id) {
        UnloadTexture(slot);
    }
    slot = texture;
}

bool TextureManager::loadFile(TextureId id, const std::string& path) {
    set(id, LoadTexture(path.c_str()));
    return tex_[id].id > 0;
}

void TextureManager::unloadAll() {
    for (Texture2D& t : tex_) {
        if (t.id > 0) {
            UnloadTexture(t);
            t = {0};
        }
    }
}

TextureManager& gTextures() {
    assert(g_instance != nullptr && "gTextures() called with no live TextureManager");
    return *g_instance;
}
