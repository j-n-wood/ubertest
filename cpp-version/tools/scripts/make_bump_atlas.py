#!/usr/bin/env python3
"""
Bump Atlas Generator

Creates a texture atlas from individual bump/normal map images.
Outputs both the atlas PNG and a manifest JSON file listing tile indices.

Usage:
    python make_bump_atlas.py [--input DIR] [--output DIR] [--tile-size N]

Default:
    --input   ../../assets/textures/bump
    --output  ../../assets/textures
    --tile-size 128
"""

import argparse
import json
import os
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Error: Pillow (PIL) is required. Install with: pip install Pillow")
    sys.exit(1)


def next_power_of_two(n: int) -> int:
    """Return the smallest power of 2 >= n."""
    if n <= 0:
        return 1
    n -= 1
    n |= n >> 1
    n |= n >> 2
    n |= n >> 4
    n |= n >> 8
    n |= n >> 16
    return n + 1


def calculate_atlas_size(tile_count: int, tile_size: int) -> tuple[int, int, int]:
    """
    Calculate optimal atlas dimensions (power of 2) for given tile count.
    Returns (width, height, columns) minimizing wasted space.
    """
    # Try different column counts and find the one with least overhead
    best_overhead = float('inf')
    best_dims = (tile_size, tile_size, 1)

    for cols in range(1, tile_count + 1):
        rows = (tile_count + cols - 1) // cols
        raw_width = cols * tile_size
        raw_height = rows * tile_size

        # Round up to power of 2
        atlas_width = next_power_of_two(raw_width)
        atlas_height = next_power_of_two(raw_height)

        # Calculate overhead (wasted pixels)
        total_pixels = atlas_width * atlas_height
        used_pixels = tile_count * tile_size * tile_size
        overhead = total_pixels - used_pixels

        if overhead < best_overhead:
            best_overhead = overhead
            best_dims = (atlas_width, atlas_height, cols)

    return best_dims


def collect_images(input_dir: Path) -> list[Path]:
    """Collect all image files from input directory."""
    extensions = {'.png', '.jpg', '.jpeg', '.bmp'}
    images = []

    for f in input_dir.iterdir():
        if f.is_file() and f.suffix.lower() in extensions:
            images.append(f)

    return images


def sort_images(images: list[Path]) -> list[Path]:
    """Sort images with flat.png first, then alphabetically by filename."""
    flat_images = [img for img in images if img.name.lower() == 'flat.png']
    other_images = [img for img in images if img.name.lower() != 'flat.png']

    # Sort others alphabetically (case-insensitive)
    other_images.sort(key=lambda p: p.name.lower())

    return flat_images + other_images


def create_atlas(
    images: list[Path],
    tile_size: int,
    atlas_width: int,
    atlas_height: int,
    columns: int
) -> Image.Image:
    """Create the atlas image by compositing all source images."""
    # Create RGBA atlas with neutral normal map color (128, 128, 255) for unused slots
    atlas = Image.new('RGBA', (atlas_width, atlas_height), (128, 128, 255, 255))

    for idx, img_path in enumerate(images):
        col = idx % columns
        row = idx // columns
        x = col * tile_size
        y = row * tile_size

        # Load and resize image
        try:
            img = Image.open(img_path)
            # Convert to RGBA if needed
            if img.mode != 'RGBA':
                img = img.convert('RGBA')
            # Resize to tile size using high-quality resampling
            if img.size != (tile_size, tile_size):
                img = img.resize((tile_size, tile_size), Image.Resampling.LANCZOS)

            atlas.paste(img, (x, y))
            print(f"  [{idx:2d}] {img_path.name} -> ({col}, {row})")
        except Exception as e:
            print(f"  Warning: Failed to load {img_path.name}: {e}")

    return atlas


def create_manifest(
    images: list[Path],
    tile_size: int,
    columns: int,
    atlas_width: int,
    atlas_height: int
) -> dict:
    """Create the manifest JSON structure."""
    rows = (len(images) + columns - 1) // columns

    tiles = []
    for idx, img_path in enumerate(images):
        tiles.append({
            "index": idx,
            "name": img_path.name
        })

    return {
        "tileSize": tile_size,
        "columns": columns,
        "rows": rows,
        "atlasWidth": atlas_width,
        "atlasHeight": atlas_height,
        "tileCount": len(images),
        "tiles": tiles
    }


def main():
    parser = argparse.ArgumentParser(
        description="Generate bump/normal map texture atlas"
    )
    parser.add_argument(
        "--input", "-i",
        type=Path,
        default=None,
        help="Input directory containing bump map images"
    )
    parser.add_argument(
        "--output", "-o",
        type=Path,
        default=None,
        help="Output directory for atlas and manifest"
    )
    parser.add_argument(
        "--tile-size", "-s",
        type=int,
        default=128,
        help="Tile size in pixels (default: 128)"
    )
    parser.add_argument(
        "--atlas-name",
        type=str,
        default="bump_atlas",
        help="Base name for output files (default: bump_atlas)"
    )

    args = parser.parse_args()

    # Determine paths relative to script location if not specified
    script_dir = Path(__file__).parent

    if args.input is None:
        args.input = script_dir / ".." / ".." / "assets" / "textures" / "bump"
    if args.output is None:
        args.output = script_dir / ".." / ".." / "assets" / "textures"

    args.input = args.input.resolve()
    args.output = args.output.resolve()

    print(f"Bump Atlas Generator")
    print(f"====================")
    print(f"Input directory:  {args.input}")
    print(f"Output directory: {args.output}")
    print(f"Tile size:        {args.tile_size}x{args.tile_size}")
    print()

    # Validate input directory
    if not args.input.is_dir():
        print(f"Error: Input directory does not exist: {args.input}")
        sys.exit(1)

    # Collect and sort images
    images = collect_images(args.input)
    if not images:
        print(f"Error: No images found in {args.input}")
        sys.exit(1)

    images = sort_images(images)
    print(f"Found {len(images)} images:")

    # Calculate atlas dimensions
    atlas_width, atlas_height, columns = calculate_atlas_size(
        len(images), args.tile_size
    )
    rows = (len(images) + columns - 1) // columns
    slots = columns * rows
    spare = slots - len(images)

    print(f"\nAtlas layout:")
    print(f"  Size:    {atlas_width}x{atlas_height} pixels")
    print(f"  Grid:    {columns} columns x {rows} rows = {slots} slots")
    print(f"  Used:    {len(images)} tiles")
    print(f"  Spare:   {spare} slots for future use")
    print()

    # Create atlas
    print("Compositing atlas:")
    atlas = create_atlas(images, args.tile_size, atlas_width, atlas_height, columns)

    # Create manifest
    manifest = create_manifest(
        images, args.tile_size, columns, atlas_width, atlas_height
    )

    # Ensure output directory exists
    args.output.mkdir(parents=True, exist_ok=True)

    # Save atlas
    atlas_path = args.output / f"{args.atlas_name}.png"
    atlas.save(atlas_path, "PNG")
    print(f"\nSaved atlas: {atlas_path}")

    # Save manifest
    manifest_path = args.output / f"{args.atlas_name}_manifest.json"
    with open(manifest_path, 'w') as f:
        json.dump(manifest, f, indent=2)
    print(f"Saved manifest: {manifest_path}")

    print("\nDone!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
