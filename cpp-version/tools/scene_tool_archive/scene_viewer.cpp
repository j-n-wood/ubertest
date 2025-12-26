#include "scene_viewer.h"
#include "scene_json.h"
#include "rlgl.h"
#include "raymath.h"
#include <iostream>
#include <cmath>
#include <cfloat>
#include <algorithm>
#include <filesystem>
#include <iomanip>

namespace fs = std::filesystem;

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

static const float CAMERA_MOVE_SPEED = 500.0f;
static const float CAMERA_ZOOM_SPEED = 50.0f;
static const float CAMERA_ROTATE_SPEED = 2.0f;

//------------------------------------------------------------------------------
// Debug Logging
//------------------------------------------------------------------------------

#define LOG_INFO(msg) std::cout << "[INFO] " << msg << std::endl
#define LOG_DEBUG(msg) std::cout << "[DEBUG] " << msg << std::endl
#define LOG_WARN(msg) std::cout << "[WARN] " << msg << std::endl
#define LOG_ERROR(msg) std::cerr << "[ERROR] " << msg << std::endl

//------------------------------------------------------------------------------
// Mesh Generation
//------------------------------------------------------------------------------

static Model createTileMesh(const std::vector<Tile>& tiles, float scale, SceneDebugStats* stats) {
    LOG_DEBUG("createTileMesh: Starting with " << tiles.size() << " tiles, scale=" << scale);

    if (tiles.empty()) {
        LOG_WARN("createTileMesh: No tiles provided");
        return {0};
    }

    // Count total vertices (tiles are triangle strips, convert to triangles)
    int totalTriangles = 0;
    int tilesWithGeometry = 0;
    for (const auto& tile : tiles) {
        if (tile.vertices.size() >= 3) {
            // Triangle strip: n vertices = n-2 triangles
            totalTriangles += static_cast<int>(tile.vertices.size()) - 2;
            tilesWithGeometry++;
        }
    }

    LOG_DEBUG("createTileMesh: " << tilesWithGeometry << "/" << tiles.size()
              << " tiles have geometry, " << totalTriangles << " triangles total");

    if (totalTriangles == 0) {
        LOG_WARN("createTileMesh: No triangles to generate");
        return {0};
    }

    int vertexCount = totalTriangles * 3;

    // Update stats
    if (stats) {
        stats->totalTiles += static_cast<int>(tiles.size());
        stats->totalTriangles += totalTriangles;
        stats->totalVertices += vertexCount;
    }

    // Allocate mesh
    Mesh mesh = {0};
    mesh.vertexCount = vertexCount;
    mesh.triangleCount = totalTriangles;
    mesh.vertices = (float*)MemAlloc(vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float*)MemAlloc(vertexCount * 2 * sizeof(float));
    mesh.normals = (float*)MemAlloc(vertexCount * 3 * sizeof(float));
    mesh.colors = (unsigned char*)MemAlloc(vertexCount * 4 * sizeof(unsigned char));

    if (!mesh.vertices || !mesh.texcoords || !mesh.normals || !mesh.colors) {
        LOG_ERROR("createTileMesh: Failed to allocate mesh memory for " << vertexCount << " vertices");
        return {0};
    }

    int vi = 0;  // vertex index

    // Track bounds for debugging
    float minX = FLT_MAX, minY = FLT_MAX, minZ = FLT_MAX;
    float maxX = -FLT_MAX, maxY = -FLT_MAX, maxZ = -FLT_MAX;

    for (const auto& tile : tiles) {
        if (tile.vertices.size() < 3) continue;

        // Convert triangle strip to individual triangles
        // In a triangle strip: triangles are (0,1,2), (2,1,3), (2,3,4), (4,3,5)...
        for (size_t i = 0; i + 2 < tile.vertices.size(); ++i) {
            int i0, i1, i2;
            if (i % 2 == 0) {
                i0 = static_cast<int>(i);
                i1 = static_cast<int>(i + 1);
                i2 = static_cast<int>(i + 2);
            } else {
                // Flip winding for odd triangles
                i0 = static_cast<int>(i + 1);
                i1 = static_cast<int>(i);
                i2 = static_cast<int>(i + 2);
            }

            const auto& v0 = tile.vertices[i0];
            const auto& v1 = tile.vertices[i1];
            const auto& v2 = tile.vertices[i2];

            // Positions (swap Y and Z for 3D rendering: game Y -> 3D Z)
            // Apply scale factor
            float x0 = v0.position.x * scale;
            float y0 = v0.position.z * scale;  // Game Z (height) -> 3D Y
            float z0 = v0.position.y * scale;  // Game Y -> 3D Z

            mesh.vertices[vi * 3 + 0] = x0;
            mesh.vertices[vi * 3 + 1] = y0;
            mesh.vertices[vi * 3 + 2] = z0;

            minX = std::min(minX, x0); maxX = std::max(maxX, x0);
            minY = std::min(minY, y0); maxY = std::max(maxY, y0);
            minZ = std::min(minZ, z0); maxZ = std::max(maxZ, z0);
            vi++;

            float x1 = v1.position.x * scale;
            float y1 = v1.position.z * scale;
            float z1 = v1.position.y * scale;

            mesh.vertices[vi * 3 + 0] = x1;
            mesh.vertices[vi * 3 + 1] = y1;
            mesh.vertices[vi * 3 + 2] = z1;

            minX = std::min(minX, x1); maxX = std::max(maxX, x1);
            minY = std::min(minY, y1); maxY = std::max(maxY, y1);
            minZ = std::min(minZ, z1); maxZ = std::max(maxZ, z1);
            vi++;

            float x2 = v2.position.x * scale;
            float y2 = v2.position.z * scale;
            float z2 = v2.position.y * scale;

            mesh.vertices[vi * 3 + 0] = x2;
            mesh.vertices[vi * 3 + 1] = y2;
            mesh.vertices[vi * 3 + 2] = z2;

            minX = std::min(minX, x2); maxX = std::max(maxX, x2);
            minY = std::min(minY, y2); maxY = std::max(maxY, y2);
            minZ = std::min(minZ, z2); maxZ = std::max(maxZ, z2);
            vi++;
        }
    }

    LOG_DEBUG("createTileMesh: Mesh bounds X[" << minX << ", " << maxX
              << "] Y[" << minY << ", " << maxY << "] Z[" << minZ << ", " << maxZ << "]");

    // Fill in UVs, normals, and colors
    // NOTE: We need to compute normals from the actual vertex positions, not hardcode them
    vi = 0;
    int triIndex = 0;
    for (const auto& tile : tiles) {
        if (tile.vertices.size() < 3) continue;

        // Color from tile properties
        unsigned char r = static_cast<unsigned char>(tile.properties.diffuseColour.x * 255);
        unsigned char g = static_cast<unsigned char>(tile.properties.diffuseColour.y * 255);
        unsigned char b = static_cast<unsigned char>(tile.properties.diffuseColour.z * 255);

        for (size_t i = 0; i + 2 < tile.vertices.size(); ++i) {
            int i0, i1, i2;
            if (i % 2 == 0) {
                i0 = static_cast<int>(i);
                i1 = static_cast<int>(i + 1);
                i2 = static_cast<int>(i + 2);
            } else {
                i0 = static_cast<int>(i + 1);
                i1 = static_cast<int>(i);
                i2 = static_cast<int>(i + 2);
            }

            const auto& v0 = tile.vertices[i0];
            const auto& v1 = tile.vertices[i1];
            const auto& v2 = tile.vertices[i2];

            // Get the 3D positions (with coordinate transform applied)
            float scale = 1.0f;  // Scale already applied in first pass, use positions from mesh
            int baseVi = vi;  // Starting vertex index for this triangle

            // Read back the positions we stored in the first pass
            float x0 = mesh.vertices[baseVi * 3 + 0];
            float y0 = mesh.vertices[baseVi * 3 + 1];
            float z0 = mesh.vertices[baseVi * 3 + 2];
            float x1 = mesh.vertices[(baseVi + 1) * 3 + 0];
            float y1 = mesh.vertices[(baseVi + 1) * 3 + 1];
            float z1 = mesh.vertices[(baseVi + 1) * 3 + 2];
            float x2 = mesh.vertices[(baseVi + 2) * 3 + 0];
            float y2 = mesh.vertices[(baseVi + 2) * 3 + 1];
            float z2 = mesh.vertices[(baseVi + 2) * 3 + 2];

            // Compute triangle normal from cross product of edges
            Vector3 edge1 = {x1 - x0, y1 - y0, z1 - z0};
            Vector3 edge2 = {x2 - x0, y2 - y0, z2 - z0};
            Vector3 normal = Vector3CrossProduct(edge1, edge2);
            normal = Vector3Normalize(normal);

            // Log first few triangle normals for debugging
            if (triIndex < 3) {
                LOG_DEBUG("Triangle " << triIndex << " normal: (" << normal.x << ", " << normal.y << ", " << normal.z << ")");
                LOG_DEBUG("  v0: (" << x0 << ", " << y0 << ", " << z0 << ")");
                LOG_DEBUG("  v1: (" << x1 << ", " << y1 << ", " << z1 << ")");
                LOG_DEBUG("  v2: (" << x2 << ", " << y2 << ", " << z2 << ")");
            }

            // UVs and colors for vertex 0
            mesh.texcoords[vi * 2 + 0] = v0.uv1.x;
            mesh.texcoords[vi * 2 + 1] = v0.uv1.y;
            mesh.colors[vi * 4 + 0] = r;
            mesh.colors[vi * 4 + 1] = g;
            mesh.colors[vi * 4 + 2] = b;
            mesh.colors[vi * 4 + 3] = 255;
            mesh.normals[vi * 3 + 0] = normal.x;
            mesh.normals[vi * 3 + 1] = normal.y;
            mesh.normals[vi * 3 + 2] = normal.z;
            vi++;

            // UVs and colors for vertex 1
            mesh.texcoords[vi * 2 + 0] = v1.uv1.x;
            mesh.texcoords[vi * 2 + 1] = v1.uv1.y;
            mesh.colors[vi * 4 + 0] = r;
            mesh.colors[vi * 4 + 1] = g;
            mesh.colors[vi * 4 + 2] = b;
            mesh.colors[vi * 4 + 3] = 255;
            mesh.normals[vi * 3 + 0] = normal.x;
            mesh.normals[vi * 3 + 1] = normal.y;
            mesh.normals[vi * 3 + 2] = normal.z;
            vi++;

            // UVs and colors for vertex 2
            mesh.texcoords[vi * 2 + 0] = v2.uv1.x;
            mesh.texcoords[vi * 2 + 1] = v2.uv1.y;
            mesh.colors[vi * 4 + 0] = r;
            mesh.colors[vi * 4 + 1] = g;
            mesh.colors[vi * 4 + 2] = b;
            mesh.colors[vi * 4 + 3] = 255;
            mesh.normals[vi * 3 + 0] = normal.x;
            mesh.normals[vi * 3 + 1] = normal.y;
            mesh.normals[vi * 3 + 2] = normal.z;
            vi++;

            triIndex++;
        }
    }

    LOG_DEBUG("createTileMesh: Uploading mesh to GPU...");
    LOG_DEBUG("createTileMesh: Final vi count: " << vi << " (expected: " << vertexCount << ")");
    UploadMesh(&mesh, false);

    Model model = LoadModelFromMesh(mesh);
    if (model.meshCount > 0) {
        LOG_INFO("createTileMesh: SUCCESS - Created model with " << totalTriangles << " triangles, "
                 << vertexCount << " vertices");

        // Log first few vertex positions for debugging
        if (vertexCount >= 3) {
            LOG_DEBUG("createTileMesh: First triangle verts:");
            LOG_DEBUG("  v0: (" << mesh.vertices[0] << ", " << mesh.vertices[1] << ", " << mesh.vertices[2] << ")");
            LOG_DEBUG("  v1: (" << mesh.vertices[3] << ", " << mesh.vertices[4] << ", " << mesh.vertices[5] << ")");
            LOG_DEBUG("  v2: (" << mesh.vertices[6] << ", " << mesh.vertices[7] << ", " << mesh.vertices[8] << ")");
        }
    } else {
        LOG_ERROR("createTileMesh: Failed to create model from mesh");
    }

    return model;
}

