#include <array>
#include <cstddef>
#include <list>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "endian.h"

namespace oxenc {

// Basic types to help construct an RLP-serializable, recursive value.  These do *not* have to be
// used: you can pass ordinary C++ types (e.g. vectors of strings, etc.); these types are here to
// allow for arbitrary value encoding, including nesting.
struct rlp_value;
// List of RLP-serializable values:
using rlp_list = std::list<rlp_value>;
// Variant of any RLP-serializable value:
using rlp_variant = std::variant<std::string, std::string_view, uint64_t, rlp_list>;

// rlp_value is just the variant, but by using inheritance and a forward declaration we can make it
// recursive.
struct rlp_value : rlp_variant {
    using rlp_variant::rlp_variant;
    using rlp_variant::operator=;

    template <
            typename T,
            typename U = std::remove_reference_t<T>,
            std::enable_if_t<std::is_integral_v<U> && std::is_unsigned_v<U>, int> = 0>
    rlp_value(T&& uint) : rlp_variant{static_cast<uint64_t>(uint)} {}

    template <
            typename T,
            typename U = std::remove_reference_t<T>,
            std::enable_if_t<!std::is_integral_v<U>, int> = 0>
    rlp_value(T&& v) : rlp_variant{std::forward<T>(v)} {}

    rlp_value(const char* s) : rlp_value{std::string_view{s}} {}
};

namespace detail {
    template <typename T, typename = void>
    constexpr bool is_rlp_serializable = false;

    template <typename T>
    constexpr bool
            is_rlp_serializable<T, std::enable_if_t<std::is_unsigned_v<std::remove_cvref_t<T>>>> =
                    true;

    template <typename T>
    constexpr bool is_span = false;
    template <typename T>
    constexpr bool is_span<std::span<T>> = true;
    template <typename T>
    constexpr bool is_basic_char =
            sizeof(T) == 1 && (std::is_integral_v<T> || std::is_same_v<T, std::byte>);
    template <typename T>
    constexpr bool is_char_span = false;
    template <typename T>
    constexpr bool is_char_span<std::span<T>> = is_basic_char<std::remove_cv_t<T>>;
    template <typename T, typename = void>
    constexpr bool is_span_convertible = false;
    template <typename T>
    constexpr bool is_span_convertible<
            T,
            std::enable_if_t<
                    !is_span<T> &&
                    std::is_convertible_v<const T&, std::span<const typename T::value_type>>>> =
            true;

    template <typename T>
    constexpr bool is_list = false;
    template <typename T>
    constexpr bool is_list<std::list<T>> = true;

    template <typename T>
    constexpr bool is_rlp_serializable<T, std::enable_if_t<is_span_convertible<T>>> =
            is_rlp_serializable<std::span<const typename T::value_type>>;

    template <typename T>
    constexpr bool is_rlp_serializable<std::span<T>> =
            is_char_span<std::span<T>> || is_rlp_serializable<std::remove_cvref_t<T>>;

    template <typename T>
    constexpr bool is_rlp_serializable<std::list<T>> = is_rlp_serializable<std::remove_cvref_t<T>>;

    template <typename... Ts>
    constexpr bool is_rlp_serializable<std::variant<Ts...>> =
            (is_rlp_serializable<std::remove_cvref_t<Ts>> && ...);

    template <>
    inline constexpr bool is_rlp_serializable<rlp_value> = true;

    template <typename... T>
    constexpr bool is_variant = false;
    template <typename... T>
    constexpr bool is_variant<std::variant<T...>> = true;

    template <typename T>
    std::pair<std::array<char, sizeof(T)>, std::string_view> rlp_encode_integer(const T& val) {
        std::pair<std::array<char, sizeof(T)>, std::string_view> result;
        auto& [buf, length] = result;
        oxenc::write_host_as_big(val, buf.data());
        length = {buf.data(), buf.size()};
        while (!length.empty() && length[0] == 0)
            length.remove_prefix(1);
        return result;
    }
    std::string rlp_encode_payload(std::string_view payload, unsigned char base_code);

}  // namespace detail

template <typename T>
concept RLPSerializable = detail::is_rlp_serializable<T>;

/// Does rlp serialization of a serializable spannable container, string value, or unsigned integer
/// value.  If the container contains single-byte types (like a std::string, string_view, or even
/// std::vector<uint8_t>) then the value is interpreted as a string to be encoded.  (If you need to
/// pass a list of small values, use a larger integer type).
///
/// Also takes a variant of serializable types, and containers can recursively contain other
/// serializable types.
template <RLPSerializable T>
inline std::string rlp_serialize(const T& val) {
    using namespace detail;

    if constexpr (std::is_unsigned_v<T>) {
        auto [buf, v] = rlp_encode_integer(val);
        return rlp_serialize(v);
    } else if constexpr (is_char_span<T>) {
        std::string_view str{reinterpret_cast<const char*>(val.data()), val.size()};
        if (str.size() == 1 && static_cast<unsigned char>(str[0]) < 0x80)
            return std::string{str};
        return detail::rlp_encode_payload(str, 0x80u);
    } else if constexpr (is_span<T> || is_list<T>) {
        std::string payload;
        for (const auto& x : val)
            payload += rlp_serialize(x);
        return detail::rlp_encode_payload(payload, 0xc0u);
    } else if constexpr (is_span_convertible<T>) {
        std::span<const typename T::value_type> span = val;
        return rlp_serialize(span);
    } else if constexpr (is_variant<T>) {
        return std::visit([](const auto& x) { return rlp_serialize(x); }, val);
    } else if constexpr (std::is_same_v<rlp_value, T>) {
        // GCC 10 workaround; on gcc 11+/clang, the above case can deal with directly without first
        // needing the static to the base std::variant type (aka rlp_variant).
        return rlp_serialize(static_cast<const rlp_variant&>(val));
    } else {
        static_assert(std::is_void_v<T>, "Internal error: unhandled serializable type");
    }
}

inline std::string rlp_serialize(const char* str) {
    return rlp_serialize(std::string_view{str});
}

namespace detail {
    inline std::string rlp_encode_payload(std::string_view payload, unsigned char base_code) {
        std::string result;
        if (payload.size() <= 55) {
            result.reserve(1 + payload.size());
            result.push_back(base_code + payload.size());
        } else {

            auto [buf, length] = rlp_encode_integer(payload.size());
            result.reserve(1 + length.size() + payload.size());
            result.push_back(base_code + 55 + length.size());
            result += length;
        }
        result += payload;
        return result;
    }
}  // namespace detail

}  // namespace oxenc
