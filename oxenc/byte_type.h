#pragma once

// Specializations for assigning from a char into an output iterator, used by hex/base32z/base64
// decoding to bytes.

#include <iterator>
#include <type_traits>

namespace oxenc::detail {

// Fallback - we just try a char
template <typename OutputIt>
struct byte_type {
    using type = char;
};

template <typename OutputIt>
concept iterator_with_container = requires { typename OutputIt::container_type::value_type; };

// Support for things like std::back_inserter:
template <iterator_with_container OutputIt>
struct byte_type<OutputIt> {
    using type = typename OutputIt::container_type::value_type;
};

// iterator, raw pointers:
template <typename OutputIt>
    requires(
            !iterator_with_container<OutputIt> &&
            std::is_reference_v<typename std::iterator_traits<OutputIt>::reference>)
struct byte_type<OutputIt> {
    using type = std::remove_reference_t<typename std::iterator_traits<OutputIt>::reference>;
};

template <typename OutputIt>
using byte_type_t = typename byte_type<OutputIt>::type;

}  // namespace oxenc::detail