//------------------------------------------------------------------------------
// Physics Collision Setup
//------------------------------------------------------------------------------

static void createCollisionBodies(SceneViewer* viewer, const Domain& domain, float scale) {
    LOG_DEBUG("createCollisionBodies: Starting physics setup, scale=" << scale);

    // Clear existing bodies
    int clearedBodies = 0;
    for (b2BodyId bodyId : viewer->staticBodies) {
        if (b2Body_IsValid(bodyId)) {
            b2DestroyBody(bodyId);
            clearedBodies++;
        }
    }
    viewer->staticBodies.clear();
    if (clearedBodies > 0) {
        LOG_DEBUG("createCollisionBodies: Cleared " << clearedBodies << " existing bodies");
    }

    int polygonCount = 0;
    int chainCount = 0;

    // Create collision bodies from areas
    for (const auto& area : domain.areas) {
        // Create polygons
        for (const auto& poly : area.collision.polygons) {
            if (poly.vertices.size() < 3) continue;

            b2BodyDef bodyDef = b2DefaultBodyDef();
            bodyDef.type = b2_staticBody;
            b2BodyId bodyId = b2CreateBody(viewer->worldId, &bodyDef);

            // Build hull from vertices (apply scale)
            std::vector<b2Vec2> b2Verts;
            for (const auto& v : poly.vertices) {
                b2Verts.push_back({v.x * scale, v.y * scale});
            }

            // Box2D requires convex hulls with max 8 vertices
            if (b2Verts.size() <= 8) {
                b2Hull hull = b2ComputeHull(b2Verts.data(), static_cast<int>(b2Verts.size()));
                if (hull.count >= 3) {
                    b2Polygon b2Poly = b2MakePolygon(&hull, 0);
                    b2ShapeDef shapeDef = b2DefaultShapeDef();
                    b2CreatePolygonShape(bodyId, &shapeDef, &b2Poly);
                    polygonCount++;
                }
            }

            viewer->staticBodies.push_back(bodyId);
        }

        // Create chain shapes for walls
        for (const auto& chain : area.collision.chains) {
            if (chain.vertices.size() < 2) continue;

            b2BodyDef bodyDef = b2DefaultBodyDef();
            bodyDef.type = b2_staticBody;
            b2BodyId bodyId = b2CreateBody(viewer->worldId, &bodyDef);

            std::vector<b2Vec2> b2Verts;
            for (const auto& v : chain.vertices) {
                b2Verts.push_back({v.x * scale, v.y * scale});
            }

            b2ChainDef chainDef = b2DefaultChainDef();
            chainDef.points = b2Verts.data();
            chainDef.count = static_cast<int>(b2Verts.size());
            chainDef.isLoop = chain.loop;

            b2CreateChain(bodyId, &chainDef);
            viewer->staticBodies.push_back(bodyId);
            chainCount++;
        }
    }

    // Update debug stats
    viewer->debugStats.collisionPolygons = polygonCount;
    viewer->debugStats.collisionChains = chainCount;

    // Create collision for doors
    int doorBodies = 0;
    for (const auto& door : domain.objects.doors) {
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_staticBody;
        bodyDef.position = {door.position.x * scale, door.position.y * scale};
        bodyDef.rotation = b2MakeRot(door.rotation.z);
        b2BodyId bodyId = b2CreateBody(viewer->worldId, &bodyDef);

        b2Polygon box = b2MakeBox(door.collision.halfExtents.x * scale,
                                   door.collision.halfExtents.y * scale);
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        b2CreatePolygonShape(bodyId, &shapeDef, &box);

        viewer->staticBodies.push_back(bodyId);
        doorBodies++;
    }

    // Create collision for solid features
    int featureBodies = 0;
    for (const auto& area : domain.areas) {
        for (const auto& feature : area.features) {
            if (!feature.flags.solid || !feature.collision) continue;

            b2BodyDef bodyDef = b2DefaultBodyDef();
            bodyDef.type = b2_staticBody;
            bodyDef.position = {feature.position.x * scale, feature.position.y * scale};
            bodyDef.rotation = b2MakeRot(feature.rotation.z);
            b2BodyId bodyId = b2CreateBody(viewer->worldId, &bodyDef);

            b2ShapeDef shapeDef = b2DefaultShapeDef();

            if (feature.collision->type == FeatureCollision::Type::Circle) {
                b2Circle circle = {{0, 0}, feature.collision->radius * scale};
                b2CreateCircleShape(bodyId, &shapeDef, &circle);
            } else {
                b2Polygon box = b2MakeBox(feature.collision->halfExtents.x * scale,
                                          feature.collision->halfExtents.y * scale);
                b2CreatePolygonShape(bodyId, &shapeDef, &box);
            }

            viewer->staticBodies.push_back(bodyId);
            featureBodies++;
        }
    }

    viewer->debugStats.physicsBodies = static_cast<int>(viewer->staticBodies.size());

    LOG_INFO("createCollisionBodies: Created " << viewer->staticBodies.size() << " bodies ("
             << polygonCount << " polygons, " << chainCount << " chains, "
             << doorBodies << " doors, " << featureBodies << " features)");
}

