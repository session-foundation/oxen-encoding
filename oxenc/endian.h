#pragma once

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>

#if !(defined(__clang__) || defined(__GNUC__))

#if defined(_MSC_VER)
#include <cstdlib>

#else
#include <algorithm>

#endif
#endif

namespace oxenc {
#ifdef __cpp_lib_bit_cast
using std::bit_cast;
#else
template <class To, class From>
    requires(
            sizeof(To) == sizeof(From) && std::is_trivially_copyable<To>::value &&
            std::is_trivially_copyable<From>::value && std::is_trivially_constructible<To>::value &&
            std::is_trivially_constructible<From>::value)
[[nodiscard]] inline constexpr To bit_cast(const From& from) noexcept {
    To storage{};
    std::construct_at(std::launder(&storage), from);
    return storage;
}
#endif

#if defined(__clang__) || defined(__GNUC__)
#define bswap_16(x) __builtin_bswap16(x)
#define bswap_32(x) __builtin_bswap32(x)
#define bswap_64(x) __builtin_bswap64(x)

#else  //  MSVC and other weird compiler choices
template <std::integral T>
    requires std::has_unique_object_representations_v<T> && std::is_unsigned_v<T>
[[nodiscard]] inline constexpr T byteswap(T val) noexcept {
    if (std::is_constant_evaluated()) {
        auto value = bit_cast<std::array<std::byte, sizeof(T)>>(val);
        std::ranges::reverse(value);
        return bit_cast<T>(value);
    }

    // fallback implementation
    size_t diff = CHAR_BIT * (sizeof(T) - 1);

    T m1 = UCHAR_MAX;
    T m2 = (T)(m1 << diff);
    T v = val;

    for (size_t i = 0; i < sizeof(T) / 2; ++i) {
        T b1 = v & m1, b2 = v & m2;
        v = (T)(v ^ b1 ^ b2 ^ (b1 << diff) ^ (b2 >> diff));
        m1 <<= CHAR_BIT;
        m2 >>= CHAR_BIT;
        diff -= 2 * CHAR_BIT;
    }

    return v;
}

#define bswap_16(x) byteswap<uint16_t>(x)
#define bswap_32(x) byteswap<uint32_t>(x)
#define bswap_64(x) byteswap<uint64_t>(x)

#endif

/// True if this is a little-endian platform
inline constexpr bool little_endian = std::endian::native == std::endian::little;

/// True if this is a big-endian platform
inline constexpr bool big_endian = !little_endian;

/// True if the type is integral and of a size we support swapping.  (We also allow size=1
/// values to be passed here for completeness, though nothing is ever swapped for such a value).
template <typename T>
concept endian_swappable_integer =
        std::integral<T> && (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);

/// Byte swaps an integer value unconditionally.  You usually want to use one of the other
/// endian-aware functions rather than this.
template <endian_swappable_integer T>
constexpr void byteswap_inplace(T& val) {
    if constexpr (sizeof(T) == 2)
        val = bit_cast<T>(bswap_16(bit_cast<uint16_t>(val)));
    else if constexpr (sizeof(T) == 4)
        val = bit_cast<T>(bswap_32(bit_cast<uint32_t>(val)));
    else if constexpr (sizeof(T) == 8)
        val = bit_cast<T>(bswap_64(bit_cast<uint64_t>(val)));
}

/// Converts a host-order integer value into a little-endian value, mutating it.  Does nothing
/// on little-endian platforms.
template <endian_swappable_integer T>
constexpr void host_to_little_inplace(T& val) {
    if constexpr (!little_endian)
        byteswap_inplace(val);
}

/// Converts a host-order integer value into a little-endian value, returning it.  Does no
/// converstion on little-endian platforms.
template <endian_swappable_integer T>
constexpr T host_to_little(T val) {
    host_to_little_inplace(val);
    return val;
}

/// Converts a little-endian integer value into a host-order (native) integer value, mutating
/// it.  Does nothing on little-endian platforms.
template <endian_swappable_integer T>
constexpr void little_to_host_inplace(T& val) {
    if constexpr (!little_endian)
        byteswap_inplace(val);
}

/// Converts a little-order integer value into a host-order (native) integer value, returning
/// it.  Does no conversion on little-endian platforms.
template <endian_swappable_integer T>
constexpr T little_to_host(T val) {
    little_to_host_inplace(val);
    return val;
}

/// Converts a host-order integer value into a big-endian value, mutating it.  Does nothing on
/// big-endian platforms.
template <endian_swappable_integer T>
constexpr void host_to_big_inplace(T& val) {
    if constexpr (!big_endian)
        byteswap_inplace(val);
}

/// Converts a host-order integer value into a big-endian value, returning it.  Does no
/// conversion on big-endian platforms.
template <endian_swappable_integer T>
constexpr T host_to_big(T val) {
    host_to_big_inplace(val);
    return val;
}

/// Converts a big-endian value into a host-order (native) integer value, mutating it.  Does
/// nothing on big-endian platforms.
template <endian_swappable_integer T>
constexpr void big_to_host_inplace(T& val) {
    if constexpr (!big_endian)
        byteswap_inplace(val);
}

/// Converts a big-order integer value into a host-order (native) integer value, returning it.
/// Does no conversion on big-endian platforms.
template <endian_swappable_integer T>
constexpr T big_to_host(T val) {
    big_to_host_inplace(val);
    return val;
}

/// Loads a host-order integer value from a memory location containing little-endian bytes.
/// (There is no alignment requirement on the given pointer address).
template <endian_swappable_integer T>
T load_little_to_host(const void* from) {
    T val;
    std::memcpy(&val, from, sizeof(T));
    little_to_host_inplace(val);
    return val;
}

/// Loads a little-endian integer value from a memory location containing host order bytes.
/// (There is no alignment requirement on the given pointer address).
template <endian_swappable_integer T>
T load_host_to_little(const void* from) {
    T val;
    std::memcpy(&val, from, sizeof(T));
    host_to_little_inplace(val);
    return val;
}

/// Loads a host-order integer value from a memory location containing big-endian bytes.  (There
/// is no alignment requirement on the given pointer address).
template <endian_swappable_integer T>
T load_big_to_host(const void* from) {
    T val;
    std::memcpy(&val, from, sizeof(T));
    big_to_host_inplace(val);
    return val;
}

/// Loads a big-endian integer value from a memory location containing host order bytes.  (There
/// is no alignment requirement on the given pointer address).
template <endian_swappable_integer T>
T load_host_to_big(const void* from) {
    T val;
    std::memcpy(&val, from, sizeof(T));
    host_to_big_inplace(val);
    return val;
}

/// Writes a little-endian integer value into the given memory location, copying and converting
/// it (if necessary) from the given host-order integer value.
template <endian_swappable_integer T>
void write_host_as_little(T val, void* to) {
    host_to_little_inplace(val);
    std::memcpy(to, &val, sizeof(T));
}

template <endian_swappable_integer T>
constexpr void write_host_as_little(T val, T* to) {
    to = host_to_little(val);
}

/// Writes a big-endian integer value into the given memory location, copying and converting it
/// (if necessary) from the given host-order integer value.
template <endian_swappable_integer T>
void write_host_as_big(T val, void* to) {
    host_to_big_inplace(val);
    std::memcpy(to, &val, sizeof(T));
}

template <endian_swappable_integer T>
constexpr void write_host_as_big(T val, T* to) {
    to = host_to_big(val);
}

/// Writes a host-order integer value into the given memory location, copying and converting it
/// (if necessary) from the given little-endian integer value.
template <endian_swappable_integer T>
void write_little_as_host(T val, void* to) {
    little_to_host_inplace(val);
    std::memcpy(to, &val, sizeof(T));
}

template <endian_swappable_integer T>
constexpr void little_to_host_inplace(T val, T* to) {
    to = little_to_host(val);
}

/// Writes a host-order integer value into the given memory location, copying and converting it
/// (if necessary) from the given big-endian integer value.
template <endian_swappable_integer T>
void write_big_as_host(T val, void* to) {
    big_to_host_inplace(val);
    std::memcpy(to, &val, sizeof(T));
}

template <endian_swappable_integer T>
constexpr void write_big_as_host(T val, T* to) {
    to = big_to_host(val);
}

}  // namespace oxenc
