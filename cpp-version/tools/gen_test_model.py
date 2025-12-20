#!/usr/bin/env python3
"""
Generate a simple asymmetric test model to verify coordinate system.
- Wide in X (4 units)
- Short in Y (1 unit)
- Medium in Z (2 units)
- Top face normal should be (0, 1, 0) in Y-up convention
"""

import json
import struct
import base64

def generate_box_with_normals():
    """Generate a box with explicit face normals."""
    # Vertices for a box: 4 verts per face, 6 faces = 24 verts
    # Box dimensions: X=4, Y=1, Z=2 centered at origin
    hx, hy, hz = 2.0, 0.5, 1.0

    # Each face has 4 vertices with the same normal
    # Face order: +Y (top), -Y (bottom), +X, -X, +Z, -Z
    positions = []
    normals = []
    indices = []

    # Top face (+Y) - this should appear GREEN in debug mode
    # Normal: (0, 1, 0)
    positions.extend([
        -hx, hy, -hz,  # 0
        hx, hy, -hz,   # 1
        hx, hy, hz,    # 2
        -hx, hy, hz,   # 3
    ])
    normals.extend([0, 1, 0] * 4)
    indices.extend([0, 1, 2, 0, 2, 3])

    # Bottom face (-Y)
    # Normal: (0, -1, 0)
    base = len(positions) // 3
    positions.extend([
        -hx, -hy, hz,   # 4
        hx, -hy, hz,    # 5
        hx, -hy, -hz,   # 6
        -hx, -hy, -hz,  # 7
    ])
    normals.extend([0, -1, 0] * 4)
    indices.extend([base+0, base+1, base+2, base+0, base+2, base+3])

    # Right face (+X) - should appear RED in debug mode
    # Normal: (1, 0, 0)
    base = len(positions) // 3
    positions.extend([
        hx, -hy, -hz,
        hx, hy, -hz,
        hx, hy, hz,
        hx, -hy, hz,
    ])
    normals.extend([1, 0, 0] * 4)
    indices.extend([base+0, base+1, base+2, base+0, base+2, base+3])

    # Left face (-X)
    # Normal: (-1, 0, 0)
    base = len(positions) // 3
    positions.extend([
        -hx, -hy, hz,
        -hx, hy, hz,
        -hx, hy, -hz,
        -hx, -hy, -hz,
    ])
    normals.extend([-1, 0, 0] * 4)
    indices.extend([base+0, base+1, base+2, base+0, base+2, base+3])

    # Front face (+Z) - should appear BLUE in debug mode
    # Normal: (0, 0, 1)
    base = len(positions) // 3
    positions.extend([
        -hx, -hy, hz,
        hx, -hy, hz,
        hx, hy, hz,
        -hx, hy, hz,
    ])
    normals.extend([0, 0, 1] * 4)
    indices.extend([base+0, base+1, base+2, base+0, base+2, base+3])

    # Back face (-Z)
    # Normal: (0, 0, -1)
    base = len(positions) // 3
    positions.extend([
        hx, -hy, -hz,
        -hx, -hy, -hz,
        -hx, hy, -hz,
        hx, hy, -hz,
    ])
    normals.extend([0, 0, -1] * 4)
    indices.extend([base+0, base+1, base+2, base+0, base+2, base+3])

    return positions, normals, indices

def create_gltf(positions, normals, indices):
    """Create a GLTF file from geometry data."""
    # Pack binary data
    pos_data = struct.pack(f'{len(positions)}f', *positions)
    norm_data = struct.pack(f'{len(normals)}f', *normals)
    idx_data = struct.pack(f'{len(indices)}H', *indices)

    # Calculate bounds
    pos_tuples = [(positions[i], positions[i+1], positions[i+2])
                  for i in range(0, len(positions), 3)]
    min_pos = [min(p[i] for p in pos_tuples) for i in range(3)]
    max_pos = [max(p[i] for p in pos_tuples) for i in range(3)]

    # Combine buffers
    buffer_data = pos_data + norm_data + idx_data
    buffer_b64 = base64.b64encode(buffer_data).decode('ascii')

    vertex_count = len(positions) // 3
    index_count = len(indices)

    gltf = {
        "asset": {"version": "2.0", "generator": "Test model generator - Y-up convention"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "TestBox"}],
        "meshes": [{
            "primitives": [{
                "attributes": {"POSITION": 0, "NORMAL": 1},
                "indices": 2,
                "mode": 4
            }],
            "name": "TestBox"
        }],
        "accessors": [
            {
                "bufferView": 0,
                "componentType": 5126,
                "count": vertex_count,
                "type": "VEC3",
                "min": min_pos,
                "max": max_pos
            },
            {
                "bufferView": 1,
                "componentType": 5126,
                "count": vertex_count,
                "type": "VEC3"
            },
            {
                "bufferView": 2,
                "componentType": 5123,
                "count": index_count,
                "type": "SCALAR"
            }
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(pos_data), "target": 34962},
            {"buffer": 0, "byteOffset": len(pos_data), "byteLength": len(norm_data), "target": 34962},
            {"buffer": 0, "byteOffset": len(pos_data) + len(norm_data), "byteLength": len(idx_data), "target": 34963}
        ],
        "buffers": [{
            "uri": f"data:application/octet-stream;base64,{buffer_b64}",
            "byteLength": len(buffer_data)
        }]
    }

    return gltf

if __name__ == "__main__":
    positions, normals, indices = generate_box_with_normals()
    gltf = create_gltf(positions, normals, indices)

    output_path = "../assets/models/test_box.gltf"
    with open(output_path, 'w') as f:
        json.dump(gltf, f, indent=2)

    print(f"Generated {output_path}")
    print(f"  Vertices: {len(positions)//3}")
    print(f"  Indices: {len(indices)}")
    print(f"  Box dimensions: X=4, Y=1, Z=2 (wide, short, medium)")
    print(f"  Top face normal: (0, 1, 0) - should appear GREEN in debug mode 1")