//------------------------------------------------------------------------------
// Mesh Management
//------------------------------------------------------------------------------

static void clearMeshes(SceneViewer* viewer) {
    LOG_DEBUG("clearMeshes: Unloading " << viewer->tileModels.size() << " tile models, "
              << viewer->featureModels.size() << " feature models");

    for (auto& model : viewer->tileModels) {
        if (model.meshCount > 0) {
            UnloadModel(model);
        }
    }
    viewer->tileModels.clear();

    for (auto& model : viewer->featureModels) {
        if (model.meshCount > 0) {
            UnloadModel(model);
        }
    }
    viewer->featureModels.clear();
}

static void generateMeshes(SceneViewer* viewer, const Domain& domain) {
    LOG_INFO("=== MESH GENERATION STARTED ===");
    LOG_INFO("generateMeshes: Domain '" << domain.name << "' (Level " << domain.levelNumber << ")");
    LOG_INFO("generateMeshes: " << domain.areas.size() << " areas to process");
    LOG_INFO("generateMeshes: Geometry scale factor: " << viewer->scaleConfig.geometryScale);

    clearMeshes(viewer);

    // Reset stats for this generation
    viewer->debugStats.reset();
    viewer->debugStats.areaCount = static_cast<int>(domain.areas.size());

    // Generate tile meshes for each area
    int areaIndex = 0;
    for (const auto& area : domain.areas) {
        LOG_DEBUG("generateMeshes: Processing area " << areaIndex << " with " << area.tiles.size() << " tiles");

        if (area.tiles.empty()) {
            LOG_WARN("generateMeshes: Area " << areaIndex << " has no tiles");
            viewer->debugStats.emptyAreas++;
        } else {
            Model tileMesh = createTileMesh(area.tiles, viewer->scaleConfig.geometryScale, &viewer->debugStats);
            if (tileMesh.meshCount > 0) {
                viewer->tileModels.push_back(tileMesh);
                viewer->debugStats.meshesGenerated++;
            } else {
                LOG_WARN("generateMeshes: Area " << areaIndex << " mesh creation failed");
            }
        }

        viewer->debugStats.featureCount += static_cast<int>(area.features.size());
        areaIndex++;
    }

    viewer->debugStats.waypointCount = static_cast<int>(domain.waypoints.size());

    // Store bounds
    viewer->debugStats.boundsMinX = domain.bounds.min.x;
    viewer->debugStats.boundsMinY = domain.bounds.min.y;
    viewer->debugStats.boundsMaxX = domain.bounds.max.x;
    viewer->debugStats.boundsMaxY = domain.bounds.max.y;

    LOG_INFO("=== MESH GENERATION COMPLETE ===");
    LOG_INFO("generateMeshes: Created " << viewer->debugStats.meshesGenerated << " meshes");
    LOG_INFO("generateMeshes: Total tiles: " << viewer->debugStats.totalTiles
             << ", triangles: " << viewer->debugStats.totalTriangles
             << ", vertices: " << viewer->debugStats.totalVertices);
    LOG_INFO("generateMeshes: Domain bounds: ("
             << domain.bounds.min.x << ", " << domain.bounds.min.y << ") to ("
             << domain.bounds.max.x << ", " << domain.bounds.max.y << ")");
}

//------------------------------------------------------------------------------
// Initialization / Cleanup
//------------------------------------------------------------------------------

bool sceneViewerInit(SceneViewer* viewer, const char* shaderPath) {
    LOG_INFO("=== SCENE VIEWER INITIALIZATION ===");
    LOG_INFO("sceneViewerInit: Shader path: " << (shaderPath ? shaderPath : "(null)"));

    *viewer = SceneViewer{};

    // Initialize renderer
    LOG_DEBUG("sceneViewerInit: Initializing scene renderer...");
    if (!sceneRendererInit(&viewer->renderer, shaderPath)) {
        LOG_ERROR("sceneViewerInit: Failed to initialize scene renderer");
        return false;
    }
    LOG_INFO("sceneViewerInit: Scene renderer initialized successfully");

    // Setup camera for top-down view (initial position, will be updated on domain load)
    // For top-down view looking down Y-axis, up vector should be along Z (not Y!)
    viewer->camera.position = {0.0f, 500.0f, 100.0f};  // Slight Z offset
    viewer->camera.target = {0.0f, 0.0f, 100.0f};
    viewer->camera.up = {0.0f, 0.0f, -1.0f};  // Up is negative Z for top-down
    viewer->camera.fovy = 60.0f;
    viewer->camera.projection = CAMERA_PERSPECTIVE;

    // Create physics world
    LOG_DEBUG("sceneViewerInit: Creating physics world...");
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = {0, 0};  // No gravity for top-down
    viewer->worldId = b2CreateWorld(&worldDef);

    if (!b2World_IsValid(viewer->worldId)) {
        LOG_ERROR("sceneViewerInit: Failed to create physics world");
        return false;
    }
    LOG_INFO("sceneViewerInit: Physics world created");

    // Default scale configuration
    viewer->scaleConfig = SceneScaleConfig{};  // Use default values
    LOG_INFO("sceneViewerInit: Scale config - geometry: " << viewer->scaleConfig.geometryScale
             << ", render: " << viewer->scaleConfig.renderScale);

    // Default display options
    viewer->showPhysics = true;
    viewer->showWaypoints = true;
    viewer->showObjects = true;
    viewer->showTiles = true;
    viewer->showFeatures = true;
    viewer->showGeometry = true;
    viewer->showHelp = false;
    viewer->showDebugStats = true;  // Show stats by default for debugging
    viewer->useShader = true;       // Use shader by default
    viewer->shaderDebugMode = 0;    // Normal rendering
    viewer->backfaceCulling = false; // OFF by default to see both sides of geometry

    viewer->currentDomainIndex = -1;
    viewer->domainLoaded = false;

    // Reference model
    viewer->referenceModel = {0};
    viewer->referenceModelLoaded = false;
    viewer->showReferenceModel = true;

    // Reset debug stats
    viewer->debugStats.reset();

    // Add default directional light
    // For directional lights: lightDir = normalize(target - position)
    // To light from above (positive Y axis in 3D), position should be above target
    // position={0, 1000, 0}, target={0, 0, 0} gives light direction (0, -1, 0) pointing DOWN
    LOG_DEBUG("sceneViewerInit: Adding directional light from above...");
    sceneRendererAddDirectionalLight(&viewer->renderer,
        {0, 1000, 0}, {0, 0, 0}, WHITE);

    LOG_INFO("=== SCENE VIEWER READY ===");
    return true;
}

