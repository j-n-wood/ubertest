#include "unit_instance.h"

//------------------------------------------------------------------------------
// SectionInstance destructor - RAII cleanup of physics and rendering resources
//------------------------------------------------------------------------------

SectionInstance::~SectionInstance() {
    // Children are destroyed automatically via unique_ptr

    // Destroy joint to parent
    if (b2Joint_IsValid(parentJoint)) {
        b2DestroyJoint(parentJoint);
        parentJoint = b2_nullJointId;
    }

    // Destroy physics body
    if (b2Body_IsValid(bodyId)) {
        b2DestroyBody(bodyId);
        bodyId = b2_nullBodyId;
    }

    // Unload model
    if (hasModel) {
        UnloadModel(model);
        hasModel = false;
    }
}
