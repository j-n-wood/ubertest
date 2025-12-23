#ifndef MDL_TYPES_H
#define MDL_TYPES_H

#include <cstdint>

// MDL file magic number "IDST" and version
constexpr int32_t MDL_ID = (('T' << 24) + ('S' << 16) + ('D' << 8) + 'I');
constexpr int32_t MDL_VERSION = 10;

// Limits from Half-Life STUDIO.H
constexpr int MAXSTUDIOBONES = 128;
constexpr int MAXSTUDIOSEQUENCES = 256;
constexpr int MAXSTUDIOSKINS = 100;
constexpr int MAXSTUDIOMODELS = 32;
constexpr int MAXSTUDIOMESHES = 256;
constexpr int MAXSTUDIOVERTS = 2048;
constexpr int MAXSTUDIOTRIANGLES = 20000;

// Sequence flags
constexpr int STUDIO_LOOPING = 0x0001;

// Motion flags for animation channels
constexpr int STUDIO_X = 0x0001;
constexpr int STUDIO_Y = 0x0002;
constexpr int STUDIO_Z = 0x0004;
constexpr int STUDIO_XR = 0x0008;
constexpr int STUDIO_YR = 0x0010;
constexpr int STUDIO_ZR = 0x0020;

//------------------------------------------------------------------------------
// Main MDL header (matches studiohdr_t)
//------------------------------------------------------------------------------
struct MDLHeader {
    int32_t id;              // Must be MDL_ID ("IDST")
    int32_t version;         // Must be MDL_VERSION (10)
    char name[64];           // Model name
    int32_t length;          // File size in bytes

    float eyeposition[3];    // Ideal eye position
    float hull_min[3];       // Ideal movement hull size
    float hull_max[3];
    float view_bbmin[3];     // Clipping bounding box
    float view_bbmax[3];

    int32_t flags;

    // Bones
    int32_t numbones;
    int32_t boneindex;

    // Bone controllers
    int32_t numbonecontrollers;
    int32_t bonecontrollerindex;

    // Hit boxes
    int32_t numhitboxes;
    int32_t hitboxindex;

    // Animation sequences
    int32_t numseq;
    int32_t seqindex;

    // Demand-loaded sequence groups
    int32_t numseqgroups;
    int32_t seqgroupindex;

    // Textures
    int32_t numtextures;
    int32_t textureindex;
    int32_t texturedataindex;

    // Skin references (texture variations)
    int32_t numskinref;
    int32_t numskinfamilies;
    int32_t skinindex;

    // Body parts
    int32_t numbodyparts;
    int32_t bodypartindex;

    // Attachments
    int32_t numattachments;
    int32_t attachmentindex;

    // Sound tables (unused for conversion)
    int32_t soundtable;
    int32_t soundindex;
    int32_t soundgroups;
    int32_t soundgroupindex;

    // Transition graph
    int32_t numtransitions;
    int32_t transitionindex;
};

//------------------------------------------------------------------------------
// Bone definition (matches mstudiobone_t)
//------------------------------------------------------------------------------
struct MDLBone {
    char name[32];           // Bone name for symbolic links
    int32_t parent;          // Parent bone index, -1 = root
    int32_t flags;
    int32_t bonecontroller[6];  // Bone controller indices, -1 = none
    float value[6];          // Default position (X,Y,Z) and rotation (XR,YR,ZR)
    float scale[6];          // Scale for delta animation values
};

//------------------------------------------------------------------------------
// Bone controller (matches mstudiobonecontroller_t)
//------------------------------------------------------------------------------
struct MDLBoneController {
    int32_t bone;            // Bone index, -1 = none
    int32_t type;            // X, Y, Z, XR, YR, ZR, M (mouth)
    float start;
    float end;
    int32_t rest;            // Byte index value at rest
    int32_t index;           // Controller slot (0-3 user, 4 mouth)
};

//------------------------------------------------------------------------------
// Sequence group for demand loading (matches mstudioseqgroup_t)
//------------------------------------------------------------------------------
struct MDLSeqGroup {
    char label[32];          // Textual name
    char name[64];           // File name
    int32_t unused1;         // Was cache pointer
    int32_t unused2;         // Was data pointer
};

