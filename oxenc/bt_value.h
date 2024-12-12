#pragma once

// This header is here to provide just the basic bt_value/bt_dict/bt_list definitions without
// needing to include the full bt_serialize.h header.

#include <concepts>
#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "span.h"

namespace oxenc {

struct bt_value;

/// The type used to store dictionaries inside bt_value.
using bt_dict = std::map<std::string, bt_value>;  // NB: unordered_map doesn't work because it can't
                                                  // be used with a predeclared type
/// The type used to store list items inside bt_value.
using bt_list = std::list<bt_value>;

/// The basic variant that can hold anything (recursively).
using bt_variant = std::variant<std::string, std::string_view, int64_t, uint64_t, bt_list, bt_dict>;
// const_span<char>>;

namespace detail {
    template <typename Tuple, size_t... Is>
    bt_list tuple_to_list(const Tuple& tuple, std::index_sequence<Is...>) {
        return {{bt_value{std::get<Is>(tuple)}...}};
    }
    template <typename T>
    constexpr bool is_tuple = false;
    template <typename... T>
    constexpr bool is_tuple<std::tuple<T...>> = true;
    template <typename S, typename T>
    constexpr bool is_tuple<std::pair<S, T>> = true;
}  // namespace detail

/// Recursive generic type that can fully represent everything valid for a BT serialization.
/// This is basically just an empty wrapper around the std::variant, except we add some extra
/// converting constructors:
/// - integer constructors so that any unsigned value goes to the uint64_t and any signed value goes
///   to the int64_t.
/// - std::tuple and std::pair constructors that build a bt_list out of the tuple/pair elements.
struct bt_value : bt_variant {
    using bt_variant::bt_variant;
    using bt_variant::operator=;

    template <typename T>
    requires std::unsigned_integral<std::remove_cvref_t<T>>
    bt_value(T&& u_val) : bt_variant{static_cast<uint64_t>(u_val)} {}

    template <typename T>
    requires std::signed_integral<std::remove_cvref_t<T>>
    bt_value(T&& s_val) : bt_variant{static_cast<int64_t>(s_val)} {}

    template <typename... T>
    bt_value(const std::tuple<T...>& tuple) :
            bt_variant{detail::tuple_to_list(tuple, std::index_sequence_for<T...>{})} {}

    template <typename S, typename T>
    bt_value(const std::pair<S, T>& pair) :
            bt_variant{detail::tuple_to_list(pair, std::index_sequence_for<S, T>{})} {}

    template <typename T>
    requires(!std::integral<std::remove_cvref_t<T>> && !detail::is_tuple<std::remove_cvref_t<T>>)
    bt_value(T&& v) : bt_variant{std::forward<T>(v)} {}

    template <const_span_type T>
    bt_value(T& sp) :
            bt_value{std::string_view{reinterpret_cast<const char*>(sp.data()), sp.size()}} {}

    bt_value(const char* s) : bt_value{std::string_view{s}} {}
};

}  // namespace oxenc
