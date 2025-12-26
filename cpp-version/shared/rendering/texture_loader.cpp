#include "texture_loader.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <cstdio>

namespace fs = std::filesystem;

//------------------------------------------------------------------------------
// Path Utilities
//------------------------------------------------------------------------------

std::string normalizePath(const std::string& path) {
    std::string result = path;
    // Convert backslashes to forward slashes
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

//------------------------------------------------------------------------------
// Parse textures.txt
//------------------------------------------------------------------------------

bool parseTexturesFile(const char* textureTxtPath, const char* basePath, TextureLookup& outLookup) {
    std::ifstream file(textureTxtPath);
    if (!file.is_open()) {
        TraceLog(LOG_ERROR, "TEXTURE_LOADER: Failed to open: %s", textureTxtPath);
        return false;
    }

    outLookup.entries.clear();
    outLookup.indexMap.clear();
    outLookup.basePath = basePath ? basePath : "";
    outLookup.loaded = false;

    // Ensure basePath ends with /
    if (!outLookup.basePath.empty() && outLookup.basePath.back() != '/') {
        outLookup.basePath += '/';
    }

    std::string line;
    int lineNum = 0;
    int validCount = 0;
    int nullCount = 0;

    while (std::getline(file, line)) {
        lineNum++;

        // Skip empty lines
        if (line.empty()) continue;

        // Parse: index path flags
        std::istringstream iss(line);
        int index;
        std::string path;
        int flags = 0;

        if (!(iss >> index >> path)) {
            continue;  // Skip malformed lines
        }
        iss >> flags;  // Optional flags

        TextureEntry entry;
        entry.index = index;
        entry.flags = flags;

        if (path == "NULL" || path == "null") {
            entry.isNull = true;
            entry.path = "";
            entry.normalizedPath = "";
            nullCount++;
        } else {
            entry.isNull = false;
            entry.path = path;
            entry.normalizedPath = normalizePath(path);
            validCount++;
        }

        size_t pos = outLookup.entries.size();
        outLookup.entries.push_back(entry);
        outLookup.indexMap[index] = pos;
    }

    outLookup.loaded = true;

    TraceLog(LOG_INFO, "TEXTURE_LOADER: Parsed %s - %d valid, %d null entries",
             textureTxtPath, validCount, nullCount);

    return true;
}

//------------------------------------------------------------------------------
// Texture Path Lookup
//------------------------------------------------------------------------------

std::string getTexturePath(const TextureLookup& lookup, int index) {
    auto it = lookup.indexMap.find(index);
    if (it == lookup.indexMap.end()) {
        return "";
    }

    const TextureEntry& entry = lookup.entries[it->second];
    if (entry.isNull) {
        return "";
    }

    return entry.normalizedPath;
}

std::string getTextureFullPath(const TextureLookup& lookup, int index) {
    std::string relativePath = getTexturePath(lookup, index);
    if (relativePath.empty()) {
        return "";
    }

    return lookup.basePath + relativePath;
}

bool isTextureValid(const TextureLookup& lookup, int index) {
    auto it = lookup.indexMap.find(index);
    if (it == lookup.indexMap.end()) {
        return false;
    }

    return !lookup.entries[it->second].isNull;
}

//------------------------------------------------------------------------------
// Texture Cache
//------------------------------------------------------------------------------

void textureCacheInit(TextureCache& cache) {
    cache.textures.clear();
    cache.flatNormal = {0};
    cache.initialized = true;
}

bool textureCacheLoadFlatNormal(TextureCache& cache, const char* flatNormalPath) {
    if (!cache.initialized) {
        TraceLog(LOG_ERROR, "TEXTURE_CACHE: Not initialized");
        return false;
    }

    if (!fs::exists(flatNormalPath)) {
        TraceLog(LOG_ERROR, "TEXTURE_CACHE: Flat normal not found: %s", flatNormalPath);
        return false;
    }

    cache.flatNormal = LoadTexture(flatNormalPath);
    if (cache.flatNormal.id == 0) {
        TraceLog(LOG_ERROR, "TEXTURE_CACHE: Failed to load flat normal: %s", flatNormalPath);
        return false;
    }

    TraceLog(LOG_INFO, "TEXTURE_CACHE: Loaded flat normal map: %s", flatNormalPath);
    return true;
}

bool textureCacheLoad(TextureCache& cache, const TextureLookup& lookup, int index) {
    if (!cache.initialized) {
        return false;
    }

    // Already loaded?
    if (cache.textures.find(index) != cache.textures.end()) {
        return true;
    }

    std::string fullPath = getTextureFullPath(lookup, index);
    if (fullPath.empty()) {
        return false;  // NULL or invalid index
    }

    if (!fs::exists(fullPath)) {
        TraceLog(LOG_WARNING, "TEXTURE_CACHE: File not found: %s (index %d)", fullPath.c_str(), index);
        return false;
    }

    Texture2D texture = LoadTexture(fullPath.c_str());
    if (texture.id == 0) {
        TraceLog(LOG_WARNING, "TEXTURE_CACHE: Failed to load: %s (index %d)", fullPath.c_str(), index);
        return false;
    }

    cache.textures[index] = texture;
    TraceLog(LOG_DEBUG, "TEXTURE_CACHE: Loaded index %d: %s", index, fullPath.c_str());
    return true;
}

Texture2D textureCacheGet(const TextureCache& cache, int index) {
    auto it = cache.textures.find(index);
    if (it != cache.textures.end()) {
        return it->second;
    }
    return cache.flatNormal;  // Default to flat normal
}

Texture2D textureCacheGetDiffuse(const TextureCache& cache, int textureIndex1) {
    return textureCacheGet(cache, textureIndex1);
}

Texture2D textureCacheGetBump(const TextureCache& cache, int textureIndex2) {
    // Index 0 is flat bump, so return flat normal
    if (textureIndex2 == 0) {
        return cache.flatNormal;
    }
    return textureCacheGet(cache, textureIndex2);
}

void textureCacheDestroy(TextureCache& cache) {
    for (auto& [index, texture] : cache.textures) {
        if (texture.id > 0) {
            UnloadTexture(texture);
        }
    }
    cache.textures.clear();

    if (cache.flatNormal.id > 0) {
        UnloadTexture(cache.flatNormal);
        cache.flatNormal = {0};
    }

    cache.initialized = false;
    TraceLog(LOG_INFO, "TEXTURE_CACHE: Destroyed");
}

//------------------------------------------------------------------------------
// File Copy Utilities
//------------------------------------------------------------------------------

int copyTexturesToOutput(const TextureLookup& lookup,
                         const std::vector<int>& indices,
                         const char* outputDir) {
    if (!lookup.loaded) {
        TraceLog(LOG_ERROR, "TEXTURE_LOADER: Lookup not loaded");
        return 0;
    }

    fs::path outPath(outputDir);
    if (!fs::exists(outPath)) {
        fs::create_directories(outPath);
    }

    int copiedCount = 0;

    for (int index : indices) {
        std::string srcPath = getTextureFullPath(lookup, index);
        if (srcPath.empty()) {
            continue;  // NULL or invalid
        }

        std::string relativePath = getTexturePath(lookup, index);
        fs::path destPath = outPath / relativePath;

        // Skip if destination already exists
        if (fs::exists(destPath)) {
            continue;
        }

        // Create destination directory
        fs::path destDir = destPath.parent_path();
        if (!fs::exists(destDir)) {
            fs::create_directories(destDir);
        }

        // Copy file
        try {
            if (fs::exists(srcPath)) {
                fs::copy_file(srcPath, destPath, fs::copy_options::skip_existing);
                copiedCount++;
                TraceLog(LOG_DEBUG, "TEXTURE_LOADER: Copied %s", relativePath.c_str());
            } else {
                TraceLog(LOG_WARNING, "TEXTURE_LOADER: Source not found: %s", srcPath.c_str());
            }
        } catch (const fs::filesystem_error& e) {
            TraceLog(LOG_WARNING, "TEXTURE_LOADER: Copy failed: %s - %s",
                     relativePath.c_str(), e.what());
        }
    }

    if (copiedCount > 0) {
        TraceLog(LOG_INFO, "TEXTURE_LOADER: Copied %d texture files to %s", copiedCount, outputDir);
    }

    return copiedCount;
}