void sceneViewerCleanup(SceneViewer* viewer) {
    clearMeshes(viewer);

    // Unload reference model if loaded
    if (viewer->referenceModelLoaded) {
        UnloadModel(viewer->referenceModel);
        viewer->referenceModelLoaded = false;
    }

    // Destroy physics bodies
    for (b2BodyId bodyId : viewer->staticBodies) {
        if (b2Body_IsValid(bodyId)) {
            b2DestroyBody(bodyId);
        }
    }
    viewer->staticBodies.clear();

    // Destroy physics world
    if (b2World_IsValid(viewer->worldId)) {
        b2DestroyWorld(viewer->worldId);
        viewer->worldId = b2_nullWorldId;
    }

    sceneRendererDestroy(&viewer->renderer);
}

//------------------------------------------------------------------------------
// Loading
//------------------------------------------------------------------------------

bool sceneViewerLoadShip(SceneViewer* viewer, const std::string& jsonPath) {
    LOG_INFO("=== LOADING SHIP ===");
    LOG_INFO("sceneViewerLoadShip: Path: " << jsonPath);

    if (!loadShipFromFile(jsonPath, viewer->ship)) {
        LOG_ERROR("sceneViewerLoadShip: Failed to load ship from: " << jsonPath);
        return false;
    }

    LOG_INFO("sceneViewerLoadShip: Ship '" << viewer->ship.name << "' loaded");
    LOG_INFO("sceneViewerLoadShip: " << viewer->ship.domainPaths.size() << " domain paths");

    // Store base path for domain resolution
    viewer->basePath = fs::path(jsonPath).parent_path().string();
    LOG_DEBUG("sceneViewerLoadShip: Base path set to: " << viewer->basePath);

    sceneViewerSetStatus(viewer, "Loaded ship: " + viewer->ship.name);

    // Load first domain if available
    if (!viewer->ship.domainPaths.empty()) {
        std::string firstDomainPath = (fs::path(viewer->basePath) / viewer->ship.domainPaths[0]).string();
        LOG_INFO("sceneViewerLoadShip: Loading first domain: " << firstDomainPath);
        return sceneViewerLoadDomain(viewer, firstDomainPath);
    } else {
        LOG_WARN("sceneViewerLoadShip: No domain paths in ship file");
    }

    return true;
}

bool sceneViewerLoadDomain(SceneViewer* viewer, const std::string& jsonPath) {
    LOG_INFO("=== LOADING DOMAIN ===");
    LOG_INFO("sceneViewerLoadDomain: Path: " << jsonPath);

    Domain domain;
    if (!loadDomainFromFile(jsonPath, domain)) {
        LOG_ERROR("sceneViewerLoadDomain: Failed to load domain from: " << jsonPath);
        return false;
    }

    LOG_INFO("sceneViewerLoadDomain: Domain '" << domain.name << "' (Level " << domain.levelNumber << ")");
    LOG_INFO("sceneViewerLoadDomain: " << domain.areas.size() << " areas, "
             << domain.waypoints.size() << " waypoints");
    LOG_INFO("sceneViewerLoadDomain: Bounds: (" << domain.bounds.min.x << ", " << domain.bounds.min.y
             << ") to (" << domain.bounds.max.x << ", " << domain.bounds.max.y << ")");

    // Store in ship's domains vector if not already there
    bool found = false;
    for (size_t i = 0; i < viewer->ship.domains.size(); ++i) {
        if (viewer->ship.domains[i].levelNumber == domain.levelNumber) {
            viewer->ship.domains[i] = std::move(domain);
            viewer->currentDomainIndex = static_cast<int>(i);
            found = true;
            LOG_DEBUG("sceneViewerLoadDomain: Updated existing domain at index " << i);
            break;
        }
    }

    if (!found) {
        viewer->currentDomainIndex = static_cast<int>(viewer->ship.domains.size());
        viewer->ship.domains.push_back(std::move(domain));
        LOG_DEBUG("sceneViewerLoadDomain: Added new domain at index " << viewer->currentDomainIndex);
    }

    const Domain& currentDomain = viewer->ship.domains[viewer->currentDomainIndex];

    // Generate meshes and physics
    generateMeshes(viewer, currentDomain);
    createCollisionBodies(viewer, currentDomain, viewer->scaleConfig.geometryScale);

    // Position camera at first waypoint if available, otherwise at domain center
    float scale = viewer->scaleConfig.geometryScale;
    float targetX, targetZ;
    float cameraHeight = 200.0f;  // Default height for good visibility

    if (!currentDomain.waypoints.empty()) {
        // Position at first waypoint (waypoint 0 or first in list)
        const auto& wp = currentDomain.waypoints[0];
        targetX = wp.position.x * scale;
        targetZ = wp.position.y * scale;  // Game Y -> 3D Z
        LOG_INFO("sceneViewerLoadDomain: Camera at waypoint 0: (" << wp.position.x << ", " << wp.position.y << ")");
    } else {
        // Fallback to domain center
        targetX = (currentDomain.bounds.min.x + currentDomain.bounds.max.x) / 2.0f * scale;
        targetZ = (currentDomain.bounds.min.y + currentDomain.bounds.max.y) / 2.0f * scale;
        LOG_INFO("sceneViewerLoadDomain: Camera at domain center");
    }

    LOG_INFO("sceneViewerLoadDomain: Camera setup - target: (" << targetX << ", " << targetZ
             << "), height: " << cameraHeight);

    // For top-down view, position camera above target looking down
    // Up vector should be along negative Z (pointing "forward" in screen)
    viewer->camera.position = {targetX, cameraHeight, targetZ + 10.0f};  // Slight Z offset
    viewer->camera.target = {targetX, 0.0f, targetZ + 10.0f};
    viewer->camera.up = {0.0f, 0.0f, -1.0f};

    viewer->domainLoaded = true;
    sceneViewerSetStatus(viewer, "Loaded domain: " + currentDomain.name +
                         " (Level " + std::to_string(currentDomain.levelNumber) + ")");

    // Print summary to console
    sceneViewerPrintStats(viewer);

    return true;
}

