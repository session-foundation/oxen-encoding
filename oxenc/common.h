#pragma once
#include <concepts>
#include <iterator>
#include <string_view>
#include <type_traits>

namespace oxenc {

template <typename T>
concept string_view_compatible = std::convertible_to<T, std::string_view> ||
                                 std::convertible_to<T, std::basic_string_view<unsigned char>> ||
                                 std::convertible_to<T, std::basic_string_view<std::byte>>;

template <typename Char>
concept basic_char = sizeof(Char) == 1 && !std::same_as<Char, bool> &&
                     (std::integral<Char> || std::same_as<Char, std::byte>);

using namespace std::literals;

}  // namespace oxenc
