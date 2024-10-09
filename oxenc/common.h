#pragma once
#include <algorithm>
#include <concepts>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace oxenc {

template <typename T>
concept string_view_compatible = std::convertible_to<T, std::string_view> ||
                                 std::convertible_to<T, std::basic_string_view<unsigned char>> ||
                                 std::convertible_to<T, std::basic_string_view<std::byte>>;

template <typename Char>
concept basic_char = sizeof(Char) == 1 && !
std::same_as<Char, bool> && (std::integral<Char> || std::same_as<Char, std::byte>);

/// Partial dict validity; we don't check the second type for serializability, that will be
/// handled via the base case static_assert if invalid.
template <typename T>
concept bt_input_dict_container =
        (std::same_as<std::string, std::remove_cv_t<typename T::value_type::first_type>> ||
         std::same_as<std::string_view, std::remove_cv_t<typename T::value_type::first_type>>) &&
        requires {
            typename T::const_iterator;           // is const iterable
            typename T::value_type::second_type;  // has a second type
        };

template <typename Tuple>
concept tuple_like = requires { std::tuple_size<Tuple>::value; };

// True if the type is a std::string, std::string_view, or some a basic_string<Char> for some
// single-byte type Char.
template <typename T, typename U = T::value_type>
concept string_like = basic_char<U> &&
                      (std::same_as<std::basic_string<U>, std::remove_cv_t<T>> ||
                       std::same_as<std::basic_string_view<U>, std::remove_cv_t<T>>);

/// Accept anything that looks iterable (except for string-like types); value serialization
/// validity isn't checked here (it fails via the base case static assert).
template <typename T>
concept bt_input_list_container = !
string_like<T> && !tuple_like<T> && !bt_input_dict_container<T> && requires {
                                                                       typename T::const_iterator;
                                                                       typename T::value_type;
                                                                   };

template <typename R, typename T>
concept const_contiguous_range_t =
        std::ranges::contiguous_range<R const> &&
        std::same_as<std::remove_cvref_t<T>, std::ranges::range_value_t<R const>>;

using namespace std::literals;

template <class InputIterator1, class InputIterator2, class Cmp>
constexpr auto lexi_three_way_compare(
        InputIterator1 first1,
        InputIterator1 last1,
        InputIterator2 first2,
        InputIterator2 last2,
        Cmp comp) -> decltype(comp(*first1, *first2)) {
#if defined(__clang__) && defined(_LIBCPP_VERSION) && (_LIBCPP_VERSION < 170000)
    size_t len1 = last1 - first1;
    size_t len2 = last2 - first2;
    size_t minLen = std::min(len1, len2);
    for (size_t i = 0; i < minLen; ++i, ++first1, ++first2) {
        auto c = comp(*first1, *first2);
        if (c != 0) {
            return c;
        }
    }
    return len1 <=> len2;
#else
    return std::lexicographical_compare_three_way(first1, last1, first2, last2, comp);
#endif
}
}  // namespace oxenc