bool sceneViewerSwitchDomain(SceneViewer* viewer, int domainIndex) {
    LOG_INFO("sceneViewerSwitchDomain: Requested domain index " << domainIndex);

    if (domainIndex < 0 || domainIndex >= static_cast<int>(viewer->ship.domainPaths.size())) {
        LOG_WARN("sceneViewerSwitchDomain: Invalid domain index " << domainIndex
                 << " (have " << viewer->ship.domainPaths.size() << " domains)");
        return false;
    }

    // Check if domain is already loaded in memory
    for (size_t i = 0; i < viewer->ship.domains.size(); ++i) {
        if (viewer->ship.domains[i].levelNumber == domainIndex) {
            LOG_INFO("sceneViewerSwitchDomain: Domain already loaded at index " << i);
            viewer->currentDomainIndex = static_cast<int>(i);

            const Domain& domain = viewer->ship.domains[i];
            generateMeshes(viewer, domain);
            createCollisionBodies(viewer, domain, viewer->scaleConfig.geometryScale);

            // Update camera position
            float scale = viewer->scaleConfig.geometryScale;
            float centerX = (domain.bounds.min.x + domain.bounds.max.x) / 2.0f * scale;
            float centerZ = (domain.bounds.min.y + domain.bounds.max.y) / 2.0f * scale;
            float domainWidth = (domain.bounds.max.x - domain.bounds.min.x) * scale;
            float domainHeight = (domain.bounds.max.y - domain.bounds.min.y) * scale;
            float cameraHeight = std::max(domainWidth, domainHeight) * viewer->scaleConfig.cameraHeightFactor;
            cameraHeight = std::max(cameraHeight, 100.0f);

            viewer->camera.position = {centerX, cameraHeight, centerZ};
            viewer->camera.target = {centerX, 0.0f, centerZ};

            sceneViewerSetStatus(viewer, "Switched to: " + domain.name);
            sceneViewerPrintStats(viewer);
            return true;
        }
    }

    // Domain not loaded - load from file using base path
    if (viewer->basePath.empty()) {
        LOG_ERROR("sceneViewerSwitchDomain: No base path set - cannot resolve domain path");
        sceneViewerSetStatus(viewer, "Error: Cannot load domain (no base path)");
        return false;
    }

    std::string domainPath = (fs::path(viewer->basePath) / viewer->ship.domainPaths[domainIndex]).string();
    LOG_INFO("sceneViewerSwitchDomain: Loading domain from: " << domainPath);

    return sceneViewerLoadDomain(viewer, domainPath);
}

//------------------------------------------------------------------------------
// Update
//------------------------------------------------------------------------------

void sceneViewerUpdate(SceneViewer* viewer, float dt) {
    // Update status timer
    if (viewer->statusTimer > 0) {
        viewer->statusTimer -= dt;
        if (viewer->statusTimer <= 0) {
            viewer->statusMessage.clear();
        }
    }

    // Camera controls
    Vector3 moveDir = {0, 0, 0};

    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) moveDir.z -= 1;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) moveDir.z += 1;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) moveDir.x -= 1;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) moveDir.x += 1;
    if (IsKeyDown(KEY_Q)) moveDir.y += 1;
    if (IsKeyDown(KEY_E)) moveDir.y -= 1;

    float speed = CAMERA_MOVE_SPEED * dt;
    if (IsKeyDown(KEY_LEFT_SHIFT)) speed *= 2.0f;

    viewer->camera.position.x += moveDir.x * speed;
    viewer->camera.position.y += moveDir.y * speed;
    viewer->camera.position.z += moveDir.z * speed;
    viewer->camera.target.x += moveDir.x * speed;
    viewer->camera.target.z += moveDir.z * speed;

    // Mouse wheel zoom
    float wheel = GetMouseWheelMove();
    if (wheel != 0) {
        viewer->camera.position.y -= wheel * CAMERA_ZOOM_SPEED;
        viewer->camera.position.y = std::max(100.0f, viewer->camera.position.y);
    }

    // Toggle keys
    if (IsKeyPressed(KEY_F1)) {
        viewer->showPhysics = !viewer->showPhysics;
        sceneViewerSetStatus(viewer, viewer->showPhysics ? "Physics: ON" : "Physics: OFF");
    }
    if (IsKeyPressed(KEY_F2)) {
        viewer->showWaypoints = !viewer->showWaypoints;
        sceneViewerSetStatus(viewer, viewer->showWaypoints ? "Waypoints: ON" : "Waypoints: OFF");
    }
    if (IsKeyPressed(KEY_F3)) {
        viewer->showObjects = !viewer->showObjects;
        sceneViewerSetStatus(viewer, viewer->showObjects ? "Objects: ON" : "Objects: OFF");
    }
    if (IsKeyPressed(KEY_F4)) {
        viewer->showTiles = !viewer->showTiles;
        sceneViewerSetStatus(viewer, viewer->showTiles ? "Tiles: ON" : "Tiles: OFF");
    }
    if (IsKeyPressed(KEY_F5)) {
        viewer->showDebugStats = !viewer->showDebugStats;
        sceneViewerSetStatus(viewer, viewer->showDebugStats ? "Debug Stats: ON" : "Debug Stats: OFF");
    }
    if (IsKeyPressed(KEY_F6)) {
        viewer->useShader = !viewer->useShader;
        LOG_INFO("Shader rendering: " << (viewer->useShader ? "ON" : "OFF"));
        sceneViewerSetStatus(viewer, viewer->useShader ? "Shader: ON" : "Shader: OFF (Debug)");
    }
    if (IsKeyPressed(KEY_F7)) {
        viewer->backfaceCulling = !viewer->backfaceCulling;
        LOG_INFO("Backface culling: " << (viewer->backfaceCulling ? "ON" : "OFF"));
        sceneViewerSetStatus(viewer, viewer->backfaceCulling ? "Backface culling: ON" : "Backface culling: OFF");
    }
    if (IsKeyPressed(KEY_F8)) {
        viewer->showReferenceModel = !viewer->showReferenceModel;
        LOG_INFO("Reference model: " << (viewer->showReferenceModel ? "ON" : "OFF"));
        sceneViewerSetStatus(viewer, viewer->showReferenceModel ? "Reference model: ON" : "Reference model: OFF");
    }
    if (IsKeyPressed(KEY_H)) {
        viewer->showHelp = !viewer->showHelp;
    }

    // Shader debug modes 0-5 (like model_tool)
    if (IsKeyPressed(KEY_ZERO) || IsKeyPressed(KEY_KP_0)) {
        viewer->shaderDebugMode = 0;
        sceneRendererSetDebugMode(&viewer->renderer, 0);
        sceneViewerSetStatus(viewer, "Debug mode: 0 (Normal)");
    }
    if (IsKeyPressed(KEY_ONE) && !IsKeyDown(KEY_LEFT_CONTROL)) {
        // Only if not Ctrl+number (domain switching uses number keys)
        viewer->shaderDebugMode = 1;
        sceneRendererSetDebugMode(&viewer->renderer, 1);
        sceneViewerSetStatus(viewer, "Debug mode: 1 (Diffuse)");
    }
    if (IsKeyPressed(KEY_TWO) && !IsKeyDown(KEY_LEFT_CONTROL)) {
        viewer->shaderDebugMode = 2;
        sceneRendererSetDebugMode(&viewer->renderer, 2);
        sceneViewerSetStatus(viewer, "Debug mode: 2 (Normals)");
    }
    if (IsKeyPressed(KEY_THREE) && !IsKeyDown(KEY_LEFT_CONTROL)) {
        viewer->shaderDebugMode = 3;
        sceneRendererSetDebugMode(&viewer->renderer, 3);
        sceneViewerSetStatus(viewer, "Debug mode: 3 (Specular)");
    }
    if (IsKeyPressed(KEY_FOUR) && !IsKeyDown(KEY_LEFT_CONTROL)) {
        viewer->shaderDebugMode = 4;
        sceneRendererSetDebugMode(&viewer->renderer, 4);
        sceneViewerSetStatus(viewer, "Debug mode: 4 (Lighting)");
    }
    if (IsKeyPressed(KEY_FIVE) && !IsKeyDown(KEY_LEFT_CONTROL)) {
        viewer->shaderDebugMode = 5;
        sceneRendererSetDebugMode(&viewer->renderer, 5);
        sceneViewerSetStatus(viewer, "Debug mode: 5 (UV)");
    }

    // Scale adjustment ([ and ] keys)
    if (IsKeyPressed(KEY_LEFT_BRACKET)) {
        viewer->scaleConfig.geometryScale *= 0.5f;
        LOG_INFO("Scale decreased to: " << viewer->scaleConfig.geometryScale);
        sceneViewerSetStatus(viewer, "Scale: " + std::to_string(viewer->scaleConfig.geometryScale));
        // Regenerate meshes with new scale
        if (viewer->domainLoaded && viewer->currentDomainIndex >= 0) {
            const Domain& domain = viewer->ship.domains[viewer->currentDomainIndex];
            generateMeshes(viewer, domain);
            createCollisionBodies(viewer, domain, viewer->scaleConfig.geometryScale);
        }
    }
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) {
        viewer->scaleConfig.geometryScale *= 2.0f;
        LOG_INFO("Scale increased to: " << viewer->scaleConfig.geometryScale);
        sceneViewerSetStatus(viewer, "Scale: " + std::to_string(viewer->scaleConfig.geometryScale));
        // Regenerate meshes with new scale
        if (viewer->domainLoaded && viewer->currentDomainIndex >= 0) {
            const Domain& domain = viewer->ship.domains[viewer->currentDomainIndex];
            generateMeshes(viewer, domain);
            createCollisionBodies(viewer, domain, viewer->scaleConfig.geometryScale);
        }
    }

    // R key to reset camera to center of domain
    if (IsKeyPressed(KEY_R) && viewer->domainLoaded && viewer->currentDomainIndex >= 0) {
        const Domain& domain = viewer->ship.domains[viewer->currentDomainIndex];
        float scale = viewer->scaleConfig.geometryScale;
        float centerX = (domain.bounds.min.x + domain.bounds.max.x) / 2.0f * scale;
        float centerZ = (domain.bounds.min.y + domain.bounds.max.y) / 2.0f * scale;
        float domainWidth = (domain.bounds.max.x - domain.bounds.min.x) * scale;
        float domainHeight = (domain.bounds.max.y - domain.bounds.min.y) * scale;
        float cameraHeight = std::max(domainWidth, domainHeight) * viewer->scaleConfig.cameraHeightFactor;
        cameraHeight = std::max(cameraHeight, 100.0f);

        viewer->camera.position = {centerX, cameraHeight, centerZ + 10.0f};
        viewer->camera.target = {centerX, 0.0f, centerZ + 10.0f};
        viewer->camera.up = {0.0f, 0.0f, -1.0f};

        LOG_INFO("Camera reset - pos: (" << centerX << ", " << cameraHeight << ", " << centerZ << ")");
        sceneViewerSetStatus(viewer, "Camera reset to domain center");
    }

    // C key to set camera to a close test position (above origin)
    if (IsKeyPressed(KEY_C)) {
        viewer->camera.position = {100.0f, 200.0f, 110.0f};
        viewer->camera.target = {100.0f, 0.0f, 110.0f};
        viewer->camera.up = {0.0f, 0.0f, -1.0f};
        LOG_INFO("Camera set to test position (100, 200, 110)");
        sceneViewerSetStatus(viewer, "Camera: test position near origin");
    }

    // Domain switching
    if (IsKeyPressed(KEY_TAB)) {
        if (IsKeyDown(KEY_LEFT_SHIFT)) {
            // Previous domain
            if (viewer->currentDomainIndex > 0) {
                sceneViewerSwitchDomain(viewer, viewer->currentDomainIndex - 1);
            }
        } else {
            // Next domain
            sceneViewerSwitchDomain(viewer, viewer->currentDomainIndex + 1);
        }
    }

    // Ctrl+Number keys for quick domain access (0-9 plain are for shader debug modes)
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
        for (int i = 0; i <= 9; ++i) {
            if (IsKeyPressed(KEY_ZERO + i)) {
                sceneViewerSwitchDomain(viewer, i);
            }
        }
    }
}

