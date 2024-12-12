#pragma once
#include "common.h"

namespace oxenc {
inline namespace types { inline namespace span {
    template <typename T, size_t N = std::dynamic_extent>
    using const_span = std::span<const T, N>;
}}  // namespace types::span

namespace detail {
template <typename T>
inline constexpr bool const_span_type = false;
template <basic_char T>
inline constexpr bool const_span_type<const_span<T>> = true;

template <typename T>
inline constexpr bool char_span_type = false;
template <basic_char T>
inline constexpr bool char_span_type<std::span<T>> = true;
}  // namespace detail

template <typename T, typename U = std::remove_cv_t<T>>
concept const_span_type = detail::const_span_type<U>;

template <typename T>
concept const_span_convertible = std::convertible_to<T, const_span<char>> ||
                                 std::convertible_to<T, const_span<unsigned char>> ||
                                 std::convertible_to<T, const_span<std::byte>>;

namespace detail {
template <basic_char T>
std::string_view span_to_sv(std::span<T>& x) {
    return {reinterpret_cast<const char*>(x.data()), x.size()};
}
}  // namespace detail

inline namespace operators { inline namespace span {
    template <std::equality_comparable T, size_t N, const_contiguous_range_t<T> R>
    inline bool operator==(const std::span<T, N>& lhs, const R& rhs) {
        return std::ranges::equal(lhs, rhs);
    }

    template <std::three_way_comparable T, size_t N, const_contiguous_range_t<T> R>
    inline auto operator<=>(const std::span<T, N>& lhs, const R& rhs) {
        return lexicographical_compare_three_way(
                lhs.begin(), lhs.end(), std::ranges::begin(rhs), std::ranges::end(rhs));
    }
}}  // namespace operators::span

namespace detail {
template <basic_char T, size_t N>
struct literal {
    consteval literal(const char (&s)[N]) {
        for (size_t i = 0; i < N; ++i)
            arr[i] = static_cast<T>(s[i]);
    }

    inline static constexpr size_t size{N - 1};
    T arr[N];

    constexpr const_span<T> span() const { return {arr, size}; }
};

template <size_t N>
struct sp_literal : literal<char, N> {
    consteval sp_literal(const char (&s)[N]) : literal<char, N>{s} {}
};
template <size_t N>
struct bsp_literal : literal<std::byte, N> {
    consteval bsp_literal(const char (&s)[N]) : literal<std::byte, N>{s} {}
};
template <size_t N>
struct usp_literal : literal<unsigned char, N> {
    consteval usp_literal(const char (&s)[N]) : literal<unsigned char, N>{s} {}
};
}  //  namespace detail

inline namespace literals { inline namespace span {
    template <detail::sp_literal CStr>
    constexpr auto operator""_csp() {
        return CStr.span();
    }

    template <detail::usp_literal UStr>
    constexpr auto operator""_usp() {
        return UStr.span();
    }

    template <detail::bsp_literal BStr>
    constexpr auto operator""_bsp() {
        return BStr.span();
    }
}}  // namespace literals::span

}  //  namespace oxenc