//------------------------------------------------------------------------------
// Sequence description (matches mstudioseqdesc_t)
//------------------------------------------------------------------------------
struct MDLSequenceDesc {
    char label[32];          // Sequence label/name
    float fps;               // Frames per second
    int32_t flags;           // STUDIO_LOOPING, etc.

    int32_t activity;
    int32_t actweight;

    int32_t numevents;
    int32_t eventindex;

    int32_t numframes;       // Number of frames in sequence

    int32_t numpivots;
    int32_t pivotindex;

    int32_t motiontype;
    int32_t motionbone;
    float linearmovement[3];
    int32_t automoveposindex;
    int32_t automoveangleindex;

    float bbmin[3];          // Per-sequence bounding box
    float bbmax[3];

    int32_t numblends;       // 1, 2, or 4 blended animations
    int32_t animindex;       // Offset to mstudioanim_t array

    int32_t blendtype[2];
    float blendstart[2];
    float blendend[2];
    int32_t blendparent;

    int32_t seqgroup;        // Sequence group (0 = main file)

    int32_t entrynode;
    int32_t exitnode;
    int32_t nodeflags;

    int32_t nextseq;
};

//------------------------------------------------------------------------------
// Animation offset structure (matches mstudioanim_t)
// One per bone per blend, contains offsets to animation data
//------------------------------------------------------------------------------
struct MDLAnim {
    uint16_t offset[6];      // Offsets for X, Y, Z, XR, YR, ZR
};

//------------------------------------------------------------------------------
// Animation value (matches mstudioanimvalue_t)
// Run-length encoded keyframe data
//------------------------------------------------------------------------------
union MDLAnimValue {
    struct {
        uint8_t valid;       // Number of valid values following
        uint8_t total;       // Total frames this span covers
    } num;
    int16_t value;           // Actual keyframe value
};

//------------------------------------------------------------------------------
// Body part (matches mstudiobodyparts_t)
// Groups of model variants (e.g., different heads)
//------------------------------------------------------------------------------
struct MDLBodyPart {
    char name[64];
    int32_t nummodels;
    int32_t base;            // Base multiplier for variant selection
    int32_t modelindex;      // Offset to MDLModel array
};

//------------------------------------------------------------------------------
// Sub-model (matches mstudiomodel_t)
// A renderable mesh within a body part
//------------------------------------------------------------------------------
struct MDLModel {
    char name[64];

    int32_t type;
    float boundingradius;

    int32_t nummesh;
    int32_t meshindex;

    int32_t numverts;        // Number of unique vertices
    int32_t vertinfoindex;   // Offset to vertex bone indices (uint8_t array)
    int32_t vertindex;       // Offset to vertex positions (vec3_t array)

    int32_t numnorms;        // Number of unique normals
    int32_t norminfoindex;   // Offset to normal bone indices (uint8_t array)
    int32_t normindex;       // Offset to normal vectors (vec3_t array)

    int32_t numgroups;       // Deformation groups
    int32_t groupindex;
};

//------------------------------------------------------------------------------
// Mesh (matches mstudiomesh_t)
// A batch of triangles sharing the same material
//------------------------------------------------------------------------------
struct MDLMesh {
    int32_t numtris;
    int32_t triindex;        // Offset to triangle strip/fan commands
    int32_t skinref;         // Material/texture index
    int32_t numnorms;
    int32_t normindex;
};

//------------------------------------------------------------------------------
// Texture info (matches mstudiotexture_t)
//------------------------------------------------------------------------------
struct MDLTexture {
    char name[64];
    int32_t flags;
    int32_t width;
    int32_t height;
    int32_t index;           // Offset to texture data (after header)
};

//------------------------------------------------------------------------------
// Attachment point (matches mstudioattachment_t)
//------------------------------------------------------------------------------
struct MDLAttachment {
    char name[32];
    int32_t type;
    int32_t bone;
    float org[3];            // Attachment point
    float vectors[3][3];
};

//------------------------------------------------------------------------------
// Hit box (matches mstudiobbox_t)
//------------------------------------------------------------------------------
struct MDLHitBox {
    int32_t bone;
    int32_t group;           // Intersection group
    float bbmin[3];
    float bbmax[3];
};

#endif // MDL_TYPES_H