//------------------------------------------------------------------------------
// Rendering
//------------------------------------------------------------------------------

static void drawPhysicsDebug(SceneViewer* viewer) {
    if (!viewer->domainLoaded) return;

    const Domain& domain = viewer->ship.domains[viewer->currentDomainIndex];
    float scale = viewer->scaleConfig.geometryScale;
    float debugHeight = viewer->scaleConfig.debugDrawHeight * scale;

    // Draw collision polygons
    for (const auto& area : domain.areas) {
        // Polygons
        for (const auto& poly : area.collision.polygons) {
            if (poly.vertices.size() < 3) continue;

            for (size_t i = 0; i < poly.vertices.size(); ++i) {
                const auto& v1 = poly.vertices[i];
                const auto& v2 = poly.vertices[(i + 1) % poly.vertices.size()];

                // Convert to 3D (Y is up in rendering, apply scale)
                Vector3 p1 = {v1.x * scale, debugHeight, v1.y * scale};
                Vector3 p2 = {v2.x * scale, debugHeight, v2.y * scale};

                DrawLine3D(p1, p2, BLUE);
            }
        }

        // Chains
        for (const auto& chain : area.collision.chains) {
            for (size_t i = 0; i + 1 < chain.vertices.size(); ++i) {
                const auto& v1 = chain.vertices[i];
                const auto& v2 = chain.vertices[i + 1];

                Vector3 p1 = {v1.x * scale, debugHeight, v1.y * scale};
                Vector3 p2 = {v2.x * scale, debugHeight, v2.y * scale};

                DrawLine3D(p1, p2, GREEN);
            }

            // Close loop if needed
            if (chain.loop && chain.vertices.size() >= 2) {
                const auto& v1 = chain.vertices.back();
                const auto& v2 = chain.vertices.front();

                Vector3 p1 = {v1.x * scale, debugHeight, v1.y * scale};
                Vector3 p2 = {v2.x * scale, debugHeight, v2.y * scale};

                DrawLine3D(p1, p2, GREEN);
            }
        }
    }
}

static void drawWaypoints(SceneViewer* viewer) {
    if (!viewer->domainLoaded) return;

    const Domain& domain = viewer->ship.domains[viewer->currentDomainIndex];
    float scale = viewer->scaleConfig.geometryScale;
    float wpHeight = viewer->scaleConfig.waypointHeight * scale;
    float wpRadius = viewer->scaleConfig.waypointRadius * scale;

    for (const auto& wp : domain.waypoints) {
        Vector3 pos = {wp.position.x * scale, wpHeight, wp.position.y * scale};

        // Color based on flags
        Color color = GRAY;
        if (wp.flags.start) color = GREEN;
        else if (wp.flags.console) color = YELLOW;
        else if (wp.flags.recharge) color = BLUE;
        else if (wp.flags.lift) color = PURPLE;
        else if (wp.flags.transmat) color = ORANGE;

        DrawSphere(pos, wpRadius, color);

        // Draw connections to neighbors
        for (int neighborId : wp.neighbors) {
            if (neighborId == 0) continue;

            // Find neighbor waypoint
            for (const auto& neighbor : domain.waypoints) {
                if (neighbor.id == neighborId) {
                    Vector3 neighborPos = {neighbor.position.x * scale, wpHeight, neighbor.position.y * scale};
                    DrawLine3D(pos, neighborPos, Fade(color, 0.5f));
                    break;
                }
            }
        }
    }
}

static void drawObjects(SceneViewer* viewer) {
    if (!viewer->domainLoaded) return;

    const Domain& domain = viewer->ship.domains[viewer->currentDomainIndex];
    float scale = viewer->scaleConfig.geometryScale;

    // Draw doors
    for (const auto& door : domain.objects.doors) {
        Vector3 pos = {door.position.x * scale, 15.0f * scale, door.position.y * scale};
        Vector3 size = {door.size.x * scale, 30.0f * scale, door.size.y * scale};

        // Rotate the cube (simplified - just draw at position)
        DrawCubeWires(pos, size.x, size.y, size.z, RED);
    }

    // Draw consoles
    for (const auto& console : domain.objects.consoles) {
        Vector3 pos = {console.position.x * scale, 15.0f * scale, console.position.y * scale};
        DrawCube(pos, 20.0f * scale, 30.0f * scale, 20.0f * scale, YELLOW);
    }

    // Draw chargers
    for (const auto& charger : domain.objects.chargers) {
        Vector3 pos = {charger.position.x * scale, 15.0f * scale, charger.position.y * scale};
        DrawCube(pos, 20.0f * scale, 40.0f * scale, 20.0f * scale, SKYBLUE);
    }
}

