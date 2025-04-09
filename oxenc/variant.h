#pragma once

#include <variant>

namespace var = std;

namespace oxenc {

namespace [[deprecated("use stl <variant> directly")]] oxenc_variant_is_deprecated {
}

using namespace oxenc_variant_is_deprecated;

}  // namespace oxenc
