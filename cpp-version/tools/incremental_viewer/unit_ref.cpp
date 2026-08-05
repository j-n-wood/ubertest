// unit_ref.cpp — spawn a single class-14 droid via the real UnitManager as an on-screen size
// reference. Uses a tiny, un-stepped Box2D world (the unit just needs to sit still) and reuses the
// game's section assembly + env-mapped rendering.
#include "viewer.h"
#include "units/unit_manager.h"
#include "units/model_cache.h"
#include "units/unit_instance.h"

#include "box2d/box2d.h"

// Held behind an opaque pointer in Viewer to keep Box2D/unit deps out of viewer.h.
struct UnitRef {
    b2WorldId world = b2_nullWorldId;
    ModelCache modelCache;
    UnitManager manager;
    UnitInstance* instance = nullptr;
    bool built = false;
};

static const char* kUnitId = "droid_class_14";

// Grid midpoint in the unit's Box2D coords. renderAll maps a body's (x, y) to render (x, height, y)
// (unit_manager.cpp:761), so render X -> pos.x and render Z -> pos.y. The grid spans gridMin..gridMax
// in render X/Z, so its centre is simply the midpoint of those.
static b2Vec2 gridMidpoint(const Viewer* viewer) {
    return b2Vec2{
        (viewer->gridMin.x + viewer->gridMax.x) * 0.5f,
        (viewer->gridMin.z + viewer->gridMax.z) * 0.5f
    };
}

static bool buildUnitRef(Viewer* viewer) {
    UnitRef* u = viewer->unitRef;

    b2WorldDef wd = b2DefaultWorldDef();
    wd.gravity = {0.0f, 0.0f};  // never stepped; gravity irrelevant
    u->world = b2CreateWorld(&wd);

    u->manager.setModelCache(&u->modelCache);
    u->manager.init(u->world, "assets/models");
    u->manager.preloadDefinitions("assets/units");

    // Spawn at the grid midpoint (or origin if no deck is fitted yet — viewerUpdateUnitRefPosition
    // recentres it once a level loads). Not stepped, so it stays put.
    const b2Vec2 spawn = viewer->gridFit ? gridMidpoint(viewer) : b2Vec2{0.0f, 0.0f};
    u->instance = u->manager.createInstance(kUnitId, Vector2{spawn.x, spawn.y}, 0.0f);
    if (!u->instance) {
        TraceLog(LOG_WARNING, "UNITREF: could not create %s (definition missing?)", kUnitId);
        return false;
    }

    // Bind the lighting/env shader so the reference droid shades like it does in-game.
    u->manager.applyShaderToModels(sceneRendererGetShader(&viewer->renderer));
    u->built = true;
    TraceLog(LOG_INFO, "UNITREF: spawned %s at (%.1f, %.1f)", kUnitId, spawn.x, spawn.y);
    return true;
}

// Recentre the reference droid on the current grid midpoint. Called after each rebuild/deck change
// (the grid re-fits per level). The world is never stepped, so we move the body directly and run a
// zero-dt update() to refresh the cached section world transforms used by renderAll.
void viewerUpdateUnitRefPosition(Viewer* viewer) {
    if (!viewer || !viewer->unitRef || !viewer->unitRef->built || !viewer->gridFit) return;
    UnitRef* u = viewer->unitRef;
    if (!u->instance || !b2Body_IsValid(u->instance->bodyId)) return;
    const b2Vec2 mid = gridMidpoint(viewer);
    b2Body_SetTransform(u->instance->bodyId, mid, b2Body_GetRotation(u->instance->bodyId));
    u->manager.update(0.0f);
}

void viewerToggleUnitRef(Viewer* viewer) {
    if (!viewer) return;
    if (!viewer->unitRef) viewer->unitRef = new UnitRef();

    if (!viewer->unitRef->built) {
        if (!buildUnitRef(viewer)) {
            viewer->toggles.showUnitRef = false;
            return;
        }
    }
    viewer->toggles.showUnitRef = !viewer->toggles.showUnitRef;
    TraceLog(LOG_INFO, "VIEWER: unit reference %s", viewer->toggles.showUnitRef ? "ON" : "OFF");
}

void viewerRenderUnitRef(Viewer* viewer) {
    if (!viewer || !viewer->unitRef || !viewer->unitRef->built) return;
    viewer->unitRef->manager.renderAll();
}

void viewerDestroyUnitRef(Viewer* viewer) {
    if (!viewer || !viewer->unitRef) return;
    UnitRef* u = viewer->unitRef;
    const b2WorldId world = u->world;
    const bool built = u->built;
    // manager dtor frees unit bodies (world still valid); modelCache dtor UnloadModels (GL still up).
    delete u;
    if (built && b2World_IsValid(world)) b2DestroyWorld(world);
    viewer->unitRef = nullptr;
}