static void drawFeatures(SceneViewer* viewer) {
    if (!viewer->domainLoaded) return;

    const Domain& domain = viewer->ship.domains[viewer->currentDomainIndex];
    float scale = viewer->scaleConfig.geometryScale;

    for (const auto& area : domain.areas) {
        for (const auto& feature : area.features) {
            Vector3 pos = {feature.position.x * scale, feature.position.z * scale, feature.position.y * scale};

            Color color = DARKGRAY;
            if (feature.flags.fullbright) color = WHITE;

            // Draw as cube placeholder
            float cubeSize = 32.0f * scale;
            DrawCube(pos, cubeSize, cubeSize, cubeSize, color);
            DrawCubeWires(pos, cubeSize, cubeSize, cubeSize, BLACK);
        }
    }
}

void sceneViewerRender(SceneViewer* viewer) {
    // Update camera position for shader specular calculations
    sceneRendererUpdateCamera(&viewer->renderer, viewer->camera.position);

    // Set backface culling based on toggle
    if (viewer->backfaceCulling) {
        rlEnableBackfaceCulling();
    } else {
        rlDisableBackfaceCulling();
    }

    BeginMode3D(viewer->camera);

    // Set far plane to very large value to prevent clipping
    rlSetClipPlanes(0.1, 100000.0);

    // Draw ground plane reference (scale the grid to match geometry scale)
    float gridSize = 64.0f * viewer->scaleConfig.geometryScale;
    DrawGrid(100, gridSize);

    // Draw test cubes at origin and at camera target for reference
    DrawCube({0, 10, 0}, 20.0f, 20.0f, 20.0f, RED);  // Red cube at origin
    DrawCubeWires({0, 10, 0}, 20.0f, 20.0f, 20.0f, WHITE);

    // Draw a cube at camera target position
    DrawCube(viewer->camera.target, 15.0f, 15.0f, 15.0f, MAGENTA);
    DrawCubeWires(viewer->camera.target, 15.0f, 15.0f, 15.0f, WHITE);

    // Draw reference model if loaded (for comparison with tile rendering)
    if (viewer->referenceModelLoaded && viewer->showReferenceModel) {
        if (viewer->useShader) {
            sceneRendererApplyShader(&viewer->renderer, &viewer->referenceModel);
        }
        // Draw at a position offset from origin for comparison
        DrawModel(viewer->referenceModel, {100, 0, 100}, 1.0f, WHITE);
    }

    // Draw tile meshes (already scaled during generation)
    if (viewer->showTiles) {
        for (auto& model : viewer->tileModels) {
            if (model.meshCount > 0) {
                if (viewer->useShader) {
                    sceneRendererApplyShader(&viewer->renderer, &model);
                }
                DrawModel(model, {0, 0, 0}, viewer->scaleConfig.renderScale, WHITE);
            }
        }
    }

    // Draw features
    if (viewer->showFeatures) {
        drawFeatures(viewer);
    }

    // Draw physics debug
    if (viewer->showPhysics) {
        drawPhysicsDebug(viewer);
    }

    // Draw waypoints
    if (viewer->showWaypoints) {
        drawWaypoints(viewer);
    }

    // Draw objects
    if (viewer->showObjects) {
        drawObjects(viewer);
    }

    EndMode3D();
}

//------------------------------------------------------------------------------
// Overlay
//------------------------------------------------------------------------------

void sceneViewerDrawOverlay(SceneViewer* viewer) {
    int y = 10;

    // Title
    if (viewer->domainLoaded && viewer->currentDomainIndex >= 0 &&
        viewer->currentDomainIndex < static_cast<int>(viewer->ship.domains.size())) {
        const Domain& domain = viewer->ship.domains[viewer->currentDomainIndex];
        std::string title = viewer->ship.name + " - " + domain.name +
                           " (Level " + std::to_string(domain.levelNumber) + ")";
        DrawText(title.c_str(), 10, y, 20, WHITE);
        y += 25;
    }

    // Status message
    if (!viewer->statusMessage.empty()) {
        DrawText(viewer->statusMessage.c_str(), 10, y, 18, YELLOW);
        y += 22;
    }

    // Help toggle hint
    DrawText("Press H for help, F5 for debug stats", 10, GetScreenHeight() - 25, 16, GRAY);

    // Help panel
    if (viewer->showHelp) {
        int helpX = GetScreenWidth() - 300;
        int helpY = 10;
        int helpW = 290;
        int helpH = 440;

        DrawRectangle(helpX, helpY, helpW, helpH, Fade(BLACK, 0.8f));
        DrawRectangleLines(helpX, helpY, helpW, helpH, WHITE);

        helpX += 10;
        helpY += 10;

        DrawText("Controls:", helpX, helpY, 18, WHITE); helpY += 25;
        DrawText("WASD/Arrows - Move camera", helpX, helpY, 14, LIGHTGRAY); helpY += 18;
        DrawText("Q/E - Camera up/down", helpX, helpY, 14, LIGHTGRAY); helpY += 18;
        DrawText("Mouse wheel - Zoom", helpX, helpY, 14, LIGHTGRAY); helpY += 18;
        DrawText("Shift - Move faster", helpX, helpY, 14, LIGHTGRAY); helpY += 18;
        DrawText("R - Reset camera", helpX, helpY, 14, LIGHTGRAY); helpY += 18;
        DrawText("C - Test camera position", helpX, helpY, 14, LIGHTGRAY); helpY += 25;

        DrawText("Display:", helpX, helpY, 18, WHITE); helpY += 25;
        DrawText("F1 - Toggle physics", helpX, helpY, 14, LIGHTGRAY); helpY += 18;
        DrawText("F2 - Toggle waypoints", helpX, helpY, 14, LIGHTGRAY); helpY += 18;
        DrawText("F3 - Toggle objects", helpX, helpY, 14, LIGHTGRAY); helpY += 18;
        DrawText("F4 - Toggle tiles", helpX, helpY, 14, LIGHTGRAY); helpY += 18;
        DrawText("F5 - Toggle debug stats", helpX, helpY, 14, LIGHTGRAY); helpY += 18;
        DrawText("F6 - Toggle shader (debug)", helpX, helpY, 14, LIGHTGRAY); helpY += 18;
        DrawText("F7 - Toggle backface culling", helpX, helpY, 14, LIGHTGRAY); helpY += 18;
        DrawText("F8 - Toggle reference model", helpX, helpY, 14, LIGHTGRAY); helpY += 18;
        DrawText("0-5 - Shader debug modes", helpX, helpY, 14, LIGHTGRAY); helpY += 18;
        DrawText("[ / ] - Decrease/Increase scale", helpX, helpY, 14, LIGHTGRAY); helpY += 25;

        DrawText("Navigation:", helpX, helpY, 18, WHITE); helpY += 25;
        DrawText("Tab - Next domain", helpX, helpY, 14, LIGHTGRAY); helpY += 18;
        DrawText("Shift+Tab - Prev domain", helpX, helpY, 14, LIGHTGRAY); helpY += 18;
        DrawText("Ctrl+0-9 - Jump to domain", helpX, helpY, 14, LIGHTGRAY);
    }

    // Debug stats panel
    if (viewer->showDebugStats && viewer->domainLoaded) {
        int statsX = 10;
        int statsY = 60;
        int statsW = 320;
        int statsH = 220;

        DrawRectangle(statsX, statsY, statsW, statsH, Fade(BLACK, 0.7f));
        DrawRectangleLines(statsX, statsY, statsW, statsH, GREEN);

        statsX += 10;
        statsY += 10;

        DrawText("Debug Statistics", statsX, statsY, 16, GREEN); statsY += 22;

        char buf[128];

        snprintf(buf, sizeof(buf), "Scale: %.4f (geometry) x %.4f (render)",
                 viewer->scaleConfig.geometryScale, viewer->scaleConfig.renderScale);
        DrawText(buf, statsX, statsY, 12, WHITE); statsY += 16;

        snprintf(buf, sizeof(buf), "Shader: %s, Debug mode: %d, Cull: %s",
                 viewer->useShader ? "ON" : "OFF", viewer->shaderDebugMode,
                 viewer->backfaceCulling ? "ON" : "OFF");
        DrawText(buf, statsX, statsY, 12, viewer->useShader ? WHITE : YELLOW); statsY += 16;

        snprintf(buf, sizeof(buf), "Meshes: %d generated, %d empty areas",
                 viewer->debugStats.meshesGenerated, viewer->debugStats.emptyAreas);
        DrawText(buf, statsX, statsY, 12, WHITE); statsY += 16;

        snprintf(buf, sizeof(buf), "Geometry: %d tiles, %d tris, %d verts",
                 viewer->debugStats.totalTiles, viewer->debugStats.totalTriangles, viewer->debugStats.totalVertices);
        DrawText(buf, statsX, statsY, 12, WHITE); statsY += 16;

        snprintf(buf, sizeof(buf), "Physics: %d bodies (%d poly, %d chain)",
                 viewer->debugStats.physicsBodies, viewer->debugStats.collisionPolygons, viewer->debugStats.collisionChains);
        DrawText(buf, statsX, statsY, 12, WHITE); statsY += 16;

        snprintf(buf, sizeof(buf), "Domain: %d areas, %d features, %d waypoints",
                 viewer->debugStats.areaCount, viewer->debugStats.featureCount, viewer->debugStats.waypointCount);
        DrawText(buf, statsX, statsY, 12, WHITE); statsY += 16;

        snprintf(buf, sizeof(buf), "Bounds: (%.1f, %.1f) to (%.1f, %.1f)",
                 viewer->debugStats.boundsMinX, viewer->debugStats.boundsMinY,
                 viewer->debugStats.boundsMaxX, viewer->debugStats.boundsMaxY);
        DrawText(buf, statsX, statsY, 12, WHITE); statsY += 16;

        // Scaled bounds
        float scale = viewer->scaleConfig.geometryScale;
        snprintf(buf, sizeof(buf), "Scaled: (%.1f, %.1f) to (%.1f, %.1f)",
                 viewer->debugStats.boundsMinX * scale, viewer->debugStats.boundsMinY * scale,
                 viewer->debugStats.boundsMaxX * scale, viewer->debugStats.boundsMaxY * scale);
        DrawText(buf, statsX, statsY, 12, YELLOW); statsY += 16;

        // Camera position
        snprintf(buf, sizeof(buf), "Camera: (%.1f, %.1f, %.1f)",
                 viewer->camera.position.x, viewer->camera.position.y, viewer->camera.position.z);
        DrawText(buf, statsX, statsY, 12, SKYBLUE);
    }

    // Bottom stats bar
    if (viewer->domainLoaded && viewer->currentDomainIndex >= 0) {
        const Domain& domain = viewer->ship.domains[viewer->currentDomainIndex];

        int statY = GetScreenHeight() - 50;

        char stats[256];
        snprintf(stats, sizeof(stats), "Areas: %zu  Tiles: %d  Features: %d  Waypoints: %zu  Bodies: %zu  Models: %zu",
                 domain.areas.size(), viewer->debugStats.totalTiles, viewer->debugStats.featureCount,
                 domain.waypoints.size(), viewer->staticBodies.size(), viewer->tileModels.size());
        DrawText(stats, 10, statY, 14, GRAY);
    }
}

