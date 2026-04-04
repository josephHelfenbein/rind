#pragma once

#include <cstddef>

namespace engine {
    struct EmbeddedAsset {
        const unsigned char* data;
        size_t size;
        const char* ext;
    };
}
