#ifndef TEXTURE_LOADER_H
#define TEXTURE_LOADER_H

#include "raylib.h"
#include <string>
#include <vector>
#include <unordered_map>

//------------------------------------------------------------------------------
// Texture Loader - Parses textures.txt and loads textures for tile rendering
//
// Texture indices in tile definitions reference entries in textures.txt:
//   - textureIndex1 = diffuse texture index
//   - textureIndex2 = bump/normal map index
//
// Texture 0 is always flat.png (flat bump map) used as default
//------------------------------------------------------------------------------

// Parsed texture entry from textures.txt
struct TextureEntry {
    int index = 0;
    std::string path;       // Original path (backslash-separated)
    std::string normalizedPath; // Forward-slash, relative path
    int flags = 0;
    bool isNull = false;    // True if path was "NULL"
};

// Texture lookup table
struct TextureLookup {
    std::vector<TextureEntry> entries;
    std::unordered_map<int, size_t> indexMap;  // index -> entries position
    std::string basePath;   // Base directory for texture files
    bool loaded = false;
};

// Loaded texture cache for rendering
struct TextureCache {
    std::unordered_map<int, Texture2D> textures;  // index -> loaded texture
    Texture2D flatNormal;   // Default flat normal map (texture 0)
    bool initialized = false;
};

//------------------------------------------------------------------------------
// Texture Lookup API (parsing textures.txt)
//------------------------------------------------------------------------------

// Parse textures.txt file and build lookup table
// textureTxtPath: path to textures.txt
// basePath: base directory for resolving texture paths (e.g., uber/uberdroid/data/)
// Returns true on success
bool parseTexturesFile(const char* textureTxtPath, const char* basePath, TextureLookup& outLookup);

// Get texture path for an index
// Returns empty string if index not found or is NULL
std::string getTexturePath(const TextureLookup& lookup, int index);

// Get full path to a texture (basePath + texture path)
std::string getTextureFullPath(const TextureLookup& lookup, int index);

// Check if texture index is valid (exists and not NULL)
bool isTextureValid(const TextureLookup& lookup, int index);

//------------------------------------------------------------------------------
// Texture Cache API (loading and managing raylib textures)
//------------------------------------------------------------------------------

// Initialize texture cache
// Must call after raylib InitWindow()
void textureCacheInit(TextureCache& cache);

// Load a texture by index from lookup
// Returns true if loaded successfully
bool textureCacheLoad(TextureCache& cache, const TextureLookup& lookup, int index);

// Load flat normal map (index 0 or default)
// flatNormalPath: path to flat_normal.png (e.g., "textures/flat_normal.png")
bool textureCacheLoadFlatNormal(TextureCache& cache, const char* flatNormalPath);

// Get texture for an index (returns flat normal if not found)
Texture2D textureCacheGet(const TextureCache& cache, int index);

// Get diffuse texture for a tile (index1)
Texture2D textureCacheGetDiffuse(const TextureCache& cache, int textureIndex1);

// Get bump texture for a tile (index2), returns flat normal if 0 or not found
Texture2D textureCacheGetBump(const TextureCache& cache, int textureIndex2);

// Cleanup all loaded textures
void textureCacheDestroy(TextureCache& cache);

//------------------------------------------------------------------------------
// File Utility Functions
//------------------------------------------------------------------------------

// Copy texture files to output directory
// lookup: parsed texture lookup
// indices: list of texture indices to copy
// outputDir: destination directory (e.g., "output/textures/")
// Returns number of files copied
int copyTexturesToOutput(const TextureLookup& lookup,
                         const std::vector<int>& indices,
                         const char* outputDir);

// Normalize path (convert backslashes to forward slashes, lowercase)
std::string normalizePath(const std::string& path);

#endif // TEXTURE_LOADER_H
