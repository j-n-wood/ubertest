# Scene Tool - Agent Instructions

This document provides instructions for AI agents automating the scene_tool.

## Tool Location

```bash
# From cpp-version directory
./build/tools/scene_tool/scene_tool

# Or absolute path after build
/path/to/cpp-version/build/tools/scene_tool/scene_tool
```

## Build Commands

```bash
# Build scene_tool only
cd /path/to/cpp-version
cmake -B build
cmake --build build --target scene_tool

# Full rebuild
cmake --build build --target scene_tool --clean-first
```

## Conversion Commands

### Convert Entire Ship

Converts ship1.txt and all referenced domains to JSON format.

```bash
./build/tools/scene_tool/scene_tool --convert-ship \
    /path/to/uber/uberdroid/data/ship1.txt \
    -o output/
```

**Expected output:**
- `output/ship.json` - Ship metadata with references to domain files
- `output/domains/domain_0.json` through `domain_15.json` - Individual domain data

**Success indicators:**
- Exit code 0
- "Conversion complete!" message
- All domain files created

**Failure indicators:**
- Exit code 1
- "Failed to parse" messages
- Missing output files

### Convert Single Domain

```bash
./build/tools/scene_tool/scene_tool --convert-domain \
    /path/to/uber/uberdroid/ship1/xmapfile0.txt \
    -t /path/to/uber/uberdroid/data/tiles.txt \
    -o domain_output.json
```

**Required for archetile expansion:** The `-t` flag with tiles.txt path is needed to expand archetile references into full tile geometry.

### Verify Conversion Output

Check that JSON was created correctly:

```bash
# Check file exists and has content
ls -la output/ship.json
ls -la output/domains/

# Validate JSON syntax
python3 -c "import json; json.load(open('output/ship.json'))"

# Check domain count
ls output/domains/*.json | wc -l
```

## Common Paths

| Asset | Typical Path |
|-------|--------------|
| Ship file | `uber/uberdroid/data/ship1.txt` |
| Domain files | `uber/uberdroid/ship1/xmapfile{n}.txt` |
| Tiles definition | `uber/uberdroid/data/tiles.txt` |
| Geometry XML | `uber/uberdroid/ship1/lvl{n}section{m}.xml` |
| Output directory | `cpp-version/build/tools/scene_tool/output/` |

## Automated Workflow

### Full Conversion Pipeline

```bash
#!/bin/bash
set -e

PROJECT_ROOT="/path/to/test_project"
CPP_VERSION="$PROJECT_ROOT/cpp-version"
UBERDROID="$PROJECT_ROOT/uber/uberdroid"
SCENE_TOOL="$CPP_VERSION/build/tools/scene_tool/scene_tool"
OUTPUT_DIR="$CPP_VERSION/build/tools/scene_tool/output"

# Ensure tool is built
cmake --build "$CPP_VERSION/build" --target scene_tool

# Convert ship1
"$SCENE_TOOL" --convert-ship "$UBERDROID/data/ship1.txt" -o "$OUTPUT_DIR/"

# Verify output
if [ -f "$OUTPUT_DIR/ship.json" ]; then
    echo "Ship conversion successful"
    echo "Domains converted: $(ls $OUTPUT_DIR/domains/*.json | wc -l)"
else
    echo "Ship conversion failed"
    exit 1
fi
```

### Convert and Validate Single Domain

```bash
DOMAIN_INDEX=5
SCENE_TOOL="./build/tools/scene_tool/scene_tool"
UBERDROID="../uber/uberdroid"

$SCENE_TOOL --convert-domain "$UBERDROID/ship1/xmapfile${DOMAIN_INDEX}.txt" \
    -t "$UBERDROID/data/tiles.txt" \
    -o "domain_${DOMAIN_INDEX}.json"

# Parse and check structure
python3 << 'EOF'
import json
with open(f"domain_{DOMAIN_INDEX}.json") as f:
    d = json.load(f)
    print(f"Domain: {d['name']}")
    print(f"Level: {d['levelNumber']}")
    print(f"Areas: {len(d['areas'])}")
    print(f"Waypoints: {len(d['waypoints'])}")
EOF
```

## Error Handling

### Common Errors and Solutions

| Error | Cause | Solution |
|-------|-------|----------|
| "Failed to open ship file" | Wrong path | Verify ship1.txt path exists |
| "tiles.txt not found" | Missing archetiles | Provide `-t` flag with correct path |
| "Failed to parse domain" | Corrupt or missing xmapfile | Check domain file path |
| "Default normal map not found" | Missing textures | Ensure build copied assets |

### Debug Mode

For verbose output during conversion, the tool logs to stdout. Capture with:

```bash
./scene_tool --convert-ship path/to/ship.txt -o output/ 2>&1 | tee conversion.log
```

## JSON Schema Validation

Domain JSON should have this structure:

```python
def validate_domain(path):
    import json
    with open(path) as f:
        d = json.load(f)

    assert 'version' in d
    assert 'levelNumber' in d
    assert 'name' in d
    assert 'areas' in d and isinstance(d['areas'], list)
    assert 'waypoints' in d and isinstance(d['waypoints'], list)
    assert 'objects' in d

    for area in d['areas']:
        assert 'tiles' in area
        assert 'features' in area

    print(f"Valid domain: {d['name']}")
    return True
```

## Integration Notes

### Using Output in Game

The converted JSON can be loaded at runtime:

```cpp
#include "scene_json.h"

Ship ship;
loadShipFromFile("output/ship.json", ship);

// Load specific domain
Domain domain;
loadDomainFromFile(ship.domainPaths[0], domain);
```

### Shared Code Location

The parsing and JSON code is in `shared/scene_convert/` for use by both scene_tool and the game:

- `scene_types.h` - Data structures
- `ship_parser.h/cpp` - Ship file parsing
- `domain_parser.h/cpp` - Domain parsing
- `scene_json.h/cpp` - JSON serialization/deserialization

### Physics Collision Data

Collision shapes are generated from geometry XML and stored in domain JSON:

```json
{
  "collision": {
    "polygons": [
      {"vertices": [[x1,y1], [x2,y2], ...]}
    ],
    "chains": [
      {"vertices": [...], "loop": false}
    ]
  }
}
```

These can be used to create Box2D static bodies at runtime.

## Viewer Mode (Interactive)

The `--view` command opens an interactive window which is not suitable for headless automation. Use only for manual inspection:

```bash
# Opens GUI window - requires display
./scene_tool --view output/ship.json
```

For automated testing, use conversion commands with JSON validation instead.