void sceneViewerSetStatus(SceneViewer* viewer, const std::string& message, float duration) {
    viewer->statusMessage = message;
    viewer->statusTimer = duration;
}

void sceneViewerSetScale(SceneViewer* viewer, float geometryScale, float renderScale) {
    LOG_INFO("sceneViewerSetScale: geometry=" << geometryScale << ", render=" << renderScale);
    viewer->scaleConfig.geometryScale = geometryScale;
    viewer->scaleConfig.renderScale = renderScale;

    // Regenerate if domain is loaded
    if (viewer->domainLoaded && viewer->currentDomainIndex >= 0) {
        const Domain& domain = viewer->ship.domains[viewer->currentDomainIndex];
        generateMeshes(viewer, domain);
        createCollisionBodies(viewer, domain, geometryScale);
    }
}

void sceneViewerPrintStats(SceneViewer* viewer) {
    LOG_INFO("=== DOMAIN RENDER STATISTICS ===");
    LOG_INFO("  Scale: geometry=" << viewer->scaleConfig.geometryScale
             << ", render=" << viewer->scaleConfig.renderScale);
    LOG_INFO("  Meshes generated: " << viewer->debugStats.meshesGenerated);
    LOG_INFO("  Empty areas: " << viewer->debugStats.emptyAreas);
    LOG_INFO("  Total tiles: " << viewer->debugStats.totalTiles);
    LOG_INFO("  Total triangles: " << viewer->debugStats.totalTriangles);
    LOG_INFO("  Total vertices: " << viewer->debugStats.totalVertices);
    LOG_INFO("  Physics bodies: " << viewer->debugStats.physicsBodies
             << " (" << viewer->debugStats.collisionPolygons << " poly, "
             << viewer->debugStats.collisionChains << " chain)");
    LOG_INFO("  Domain: " << viewer->debugStats.areaCount << " areas, "
             << viewer->debugStats.featureCount << " features, "
             << viewer->debugStats.waypointCount << " waypoints");
    LOG_INFO("  Bounds: (" << viewer->debugStats.boundsMinX << ", " << viewer->debugStats.boundsMinY
             << ") to (" << viewer->debugStats.boundsMaxX << ", " << viewer->debugStats.boundsMaxY << ")");
    LOG_INFO("  Camera: (" << viewer->camera.position.x << ", " << viewer->camera.position.y
             << ", " << viewer->camera.position.z << ")");
    if (viewer->referenceModelLoaded) {
        LOG_INFO("  Reference model: LOADED");
    }
    LOG_INFO("================================");
}

bool sceneViewerLoadReferenceModel(SceneViewer* viewer, const std::string& modelPath) {
    LOG_INFO("=== LOADING REFERENCE MODEL ===");
    LOG_INFO("sceneViewerLoadReferenceModel: Path: " << modelPath);

    // Unload existing reference model if any
    if (viewer->referenceModelLoaded) {
        UnloadModel(viewer->referenceModel);
        viewer->referenceModelLoaded = false;
    }

    // Load the model (Raylib supports GLTF, OBJ, etc.)
    viewer->referenceModel = LoadModel(modelPath.c_str());

    if (viewer->referenceModel.meshCount == 0) {
        LOG_ERROR("sceneViewerLoadReferenceModel: Failed to load model");
        return false;
    }

    viewer->referenceModelLoaded = true;

    // Log model info
    LOG_INFO("sceneViewerLoadReferenceModel: Loaded model with "
             << viewer->referenceModel.meshCount << " meshes, "
             << viewer->referenceModel.materialCount << " materials");

    // Apply the lighting shader to the model
    sceneRendererApplyShader(&viewer->renderer, &viewer->referenceModel);

    sceneViewerSetStatus(viewer, "Loaded reference model: " + modelPath);

    LOG_INFO("=== REFERENCE MODEL READY ===");
    return true;
}
