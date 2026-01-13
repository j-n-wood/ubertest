#include "unit_instance.h"

//------------------------------------------------------------------------------
// SectionInstance destructor - RAII cleanup of rendering resources
//------------------------------------------------------------------------------

SectionInstance::~SectionInstance() {
    // Children are destroyed automatically via unique_ptr

    // Unload model
    if (hasModel) {
        UnloadModel(model);
        hasModel = false;
    }
}
