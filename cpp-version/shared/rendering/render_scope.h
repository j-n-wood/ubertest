#ifndef RENDER_SCOPE_H
#define RENDER_SCOPE_H

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

#endif // RENDER_SCOPE_H
