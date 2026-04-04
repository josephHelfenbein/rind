#!/usr/bin/env python3
import sys
import os

def main():
    if len(sys.argv) != 5:
        print(f"Usage: {sys.argv[0]} INPUT_FILE OUTPUT_DIR ASSET_NAME CATEGORY", file=sys.stderr)
        sys.exit(1)

    input_file, output_dir, asset_name, category = sys.argv[1:5]

    safe_name = ''.join(c if c.isalnum() else '_' for c in asset_name)
    var_prefix = f"embedded_{category}_{safe_name}"

    _, ext = os.path.splitext(input_file)
    with open(input_file, 'rb') as f:
        data = f.read()

    byte_count = len(data)
    lines = []
    for i in range(0, byte_count, 32):
        chunk = data[i:i+32]
        hex_vals = ','.join(f'0x{b:02x}' for b in chunk)
        lines.append(f'     {hex_vals},')

    if lines:
        lines[-1] = lines[-1].rstrip(',')

    cpp_array = '\n'.join(lines)

    h_path = os.path.join(output_dir, f"{category}_{safe_name}.h")
    with open(h_path, 'w') as f:
        f.write(f"""#pragma once
#include <cstddef>
extern const unsigned char {var_prefix}_data[];
extern const size_t {var_prefix}_size;
extern const char {var_prefix}_ext[];
""")

    cpp_path = os.path.join(output_dir, f"{category}_{safe_name}.cpp")
    with open(cpp_path, 'w') as f:
        f.write(f"""#include "{category}_{safe_name}.h"
alignas(16) const unsigned char {var_prefix}_data[] = {{
{cpp_array}
}};
const size_t {var_prefix}_size = {byte_count};
const char {var_prefix}_ext[] = "{ext}";
""")

if __name__ == '__main__':
    main()
