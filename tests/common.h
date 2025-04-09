#pragma once
#include <catch2/catch.hpp>
#include <unordered_map>

#include "oxenc/base32z.h"
#include "oxenc/base64.h"
#include "oxenc/bt.h"
#include "oxenc/hex.h"

using namespace oxenc;

template <basic_char Char>
std::span<const Char> to_span(std::string_view x) {
    return {reinterpret_cast<const Char*>(x.data()), x.size()};
}

inline std::string_view view(std::span<const unsigned char> x) {
    return {reinterpret_cast<const char*>(x.data()), x.size()};
}
inline std::string_view view(std::span<const std::byte> x) {
    return {reinterpret_cast<const char*>(x.data()), x.size()};
}
inline std::string_view view(std::span<const char> x) {
    return {x.data(), x.size()};
}
