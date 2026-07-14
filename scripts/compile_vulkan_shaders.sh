#!/bin/bash
#
# Compile GLSL shaders to SPIR-V and update VulkanShaders.h
# Run this when shader source changes.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SHADER_DIR="$PROJECT_DIR/src/platform/Android/sdk/src/main/cpp/shaders"
OUTPUT_HEADER="$PROJECT_DIR/src/platform/Android/sdk/src/main/cpp/VulkanShaders.h"

# Find glslc from Android NDK
GLSLC=""
for ndk in "$HOME/Library/Android/sdk/ndk"/*/shader-tools/darwin-x86_64/glslc; do
    if [ -x "$ndk" ]; then
        GLSLC="$ndk"
        break
    fi
done

if [ -z "$GLSLC" ]; then
    echo "Error: glslc not found in Android NDK"
    echo "Install NDK or set GLSLC environment variable"
    exit 1
fi

echo "Using glslc: $GLSLC"

# Compile shaders
VERT_SPV=$(mktemp)
FRAG_SPV=$(mktemp)
trap 'rm -f "$VERT_SPV" "$FRAG_SPV"' EXIT

"$GLSLC" -O "$SHADER_DIR/fullscreen.vert" -o "$VERT_SPV"
"$GLSLC" -O "$SHADER_DIR/fullscreen.frag" -o "$FRAG_SPV"

# Generate header
python3 << PYEOF
import struct

def to_c_array(path, name):
    with open(path, 'rb') as f:
        data = f.read()
    words = [f'0x{struct.unpack("<I", data[i:i+4])[0]:08x}' for i in range(0, len(data), 4)]
    lines = []
    for i in range(0, len(words), 8):
        chunk = words[i:i+8]
        lines.append('    ' + ', '.join(chunk) + ',')
    return f'static const uint32_t {name}[] = {{\n' + '\n'.join(lines) + '\n};'

print(to_c_array('$VERT_SPV', 'kVertSpirv'))
print()
print(to_c_array('$FRAG_SPV', 'kFragSpirv'))
PYEOF > "$OUTPUT_HEADER.tmp"

# Preserve copyright and GL shaders from existing header
python3 << PYEOF
import re

with open('$OUTPUT_HEADER', 'r') as f:
    content = f.read()

# Extract everything before kVertSpirv (copyright, includes, GL shaders)
header_end = content.find('static const uint32_t kVertSpirv')
preamble = content[:header_end].rstrip()

# Read new SPIR-V data
with open('$OUTPUT_HEADER.tmp', 'r') as f:
    spv_data = f.read()

with open('$OUTPUT_HEADER', 'w') as f:
    f.write(preamble + '\n\n' + spv_data + '\n')

print(f"Updated {len(spv_data)} bytes of SPIR-V data in $OUTPUT_HEADER")
PYEOF

rm -f "$OUTPUT_HEADER.tmp"
echo "Done! VulkanShaders.h updated."
