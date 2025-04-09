#pragma once

#include <algorithm>
#include <concepts>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace oxenc {
template <typename Char>
concept basic_char = sizeof(Char) == 1 && !std::same_as<std::remove_cv_t<Char>, bool> &&
                     (std::integral<Char> || std::same_as<std::remove_cv_t<Char>, std::byte>);

template <typename T>
concept bt_input_string = std::convertible_to<const T, std::string_view> ||
                          std::convertible_to<const T, std::span<const unsigned char>> ||
                          std::convertible_to<const T, std::span<const std::byte>>;

/// Partial dict validity; we don't check the second type for serializability, that will be
/// handled via the base case static_assert if invalid.
template <typename T>
concept bt_input_dict_container = bt_input_string<typename T::value_type::first_type> && requires {
    typename T::const_iterator;           // is const iterable
    typename T::value_type::second_type;  // has a second type
};

template <typename T>
concept tuple_like = requires { std::tuple_size<T>::value; } && !bt_input_string<T>;

namespace detail {
    template <typename T>
    inline constexpr bool char_string_type = false;
    template <basic_char T>
    inline constexpr bool char_string_type<std::basic_string<T>> = true;

    template <typename T>
    inline constexpr bool char_view_type = false;
    template <basic_char T>
    inline constexpr bool char_view_type<std::basic_string_view<T>> = true;

    template <typename It, typename K, typename V>
    concept is_unordered_map_iterator =
            std::same_as<It, typename std::unordered_map<K, V>::iterator> ||
            std::same_as<It, typename std::unordered_map<K, V>::const_iterator>;
}  // namespace detail

template <typename ForwardIt, typename ItValueType = std::iter_value_t<ForwardIt>>
concept ordered_pair_iterator =
        std::forward_iterator<ForwardIt> && std::tuple_size<ItValueType>::value == 2 &&
        !detail::is_unordered_map_iterator<
                ForwardIt,
                std::remove_const_t<std::tuple_element_t<0, ItValueType>>,
                std::tuple_element_t<1, ItValueType>>;

/// Accept anything that looks iterable (except for string-like types); value serialization
/// validity isn't checked here (it fails via the base case static assert).
template <typename T>
concept bt_input_list_container =
        !bt_input_string<T> && !tuple_like<T> && !bt_input_dict_container<T> && requires {
            typename T::const_iterator;
            typename T::value_type;
        };

using namespace std::literals;

}  // namespace oxenc
