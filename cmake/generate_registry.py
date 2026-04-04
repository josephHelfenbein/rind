#!/usr/bin/env python3
import sys
import os

def main():
    if len(sys.argv) < 4:
        print(f"Usage: {sys.argv[0]} CATEGORY OUTPUT_DIR NAME1 NAME2 ...", file=sys.stderr)
        sys.exit(1)

    category = sys.argv[1]
    output_dir = sys.argv[2]
    asset_names = sys.argv[3:]

    includes = []
    entries = []
    for name in asset_names:
        safe = ''.join(c if c.isalnum() else '_' for c in name)
        prefix = f"embedded_{category}_{safe}"
        includes.append(f'#include "{category}_{safe}.h"')
        entries.append(f'       {{"{name}", {{{prefix}_data, {prefix}_size, {prefix}_ext}}}},')

    includes_str = '\n'.join(includes)
    entries_str = '\n'.join(entries)

    registry_path = os.path.join(output_dir, f"{category}_registry.h")
    with open(registry_path, 'w') as f:
        f.write(f"""#pragma once
#include <cstddef>
#include <string>
#include <unordered_map>
#include "engine/EmbeddedAssets.h"
{includes_str}

inline const std::unordered_map<std::string, engine::EmbeddedAsset>& getEmbedded_{category}() {{
    static const std::unordered_map<std::string, engine::EmbeddedAsset> assets = {{
{entries_str}
    }};
    return assets;
}}
""")

if __name__ == '__main__':
    main()
