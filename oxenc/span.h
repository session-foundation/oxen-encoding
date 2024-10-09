#pragma once
#include "common.h"

namespace oxenc {
inline namespace types { inline namespace span {
    template <typename T, size_t N = std::dynamic_extent>
    requires std::is_const_v<T> && std::equality_comparable<T>
    using const_span = std::span<T, N>;

    using cspan = const_span<const char>;
    using uspan = const_span<const unsigned char>;
    using bspan = const_span<const std::byte>;
}}  // namespace types::span

template <typename T, typename U = T::value_type>
concept const_span_like = basic_char<U> && std::same_as<const_span<const U>, T>;

namespace detail {
template <const_span_like T>
std::string_view to_sv(const T& x) {
    return {reinterpret_cast<const char*>(x.data()), x.size()};
}
}  // namespace detail

inline namespace operators { inline namespace span {
    template <typename T, size_t N, const_contiguous_range_t<T> R>
    inline bool operator==(const std::span<T, N>& lhs, const R& rhs) {
        return std::ranges::equal(lhs, rhs);
    }

    template <typename T, size_t N, const_contiguous_range_t<T> R>
    inline auto operator<=>(const std::span<T, N>& lhs, const R& rhs) {
        return lexi_three_way_compare(
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

    T arr[N];
    using size = std::integral_constant<size_t, N - 1>;

    constexpr const_span<const T> span() const { return {arr, size::value}; }
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
    constexpr cspan operator""_csp() {
        return CStr.span();
    }

    template <detail::usp_literal UStr>
    constexpr uspan operator""_usp() {
        return UStr.span();
    }

    template <detail::bsp_literal BStr>
    constexpr bspan operator""_bsp() {
        return BStr.span();
    }
}}  // namespace literals::span

}  //  namespace oxenc
