#ifndef UNIT_MANAGER_H
#define UNIT_MANAGER_H

#include "unit_types.h"
#include "unit_instance.h"
#include <unordered_map>
#include <memory>
#include <string>
#include <string_view>
#include <optional>

//------------------------------------------------------------------------------
// Unit Manager - Handles loading, caching, and instantiation
//------------------------------------------------------------------------------

class UnitManager {
public:
    UnitManager() = default;
    ~UnitManager();

    // Prevent copying
    UnitManager(const UnitManager&) = delete;
    UnitManager& operator=(const UnitManager&) = delete;

    // Initialize with Box2D world
    // modelsBasePath: optional base path for resolving model references (can be nullptr)
    void init(b2WorldId worldId, const char* modelsBasePath = nullptr);
    void destroy();

    //--------------------------------------------------------------------------
    // Definition Management
    //--------------------------------------------------------------------------

    // Load a unit definition from JSON file, returns nullptr on failure
    [[nodiscard]] const UnitDefinition* loadDefinition(std::string_view path);

    // Get a previously loaded definition by id
    [[nodiscard]] const UnitDefinition* getDefinition(std::string_view id) const;

    // Unload a definition from cache (allows reloading from file)
    void unloadDefinition(std::string_view id);

    // Preload all definitions from a directory
    void preloadDefinitions(std::string_view directory);

    // Get all loaded definition IDs
    [[nodiscard]] std::vector<std::string> getDefinitionIds() const;

    //--------------------------------------------------------------------------
    // Instance Management
    //--------------------------------------------------------------------------

    // Create an instance from a definition ID
    [[nodiscard]] UnitInstance* createInstance(
        std::string_view definitionId,
        Vector2 position,
        float rotation
    );

    // Create an instance from a definition pointer
    [[nodiscard]] UnitInstance* createInstance(
        const UnitDefinition* definition,
        Vector2 position,
        float rotation
    );

    // Destroy an instance
    void destroyInstance(UnitInstance* instance);

    // Get all active instances
    [[nodiscard]] const std::vector<std::unique_ptr<UnitInstance>>& getInstances() const;

    //--------------------------------------------------------------------------
    // Debris Management
    //--------------------------------------------------------------------------

    // Dismantle a unit into separate debris objects
    // Returns debris objects that now simulate independently
    // The original unit is destroyed
    std::vector<DebrisObject> dismantleUnit(UnitInstance* unit);

    // Get all active debris
    [[nodiscard]] const std::vector<DebrisObject>& getDebris() const;

    // Clear all debris
    void clearDebris();

    //--------------------------------------------------------------------------
    // Update & Rendering
    //--------------------------------------------------------------------------

    // Update physics sync
    void update(float dt);

    // Apply a shader to all loaded models
    void applyShaderToModels(Shader shader);

    // Render all unit instances
    // heightOffsets: optional per-section height offsets (indexed by allSections order)
    void renderAll(const std::vector<float>* heightOffsets = nullptr);

    // Render all debris objects
    void renderDebris();

    // Render debug visualization (physics shapes)
    // heightOffsets: optional per-section height offsets (indexed by allSections order)
    void renderDebug(const std::vector<float>* heightOffsets = nullptr);

    // Render debug for debris
    void renderDebrisDebug();

private:
    b2WorldId m_worldId = b2_nullWorldId;

    // Base path for resolving model references within unit definitions
    std::string m_modelsBasePath;

    // Definition cache (id -> definition)
    std::unordered_map<std::string, std::unique_ptr<UnitDefinition>> m_definitions;

    // Active instances
    std::vector<std::unique_ptr<UnitInstance>> m_instances;

    // Active debris
    std::vector<DebrisObject> m_debris;

    // Collision group counter (decrements for each new unit, negative values)
    int32_t m_nextCollisionGroup = -1;

    //--------------------------------------------------------------------------
    // Internal Helpers
    //--------------------------------------------------------------------------

    // Recursively create section instances (rendering only, no physics)
    SectionInstance* createSectionInstance(
        const SectionDefinition& def,
        SectionInstance* parent,
        UnitInstance* unit
    );

    // Update transforms for a section hierarchy (code-based positioning)
    void updateSectionTransforms(
        SectionInstance* section,
        Vector2 parentWorldPos,
        float parentWorldRot
    );

    // Render a section hierarchy
    // parentHeight: accumulated height from parent sections (for relative stacking)
    void renderSection(SectionInstance* section, const std::vector<float>* heightOffsets,
                       const std::vector<SectionInstance*>& allSections, float parentHeight = 0.0f);

    // Render debug for a section hierarchy
    // parentHeight: accumulated height from parent sections (for relative stacking)
    void renderSectionDebug(SectionInstance* section, const std::vector<float>* heightOffsets,
                            const std::vector<SectionInstance*>& allSections, float parentHeight = 0.0f);

    // Helper to get section index in allSections vector
    int getSectionIndex(SectionInstance* section, const std::vector<SectionInstance*>& allSections);

    // Create a debris object from a section at given position/rotation
    // accumulatedHeight: the total height including all parent offsets
    DebrisObject createDebrisFromSection(
        const SectionDefinition& section,
        const Model& model,
        bool hasModel,
        Vector2 position,
        float rotation,
        float accumulatedHeight,
        b2Vec2 velocity,
        float angularVelocity
    );
};

#endif // UNIT_MANAGER_H
