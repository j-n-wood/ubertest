#ifndef RENDER_SCOPE_H
#define RENDER_SCOPE_H

#include "raylib.h"
#include "rlgl.h"

// RAII guard: disables depth-buffer WRITES (glDepthMask) for its lifetime and re-enables on scope
// exit — including early returns or exceptions. Depth TESTING is left on. Use around additive /
// transparent draw passes so overlapping effects blend together instead of occluding one another,
// while still being hidden behind opaque geometry drawn earlier in the frame.
//
// Note: this controls the depth MASK (write), not the depth TEST. For disabling the test entirely
// (e.g. always-on-top debug markers) use rlDisableDepthTest directly, or add a sibling guard.
class DisableDepthMaskScope {
public:
    DisableDepthMaskScope()  { rlDisableDepthMask(); }
    ~DisableDepthMaskScope() { rlEnableDepthMask(); }
    DisableDepthMaskScope(const DisableDepthMaskScope&) = delete;
    DisableDepthMaskScope& operator=(const DisableDepthMaskScope&) = delete;
    DisableDepthMaskScope(DisableDepthMaskScope&&) = delete;
    DisableDepthMaskScope& operator=(DisableDepthMaskScope&&) = delete;
};

// RAII guard: sets a blend mode (BeginBlendMode) for its lifetime and restores the default
// (EndBlendMode) on scope exit. Use around a transparent/additive draw pass.
class BlendModeScope {
public:
    explicit BlendModeScope(int mode) { BeginBlendMode(mode); }
    ~BlendModeScope() { EndBlendMode(); }
    BlendModeScope(const BlendModeScope&) = delete;
    BlendModeScope& operator=(const BlendModeScope&) = delete;
    BlendModeScope(BlendModeScope&&) = delete;
    BlendModeScope& operator=(BlendModeScope&&) = delete;
};

// RAII guard: disables backface culling for its lifetime (so a horizontal ground-plane quad is
// visible whichever way it's wound) and re-enables it on scope exit. NOTE: rlgl draws batched
// immediate-mode geometry lazily, so pair this with a RenderBatchFlushScope constructed AFTER it
// (destroyed before it) to flush the quads while culling is still disabled — otherwise they'd be
// drawn later, with culling back on, and silently culled.
class DisableBackfaceCullScope {
public:
    DisableBackfaceCullScope() { rlDisableBackfaceCulling(); }
    ~DisableBackfaceCullScope() { rlEnableBackfaceCulling(); }
    DisableBackfaceCullScope(const DisableBackfaceCullScope&) = delete;
    DisableBackfaceCullScope& operator=(const DisableBackfaceCullScope&) = delete;
    DisableBackfaceCullScope(DisableBackfaceCullScope&&) = delete;
    DisableBackfaceCullScope& operator=(DisableBackfaceCullScope&&) = delete;
};

// RAII guard: flushes the active rlgl render batch (rlDrawRenderBatchActive) on scope exit, so any
// immediate-mode geometry drawn in the block is submitted NOW — while the surrounding GL state
// (blend mode, depth mask, culling) set up by sibling guards is still in effect — rather than at the
// next lazy flush (EndMode3D). Construct it LAST among a pass's guards so it destroys FIRST.
class RenderBatchFlushScope {
public:
    RenderBatchFlushScope() = default;
    ~RenderBatchFlushScope() { rlDrawRenderBatchActive(); }
    RenderBatchFlushScope(const RenderBatchFlushScope&) = delete;
    RenderBatchFlushScope& operator=(const RenderBatchFlushScope&) = delete;
    RenderBatchFlushScope(RenderBatchFlushScope&&) = delete;
    RenderBatchFlushScope& operator=(RenderBatchFlushScope&&) = delete;
};

// RAII guard: binds a texture (rlSetTexture) for its lifetime and unbinds (rlSetTexture(0)) on exit.
class TextureBindScope {
public:
    explicit TextureBindScope(unsigned int textureId) { rlSetTexture(textureId); }
    ~TextureBindScope() { rlSetTexture(0); }
    TextureBindScope(const TextureBindScope&) = delete;
    TextureBindScope& operator=(const TextureBindScope&) = delete;
    TextureBindScope(TextureBindScope&&) = delete;
    TextureBindScope& operator=(TextureBindScope&&) = delete;
};

#endif // RENDER_SCOPE_H
