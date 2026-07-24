# Effect Texture Integration Plan

## Overview

Add support for effect textures (2D environment maps) from legacy renderobjects.txt to the modern rendering pipeline. This involves:
1. Parsing and converting effect textures in droid_tool
2. Referencing effect textures outside GLTF (since env maps aren't standard PBR)
3. Creating a new shader with additive environment mapping
4. Organizing draw calls by shader to minimize state changes

## Key Findings

### Current State
- **Parser**: `effectTextures` vector already parsed in `RenderObject` struct but unused
- **drawType**: Parsed but unused (ENVMAP, DIFFUSE, BUMP, ADDITIVE, etc.)
- **Raylib**: Does NOT sort draw calls internally - immediate mode rendering
- **Shader**: Current `lighting.fs` has `texture0` (diffuse) and `texture2` (normal map)

### Effect Texture Semantics (from legacy_data_format_reference.md)
- Effect textures used as additive environment maps
- UV from geometry normals in screen space: `uv = (0.5, 0.5) + 0.5 * normal.xy`
- Applied additively on top of diffuse lighting
- Relevant drawTypes: ENVMAP (default), DIFFUSE (no effect texture)

---

## Implementation Plan

### Phase 1: Effect Texture Parsing & Conversion

**Files to modify:**
- `cpp-version/tools/droid_tool/unit_generator.cpp`

**Changes:**
1. Locate effect texture files from `RenderObject.effectTextures`
2. Convert to standard formats (BMP → JPG/PNG) like diffuse textures
3. Copy to output directory alongside model textures
4. Track effect texture paths per render object

**Texture path resolution** (same as diffuse):
```
uber/uberdroid/data/textures/<path> → cpp-version/assets/textures/<converted>
```

### Phase 2: Effect Texture References in JSON

**Proposal**: Add `rendering` section to unit JSON at section level.

Since GLTF materials use PBR and don't support arbitrary environment maps, store effect texture references in the unit definition JSON:

```json
{
  "rootSection": {
    "name": "section_0",
    "model": "models/476.gltf",
    "rendering": {
      "shader": "envmap",
      "effectTextures": ["textures/envmapgold.jpg"]
    }
  }
}
```

**Files to modify:**
- `cpp-version/shared/units/unit_types.h` - Add `RenderingProperties` struct
- `cpp-version/shared/units/unit_json.cpp` - Parse/serialize rendering section
- `cpp-version/tools/droid_tool/unit_generator.cpp` - Output rendering section

**Data structure addition:**
```cpp
struct RenderingProperties {
    std::string shader;                        // "envmap", "diffuse", etc.
    std::vector<std::string> effectTextures;   // Paths relative to assets
};

struct SectionDefinition {
    // ... existing fields ...
    std::optional<RenderingProperties> rendering;
};
```

### Phase 3: New Environment Map Shader

**New files:**
- `cpp-version/assets/shaders/envmap.vs` - Copy of lighting.vs
- `cpp-version/assets/shaders/envmap.fs` - New fragment shader

**Shader design:**
```glsl
// envmap.fs - Blinn-Phong with additive environment map
uniform sampler2D texture0;  // Diffuse
uniform sampler2D texture1;  // Effect/environment map (NEW - use MATERIAL_MAP_EMISSION slot)
uniform sampler2D texture2;  // Normal map

// In main():
// Calculate screen-space normal for env map lookup
vec3 viewNormal = normalize(mat3(viewMatrix) * normal);
vec2 envUV = vec2(0.5) + 0.5 * viewNormal.xy;
vec3 envColor = texture(texture1, envUV).rgb;

// Add environment contribution
result += envColor * envMapIntensity;
```

**Uniforms to add:**
- `envMapIntensity` - Strength of additive effect (default: 1.0)
- `useEnvMap` - Toggle (0/1)

### Phase 4: Shader Management & Draw Call Organization

**Files to modify:**
- `cpp-version/shared/rendering/scene_renderer.h/cpp` - Multiple shader support
- `cpp-version/shared/units/unit_manager.h/cpp` - Per-section shader assignment

**Approach:**

1. **Shader Registry** in SceneRenderer:
```cpp
struct SceneRenderer {
    Shader shaderLighting;    // Existing
    Shader shaderEnvMap;      // New
    std::unordered_map<std::string, Shader*> shaderMap;
};
```

2. **Per-Section Shader Assignment** in UnitManager:
- During `createSectionInstance()`, check `SectionDefinition.rendering.shader`
- Assign appropriate shader to model materials
- Load effect textures and bind to `MATERIAL_MAP_EMISSION` slot

3. **Draw Call Sorting** (Optional optimization):
Since Raylib doesn't sort, add explicit sorting if needed:
```cpp
// In UnitManager::renderAll()
// Sort sections by shader before rendering
std::sort(renderQueue.begin(), renderQueue.end(),
    [](auto& a, auto& b) { return a->shaderType < b->shaderType; });
```

For now, defer sorting - the number of units is small enough that shader switches won't be a bottleneck. Document this as a future optimization.

### Phase 5: Effect Texture Loading at Runtime

**Files to modify:**
- `cpp-version/shared/units/unit_manager.cpp`

**During section instance creation:**
```cpp
if (def.rendering.has_value() && !def.rendering->effectTextures.empty()) {
    // Load effect texture
    std::string texPath = resolveAssetPath(def.rendering->effectTextures[0]);
    Texture2D envTex = LoadTexture(texPath.c_str());

    // Bind to material slot (use EMISSION as it's unused for PBR)
    for (int i = 0; i < section->model.materialCount; i++) {
        section->model.materials[i].maps[MATERIAL_MAP_EMISSION].texture = envTex;
    }

    // Assign envmap shader
    applyShaderToSection(section, renderer->shaderEnvMap);
}
```

---

## File Summary

| File | Action |
|------|--------|
| `tools/droid_tool/unit_generator.cpp` | Convert & output effect textures, add rendering section |
| `shared/units/unit_types.h` | Add `RenderingProperties` struct |
| `shared/units/unit_json.cpp` | Parse/serialize rendering section |
| `assets/shaders/envmap.vs` | New (copy of lighting.vs) |
| `assets/shaders/envmap.fs` | New (lighting.fs + env map logic) |
| `shared/rendering/scene_renderer.h` | Add shader registry |
| `shared/rendering/scene_renderer.cpp` | Load multiple shaders |
| `shared/units/unit_manager.cpp` | Per-section shader/texture assignment |

---

## Verification Plan

1. **Texture conversion**: Run droid_tool, verify effect textures copied to `assets/textures/`
2. **JSON output**: Check generated unit JSONs have `rendering` section with effect texture paths
3. **Shader compilation**: Load envmap shader in unit_test, verify no GL errors
4. **Visual test**: Load a droid with ENVMAP drawType (e.g., Class 9 - 476), verify additive metallic sheen
5. **Comparison**: Side-by-side with DIFFUSE droid (e.g., Class 1 - 123) to confirm different appearance

---

## Notes

- **Draw call sorting**: Deferred - current unit count is low enough that shader state changes are acceptable
- **Multiple effect textures**: Legacy format supports up to 5 per material; start with single effect texture per section
- **Fallback**: Sections without `rendering` block use default lighting shader (backward compatible)
