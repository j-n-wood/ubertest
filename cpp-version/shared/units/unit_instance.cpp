#include "unit_instance.h"

//------------------------------------------------------------------------------
// SectionInstance destructor - RAII cleanup of rendering resources
//------------------------------------------------------------------------------

SectionInstance::~SectionInstance() {
    // Children are destroyed automatically via unique_ptr

    // Unload animations
    if (animations && animCount > 0) {
        UnloadModelAnimations(animations, animCount);
        animations = nullptr;
        animCount = 0;
    }

    // Unload model
    if (hasModel) {
        UnloadModel(model);
        hasModel = false;
    }
}
