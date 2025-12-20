#!/usr/bin/env python3
"""
Generate an ellipsoid GLTF model with correct Y-up normals.
- Semi-axes: X=2, Y=1, Z=1 (wide ellipsoid)
- Y-up coordinate system (GLTF standard)
- Smooth per-vertex normals
"""

import json
import struct
import base64
import math

def generate_ellipsoid(a=2.0, b=1.0, c=1.0, lat_segments=24, lon_segments=32):
    """
    Generate ellipsoid vertices, normals, and indices.
    a, b, c are semi-axes for X, Y, Z respectively.
    """
    positions = []
    normals = []
    indices = []

    # Generate vertices
    for lat in range(lat_segments + 1):
        theta = math.pi * lat / lat_segments  # 0 to pi (top to bottom)
        sin_theta = math.sin(theta)
        cos_theta = math.cos(theta)

        for lon in range(lon_segments + 1):
            phi = 2 * math.pi * lon / lon_segments  # 0 to 2pi

            # Unit sphere position
            x = sin_theta * math.cos(phi)
            y = cos_theta  # Y is up
            z = sin_theta * math.sin(phi)

            # Scale to ellipsoid
            px = a * x
            py = b * y
            pz = c * z

            positions.extend([px, py, pz])

            # Normal for ellipsoid: gradient of (x/a)^2 + (y/b)^2 + (z/c)^2 = 1
            # Gradient = (2x/a^2, 2y/b^2, 2z/c^2), normalized
            nx = x / (a * a)
            ny = y / (b * b)
            nz = z / (c * c)
            length = math.sqrt(nx*nx + ny*ny + nz*nz)
            if length > 0:
                nx /= length
                ny /= length
                nz /= length

            normals.extend([nx, ny, nz])

    # Generate indices (triangles)
    for lat in range(lat_segments):
        for lon in range(lon_segments):
            current = lat * (lon_segments + 1) + lon
            next_row = current + lon_segments + 1

            # Two triangles per quad
            # First triangle
            indices.extend([current, next_row, current + 1])
            # Second triangle
            indices.extend([current + 1, next_row, next_row + 1])

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
        "asset": {"version": "2.0", "generator": "Python ellipsoid generator (Y-up)"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "Ellipsoid"}],
        "meshes": [{
            "primitives": [{
                "attributes": {"POSITION": 0, "NORMAL": 1},
                "indices": 2,
                "mode": 4
            }],
            "name": "Ellipsoid"
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
    # Generate ellipsoid: wide in X (2), shorter in Y and Z (1)
    positions, normals, indices = generate_ellipsoid(a=2.0, b=1.0, c=1.0)
    gltf = create_gltf(positions, normals, indices)

    output_path = "../assets/models/ellipsoid.gltf"
    with open(output_path, 'w') as f:
        json.dump(gltf, f, indent=2)

    print(f"Generated {output_path}")
    print(f"  Vertices: {len(positions)//3}")
    print(f"  Triangles: {len(indices)//3}")
    print(f"  Semi-axes: X=2, Y=1, Z=1")
    print(f"  Top normal at Y=1 should be (0, 1, 0) - GREEN in debug mode")
