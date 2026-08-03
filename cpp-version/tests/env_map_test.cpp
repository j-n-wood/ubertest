// env_map_test.cpp — the glTF `extras` env-map reader (pure JSON parsing, no GPU).
#include <gtest/gtest.h>
#include "rendering/env_map.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string writeTempGltf(const std::string& name, const std::string& body) {
    fs::path p = fs::temp_directory_path() / name;
    std::ofstream out(p);
    out << body;
    out.close();
    return p.string();
}

}  // namespace

TEST(EnvMapExtras, ParsesTextureColorAndIntensity) {
    const std::string gltf = R"({
      "materials": [
        { "name": "m0", "extras": {
            "envTexture": "textures/envmapgold.png",
            "envColor": [0.8, 0.4, 0.2, 1.0],
            "envIntensity": 1.5 } }
      ]
    })";
    auto entries = envMapReadExtras(writeTempGltf("envmap_a.gltf", gltf));
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].gltfMaterialIndex, 0);
    EXPECT_EQ(entries[0].texturePath, "textures/envmapgold.png");
    EXPECT_FLOAT_EQ(entries[0].color[0], 0.8f);
    EXPECT_FLOAT_EQ(entries[0].color[1], 0.4f);
    EXPECT_FLOAT_EQ(entries[0].color[2], 0.2f);
    EXPECT_FLOAT_EQ(entries[0].color[3], 1.0f);
    EXPECT_FLOAT_EQ(entries[0].intensity, 1.5f);
}

TEST(EnvMapExtras, DefaultsColorWhiteAndIntensityOne) {
    const std::string gltf = R"({
      "materials": [ { "extras": { "envTexture": "textures/x.png" } } ]
    })";
    auto entries = envMapReadExtras(writeTempGltf("envmap_b.gltf", gltf));
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_FLOAT_EQ(entries[0].color[0], 1.0f);
    EXPECT_FLOAT_EQ(entries[0].color[3], 1.0f);
    EXPECT_FLOAT_EQ(entries[0].intensity, 1.0f);
}

TEST(EnvMapExtras, OnlyMaterialsWithEnvTextureAndCorrectIndices) {
    // Material 0 has no extras, 1 has extras but no envTexture, 2 has an env texture.
    const std::string gltf = R"({
      "materials": [
        { "name": "plain" },
        { "name": "other", "extras": { "shaderHint": "blinn-phong" } },
        { "name": "env", "extras": { "envTexture": "textures/e.png", "envIntensity": 0.3 } }
      ]
    })";
    auto entries = envMapReadExtras(writeTempGltf("envmap_c.gltf", gltf));
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].gltfMaterialIndex, 2);  // preserves the glTF material index
    EXPECT_FLOAT_EQ(entries[0].intensity, 0.3f);
}

TEST(EnvMapExtras, MissingOrBinaryFileYieldsEmpty) {
    EXPECT_TRUE(envMapReadExtras("/no/such/file.gltf").empty());
    // A non-JSON payload must not throw.
    EXPECT_TRUE(envMapReadExtras(writeTempGltf("envmap_bin.glb", std::string("glTF\x02\x00", 6))).empty());
}

// The shipped exemplars really carry the extras (guards against a future regeneration dropping them).
TEST(EnvMapExtras, ShippedDiskHasGoldEnvMap) {
    const std::string diskPath = std::string(TEST_PROJECT_ROOT) + "/cpp-version/assets/models/disk.gltf";
    auto entries = envMapReadExtras(diskPath);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].texturePath, "textures/envmapgold.png");
    EXPECT_FLOAT_EQ(entries[0].intensity, 1.0f);
}

// The wider unit migration (all DRAWTYPE ENVMAP / EFFECTTEXTURE models) must stay in place — the
// chrome-look probe droid is a representative guard against a future regeneration dropping extras.
TEST(EnvMapExtras, ShippedProbeHasChromeEnvMap) {
    const std::string probePath = std::string(TEST_PROJECT_ROOT) + "/cpp-version/assets/models/probe.gltf";
    auto entries = envMapReadExtras(probePath);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].texturePath, "textures/metal_grey_1_256.png");
}
