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
template <typename T>
constexpr bool is_string_like = false;
template <typename Char>
inline constexpr bool is_string_like<std::basic_string<Char>> = sizeof(Char) == 1;
template <typename Char>
inline constexpr bool is_string_like<std::basic_string_view<Char>> = sizeof(Char) == 1;

/// Accept anything that looks iterable (except for string-like types); value serialization
/// validity isn't checked here (it fails via the base case static assert).
template <typename T>
concept bt_input_list_container = !
is_string_like<T> && !tuple_like<T> && !bt_input_dict_container<T> &&
        requires {
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
        InputIterator1 __first1,
        InputIterator1 __last1,
        InputIterator2 __first2,
        InputIterator2 __last2,
        Cmp __comp) -> decltype(__comp(*__first1, *__first2)) {
#if defined(__clang__) && defined(_LIBCPP_VERSION) && (_LIBCPP_VERSION < 170000)
    size_t len1 = __last1 - __first1;
    size_t len2 = __last2 - __first2;
    size_t minLen = std::min(len1, len2);
    for (size_t i = 0; i < minLen; ++i, ++__first1, ++__first2) {
        auto __c = __comp(*__first1, *__first2);
        if (__c != 0) {
            return __c;
        }
    }
    return len1 <=> len2;
#else
    return std::lexicographical_compare_three_way(__first1, __last1, __first2, __last2, __comp);
#endif
}
}  // namespace oxenc
