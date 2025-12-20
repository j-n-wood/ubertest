# TODO

## Rendering
- [ ] Support GLTF material parameters in shader (PBR: baseColorFactor, metallicFactor, roughnessFactor, etc.)

## Future: Native Ellipsoid Mesh Generation
Plan saved to: `.claude/plans/composed-twirling-raven.md`

Generate ellipsoid mesh natively in C using Raylib's Mesh API (like GenMeshCube/GenMeshSphere) instead of loading from GLTF. This bypasses GLTF parsing issues and ensures correct Y-up normals.

## Investigation Notes
- Cube mesh (GenMeshCube) displays correct normals in debug mode
- Python-generated ellipsoid.gltf shows incorrect normals despite Y-up generation
- Vertex shader now uses mat3(matModel) instead of matNormal (raylib #1870 workaround)
- Standard GLTF models (Suzanne.glb, Duck.glb) display correct normals
- Issue is isolated to Python GLTF generation

## Completed
- [x] Verify mouse tracking works correctly (tested with Suzanne.glb)
- [x] Fix directional light direction (was pointing wrong way)
- [x] Add debug visualization modes for normals, lightDir, viewDir, halfDir
- [x] Apply lighting shader to obstacle cubes
