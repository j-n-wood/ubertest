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

static bool buildUnitRef(Viewer* viewer) {
    UnitRef* u = viewer->unitRef;

    b2WorldDef wd = b2DefaultWorldDef();
    wd.gravity = {0.0f, 0.0f};  // never stepped; gravity irrelevant
    u->world = b2CreateWorld(&wd);

    u->manager.setModelCache(&u->modelCache);
    u->manager.init(u->world, "assets/models");
    u->manager.preloadDefinitions("assets/units");

    // Spawn at the world origin, facing +forward. Not stepped, so it stays put.
    u->instance = u->manager.createInstance(kUnitId, Vector2{0.0f, 0.0f}, 0.0f);
    if (!u->instance) {
        TraceLog(LOG_WARNING, "UNITREF: could not create %s (definition missing?)", kUnitId);
        return false;
    }

    // Bind the lighting/env shader so the reference droid shades like it does in-game.
    u->manager.applyShaderToModels(sceneRendererGetShader(&viewer->renderer));
    u->built = true;
    TraceLog(LOG_INFO, "UNITREF: spawned %s at origin", kUnitId);
    return true;
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
