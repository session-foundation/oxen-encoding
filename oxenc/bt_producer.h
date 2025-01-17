#pragma once

#include <cassert>
#include <charconv>
#include <concepts>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

#ifndef NDEBUG
#include "bt_serialize.h"
#endif
#include "bt_common.h"
#include "common.h"
#include "span.h"
#include "variant.h"

namespace oxenc {

class bt_dict_producer;

#if defined(__APPLE__) && defined(__MAC_OS_X_VERSION_MIN_REQUIRED) && \
        __MAC_OS_X_VERSION_MIN_REQUIRED < 101500
#define OXENC_APPLE_TO_CHARS_WORKAROUND
/// Really simplistic version of std::to_chars on Apple, because Apple doesn't allow `std::to_chars`
/// to be used if targetting anything before macOS 10.15.  The buffer must have at least 20 chars of
/// space (for int types up to 64-bit); we return a pointer one past the last char written.
template <typename IntType>
char* apple_to_chars10(char* buf, IntType val) {
    static_assert(std::integral<IntType> && sizeof(IntType) <= 8);
    if constexpr (std::signed_integral<IntType>) {
        if (val < 0) {
            buf[0] = '-';
            return apple_to_chars10(buf + 1, static_cast<std::make_unsigned_t<IntType>>(-val));
        }
    }

    // write it to the buffer in reverse (because we don't know how many chars we'll need yet, but
    // writing in reverse will figure that out).
    char* pos = buf;
    do {
        *pos++ = '0' + static_cast<char>(val % 10);
        val /= 10;
    } while (val > 0);

    // Reverse the digits into the right order
    int swaps = (pos - buf) / 2;
    for (int i = 0; i < swaps; i++)
        std::swap(buf[i], pos[-1 - i]);

    return pos;
}
#endif

namespace detail {

    template <typename Class, detail::sign_func_hook SignFunc, typename... AppendArgs>
    void append_signature_helper(Class& self, SignFunc sign, AppendArgs&&... app_args) {
        using traits = function_traits<SignFunc>;
        using InputT = typename traits::template argument_type<0>;
        using CharIn = typename InputT::value_type;
        using RetT = typename std::remove_cvref_t<typename traits::return_type>;

        RetT sig;

        if constexpr (detail::char_view_type<InputT>)
            sig = sign(self.template view_for_signing<typename InputT::value_type>());
        else
            sig = sign(self.template span_for_signing<CharIn>());

        if constexpr (const_span_convertible<RetT>) {
            self.append(
                    std::forward<AppendArgs>(app_args)...,
                    const_span<char>{reinterpret_cast<const char*>(sig.data()), sig.size()});
        } else {
            static_assert(
                    std::is_convertible_v<RetT, std::string_view>,
                    "Signing function requires char-view-convertible or const-span-convertible "
                    "return type!");
            self.append(
                    std::forward<AppendArgs>(app_args)...,
                    std::string_view{reinterpret_cast<const char*>(sig.data()), sig.size()});
        }
    }
}  // namespace detail

template <typename T>
concept encoded_input = detail::char_view_type<T> || const_span_type<T>;

/// Class that allows you to build a bt-encoded list manually, optionally without copying or
/// allocating memory.  This is essentially the reverse of bt_list_consumer: where it lets you
/// stream-parse a buffer, this class lets you build directly into a buffer.
///
/// Out-of-buffer-space errors throw std::length_error when using an external buffer.
class bt_list_producer {
    friend class bt_dict_producer;

    // For external buffer mode we keep pointers to the start position and past-the-end positions.
    struct buf_span {
        char* const init;
        char* const end;
    };

    // Our output type: either external buffer pointers, or a string that we build:
    using output = std::variant<std::string, buf_span>;

    // Our data for the root list is either a begin/end pointer pair or a string; for
    // sublists it is a pointer to the parent list/dict.
    std::variant<output, bt_list_producer*, bt_dict_producer*> data;

    // Reference to the output; this is simply a reference to the value inside `data` for the
    // root element, and a pointer to the root's value for sublists/subdicts.
    output& out;

    // True indicates we have an open child list/dict
    bool has_child = false;

    // If we have a dict or list parent, this returns a pointer it (for a dict, static cast to
    // the base bt_list_producer type).  nullptr if no parent.
    bt_list_producer* parent();

    // The range that contains this currently serialized value; `from` is the offset (relative
    // to the beginning of the buffer, or beginning of the string) wherever the `l` was written
    // that started this list; `next` is the offset where the next value goes (which will always
    // have a closing `e` in it, to close the list; the `e` gets overwrite upon appending
    // another element).
    const size_t from;
    size_t next{from};

    // Sublist constructors
    explicit bt_list_producer(bt_list_producer* parent, char prefix = 'l');
    explicit bt_list_producer(bt_dict_producer* parent, char prefix = 'l');

    // Internal common constructor for both list and dict producer for external buffer mode.
    bt_list_producer(char* begin, char* end, char prefix);

    // Internal common constructor for both list and dict producer for expandable std::string
    // buffer mode.
    explicit bt_list_producer(char prefix, size_t reserve);

    // Does the actual appending to the buffer, and throwing if we'd overrun.
    void buffer_append(std::string_view d);

    // Appends to buffer, throws on overrun/
    void buffer_append(const char* data, size_t size);

    // Appends the 'e's into the buffer to close off open sublists/dicts *without* advancing the
    // buffer position; we do this after each append so that the buffer always contains valid
    // encoded data, even while we are still appending to it, and so that appending something raises
    // a length_error if appending it would not leave enough space for the required e's to close the
    // open list(s)/dict(s).
    void append_intermediate_ends();

    // Writes an integer to the given buffer; returns the one-past-the-data pointer.  Up to 20 bytes
    // will be written and must be available in buf.  Used for both string and integer
    // serialization.
    template <typename IntType>
    char* write_integer(IntType val, char* buf) {
        static_assert(sizeof(IntType) <= 64);

#ifndef OXENC_APPLE_TO_CHARS_WORKAROUND
        auto [ptr, ec] = std::to_chars(buf, buf + 20, val);
        assert(ec == std::errc());
        return ptr;
#else
        // Hate apple.
        return apple_to_chars10(buf, val);
#endif
    }

    // Serializes an integer value and appends it to the output buffer.  Does not call
    // append_intermediate_ends().
    template <std::integral IntType>
    void append_impl(IntType val) {
        if constexpr (std::same_as<IntType, bool>)
            buffer_append(val ? "i1e"sv : "i0e"sv);
        else {
            char buf[22];  // 'i' + base10 representation + 'e'
            buf[0] = 'i';
            auto* ptr = write_integer(val, buf + 1);
            *ptr++ = 'e';
            buffer_append({buf, static_cast<size_t>(ptr - buf)});
        }
    }

    // Appends string values, but does not call append_intermediate_ends()
    void append_impl(const char* data, size_t size) {
        char buf[21];  // length + ':'
        auto* ptr = write_integer(size, buf);
        *ptr++ = ':';
        buffer_append(buf, static_cast<size_t>(ptr - buf));
        buffer_append(data, size);
    }

    void append_impl(const unsigned char* data, size_t size) {
        append_impl(reinterpret_cast<const char*>(data), size);
    }

    void append_impl(const std::byte* data, size_t size) {
        append_impl(reinterpret_cast<const char*>(data), size);
    }

    template <const_span_convertible T>
    void append_impl(const T& v) {
        std::span<const typename T::value_type> s{v};
        append_impl(reinterpret_cast<const char*>(s.data()), s.size());
    }

  public:
    /// Returns a span of the current serialized list values suitable for signing.  The returned
    /// value is the currently serialized list data up to but not including the terminating `e`
    /// (since that `e` will be overwritten if another item, i.e. a signature, is appended), and
    /// thus includes all values added to the list so far.  Typically this doesn't need to be used
    /// directly but rather can use `append_signature` to generate an append a signature over a
    /// list's prior elements.
    template <basic_char CharT = char>
    const_span<CharT> span_for_signing() const {
        auto s = span<CharT>();
        return s.first(s.size() - 1);
    }

    template <typename Char = char>
    std::basic_string_view<Char> view_for_signing() const {
        auto v = view<Char>();
        v.remove_suffix(1);
        return v;
    }

  public:
    bt_list_producer(const bt_list_producer&) = delete;
    bt_list_producer& operator=(const bt_list_producer&) = delete;
    bt_list_producer& operator=(bt_list_producer&&) = delete;
    bt_list_producer(bt_list_producer&& other);

    /// Constructs a list producer that writes into the range [begin, end).  If a write would go
    /// beyond the end of the buffer an exception is raised.  Note that this will happen during
    /// construction if the given buffer is not large enough to contain the `le` encoding of an
    /// empty list.
    bt_list_producer(char* begin, char* end) : bt_list_producer{begin, end, 'l'} {}

    /// Constructs a list producer that writes into the range [begin, begin+size).  If a write would
    /// go beyond the end of the buffer an exception is raised.
    bt_list_producer(char* begin, size_t len) : bt_list_producer{begin, begin + len, 'l'} {}

    /// Constructs a list producer that writes to an internal, expandable string.  `reserve` can
    /// be passed a non-zero value to reserve an initial size in the std::string.
    explicit bt_list_producer(size_t reserve = 0) : bt_list_producer{'l', reserve} {}

    ~bt_list_producer();

    /// Returns a const_span into the currently serialized data buffer.  Note that the returned
    /// span includes the `e` list end serialization markers which will be overwritten if the list
    /// (or an active sublist/subdict) is appended to.  Can optionally return a const_span of
    /// a char-like type other than char for convenience.
    template <basic_char CharT = char>
    const_span<CharT> span() const {
        const char* x;
        if (auto* s = std::get_if<std::string>(&out))
            x = s->data();
        else
            x = var::get<buf_span>(out).init;

        return const_span<CharT>{reinterpret_cast<const CharT*>(x) + from, next - from + 1};
    }

    /// Returns a string_view into the currently serialized data buffer.  Note that the returned
    /// view includes the `e` list end serialization markers which will be overwritten if the list
    /// (or an active sublist/subdict) is appended to.  Can optionally return a basic_string_view of
    /// a char-like type other than char for convenience.
    template <basic_char Char = char>
    std::basic_string_view<Char> view() const {
        const char* x;
        if (auto* s = std::get_if<std::string>(&out))
            x = s->data();
        else
            x = var::get<buf_span>(out).init;

        return std::basic_string_view<Char>{
                reinterpret_cast<const Char*>(x) + from, next - from + 1};
    }

    /// Extracts the string, when not using buffer mode.  This is only usable on the root
    /// list/dict producer, and may only be used in rvalue context, as it destroys the internal
    /// buffer, such as: std::move(producer).str().  Throws logic_error if called on a
    /// sublist/subdict, or on a external buffer producer.
    ///
    /// (If you just want a copy of the string, use `view()` instead).
    std::string str() && {
        if (parent())
            throw std::logic_error{"Cannot call bt_producer .str() on a sublist/subdict"};
        auto* s = std::get_if<std::string>(&out);
        if (!s)
            throw std::logic_error{"Cannot call bt_producer .str() when using an external buffer"};

        std::string ret;
        ret.swap(*s);
        // Leave behind an empty producer
        *s += ret[0];
        *s += 'e';
        next = 1;
        return ret;
    }

    /// Returns a reference to the `std::string`, when in string-builder mode.  Unlike `str()`, this
    /// method *can* be used on a subdict/sublist, but always returns a reference to the root
    /// object's string (unlike `.view()` which just returns the view of the current sub-producer).
    const std::string& str_ref() {
        if (auto* p = parent())
            return p->str_ref();
        if (auto* s = std::get_if<std::string>(&out))
            return *s;
        throw std::logic_error{"Cannot call bt_producer .str_ref() when using an external buffer"};
    }

    /// Calls `.reserve()` on the underlying std::string, if using string-builder mode.
    void reserve(size_t new_cap) {
        if (auto* p = parent())
            return p->reserve(new_cap);
        if (auto* s = std::get_if<std::string>(&out))
            s->reserve(new_cap);
    }

    /// Returns the end position in the buffer.  (This is primarily useful for external buffer
    /// mode, but still works in string mode).
    const char* end() const {
        if (auto* s = std::get_if<std::string>(&out))
            return s->data() + next + 1;
        auto* bs = std::get_if<buf_span>(&out);
        assert(bs);
        return bs->init + next + 1;
    }

    template <size_t N>
    void append(const char (&h)[N]) {
        if (has_child)
            throw std::logic_error{"Cannot append to list when a sublist is active"};
        append_impl(h, N - 1);
        append_intermediate_ends();
    }

    /// Appends an element containing binary string data
    template <const_span_convertible T>
    void append(const T& data) {
        if (has_child)
            throw std::logic_error{"Cannot append to list when a sublist is active"};
        append_impl(data.data(), data.size());
        append_intermediate_ends();
    }

    /// Appends an element containing a const Char string
    void append(std::string_view s) {
        if (has_child)
            throw std::logic_error{"Cannot append to list when a sublist is active"};
        append_impl(s);
        append_intermediate_ends();
    }

    /// Appends an element containing span data
    template <basic_char T>
    void append(const std::span<T>& data) {
        if (has_child)
            throw std::logic_error{"Cannot append to list when a sublist is active"};
        append_impl(data);
        append_intermediate_ends();
    }

    /// Appends an integer (including bools)
    template <std::integral IntType>
    void append(IntType i) {
        if (has_child)
            throw std::logic_error{"Cannot append to list when a sublist is active"};
        append_impl(i);
        append_intermediate_ends();
    }

    template <size_t N>
    bt_list_producer& operator+=(const char (&h)[N]) {
        append(h);
        return *this;
    }

    bt_list_producer& operator+=(std::string_view s) {
        append(s);
        return *this;
    }

    template <const_span_convertible T>
    bt_list_producer& operator+=(const T& data) {
        append(data);
        return *this;
    }

    template <basic_char T>
    bt_list_producer& operator+=(const std::span<T>& data) {
        append(data);
        return *this;
    }

    template <std::integral IntType>
    bt_list_producer& operator+=(IntType i) {
        append(i);
        return *this;
    }

    /// Appends elements from the range [from, to) to the existing list (i.e. without creating a
    /// sublist).
    template <typename ForwardIt>
    void extend(ForwardIt from, ForwardIt to) {
        if (has_child)
            throw std::logic_error{"Cannot append to list when a sublist is active"};
        while (from != to)
            append_impl(*from++);
        append_intermediate_ends();
    }

    /// Appends elements from the input container to the existing list (i.e. without creating a
    /// sublist).
    template <bt_input_list_container L>
    void extend(const L& list) {
        extend(list.begin(), list.end());
    }

    // Deprecated alias for `extend(...)`.  This does *not* append the elements as a sublist.
    template <typename ForwardIt>
    [[deprecated("Use extend instead")]] void append(ForwardIt from, ForwardIt to) {
        extend(from, to);
    }

    /// Appends an input list container as a sublist.  Equivalenet to append_list(tuple).  Note that
    /// this is not equivalent to the iterator pair overload above: that appends to the current
    /// list, while this one creates a new sublist and appends the container elements to that.
    template <bt_input_list_container L>
    void append(const L& list) {
        append_list(list);
    }

    /// Appends a tuple/pair/array as a sublist.  Equivalent to append_list(tuple).
    template <tuple_like Tuple>
    void append(const Tuple& tuple) {
        append_list(tuple);
    }

    /// Appends an optional value: the value will be appended as if by calling `.append(*val)` if
    /// the optional is set, and otherwise (i.e. if given nullopt) nothing is appended.
    template <typename T>
    void append(const std::optional<T>& val) {
        if (val)
            append(*val);
    }

    /// Appends a sublist to this list.  Returns a new bt_list_producer that references the parent
    /// list.  The parent cannot be added to until the sublist is destroyed.  This is meant to be
    /// used via RAII:
    ///
    ///     buf data[16];
    ///     bt_list_producer list{data, sizeof(data)};
    ///     {
    ///         auto sublist = list.append_list();
    ///         sublist.append(42);
    ///     }
    ///     list.append(1);
    ///     // `data` now contains: `lli42eei1ee`
    ///
    /// If doing more complex lifetime management, take care not to allow the child instance to
    /// outlive the parent.
    bt_list_producer append_list();

    template <typename ForwardIt>
    void append_list(ForwardIt from, ForwardIt to) {
        append_list().extend(from, to);
    }

    /// Appends a compatible input list container as a sublist
    template <bt_input_list_container L>
    void append_list(const L& input) {
        append_list(input.begin(), input.end());
    }

    /// Appends a tuple/pair/array as a sublist
    template <tuple_like Tuple>
    void append_list(const Tuple& t);

    /// Appends a dict to this list.  Returns a new bt_dict_producer that references the parent
    /// list.  The parent cannot be added to until the subdict is destroyed.  This is meant to be
    /// used via RAII (see append_list() for details).
    ///
    /// If doing more complex lifetime management, take care not to allow the child instance to
    /// outlive the parent.
    bt_dict_producer append_dict();

    /// There is no append_dict() taking a generic dict-like object because we can't ensure it
    /// iterates in sorted order, and therefore we can't append it without copying into a sorted
    /// container.
    template <bt_input_dict_container D>
    void append_dict(const D&) = delete;

    /// Appends a bt_value, bt_dict, or bt_list to this bt_list.  You must include the
    /// bt_value_producer.h header (either directly or via bt.h) to use this method.
    template <typename T>
    void append_bt(const T& bt);

    /// Appends a signature of the previous list values to the list, calling the given invocable
    /// object to obtain the signature.
    ///
    /// The signing callable will be invoked with a std::basic_string_view<C> of the value to be
    /// signed, with C allowed to be any of `char`, `unsigned char`, or `std::byte`.
    ///
    /// The signing callable must return either a C string literal or a container of single-byte
    /// elements with contiguous storage with `data()` and `size()` members; e.g. `std::string`,
    /// `std::basic_string_view<std::byte>`, `std::array<unsigned char, 32>` and so on.
    template <detail::sign_func_hook SignFunc>
    void append_signature(SignFunc&& sign) {
        detail::append_signature_helper(*this, std::forward<SignFunc>(sign));
    }

    /// Appends an already bt-encoded string as-is to the list.  This is useful for signed
    /// encoded data which you may not know how to perfectly re-encode after decoding, i.e.
    /// a bt-dict where you only know about a subset of the keys.  In theory, one could iterate
    /// every key and decode the dict recursively, keeping every value without knowing what it
    /// is, and then reconstitute the original dict using the result of this.  That would be
    /// messy, tedious, and kinda a waste of time.
    ///
    /// Caveat emptor: this can *absolutely* be a foot-shotgun.  The rest of this class is
    /// designed such that if it *does* give you an output bt-encoded string, it *will* be
    /// valid bt-encoding.  This method violates that property.
    template <encoded_input T>
    void append_encoded(T encoded) {
#ifndef NDEBUG
        // on debug build, throw if `encoded` is invalid bt-encoded data
        (void)bt_deserialize<bt_value>(encoded);
#endif
        buffer_append(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        append_intermediate_ends();
    }
};

/// Class that allows you to build a bt-encoded dict manually, without copying or allocating memory.
/// This is essentially the reverse of bt_dict_consumer: where it lets you stream-parse a buffer,
/// this class lets you build directly into a buffer that you own.
///
/// Note that bt-encoded dicts *must* be produced in (ASCII) ascending key order, but that this is
/// only tracked/enforced for non-release builds (i.e. without -DNDEBUG).
class bt_dict_producer : bt_list_producer {
    friend class bt_list_producer;

    // Subdict constructors
    bt_dict_producer(bt_list_producer* parent) : bt_list_producer{parent, 'd'} {}
    bt_dict_producer(bt_dict_producer* parent) : bt_list_producer{parent, 'd'} {}

    // Checks a just-written key string to make sure it is monotonically increasing from the last
    // key.  Does nothing in a release build.  (The string is outside the defines because otherwise
    // we'd have a ODR violation between debug and non-debug builds).
    std::string last_key;
#ifdef NDEBUG
    constexpr void check_incrementing_key(std::string_view) const {}
#else
    void check_incrementing_key(std::string_view this_key) {
        assert(last_key.empty() || this_key > last_key);
        last_key = this_key;
    }
#endif

  public:
    /// Constructs a dict producer that writes into the range [begin, end).  If a write would go
    /// beyond the end of the buffer an exception is raised.  Note that this will happen during
    /// construction if the given buffer is not large enough to contain the `de` encoding of an
    /// empty list.
    bt_dict_producer(char* begin, char* end) : bt_list_producer{begin, end, 'd'} {}

    /// Constructs a dict producer that writes into the range [begin, begin+size).  If a write would
    /// go beyond the end of the buffer an exception is raised.
    bt_dict_producer(char* begin, size_t len) : bt_list_producer{begin, begin + len, 'd'} {}

    /// Constructs a dict producer that writes to an internal, expandable string.  `reserve` can
    /// be passed a non-zero value to reserve an initial size in the std::string.
    explicit bt_dict_producer(size_t reserve = 0) : bt_list_producer{'d', reserve} {}

    template <basic_char CharT = char>
    const_span<CharT> span() const {
        return bt_list_producer::span<CharT>();
    }

    /// Returns a string_view (or basic_string_view<Char>) into the currently serialized data
    /// buffer.  Note that the returned view includes the `e` dict end serialization markers which
    /// will be overwritten if the dict (or an active sublist/subdict) is appended to.
    template <basic_char Char = char>
    std::basic_string_view<Char> view() const {
        return bt_list_producer::view<Char>();
    }

    /// Extracts the string, when not using buffer mode.  This is only usable on the root
    /// list/dict producer, and may only be used in rvalue context, as it destroys the internal
    /// buffer, such as: std::move(producer).str().  Throws logic_error if called on a
    /// sublist/subdict, or on a external buffer producer.
    ///
    /// (If you just want a copy of the string, use `view()` instead).
    std::string str() && {
#ifndef NDEBUG
        last_key = {};
#endif
        return std::move(*this).bt_list_producer::str();
    }

    /// Returns a reference to the `std::string`, when in string-builder mode.  Unlike `str()`, this
    /// method *can* be used on a subdict/sublist, but always returns a reference to the root
    /// object's string (unlike `.view()` which just returns the view of the current sub-producer).
    const std::string& str_ref() { return bt_list_producer::str_ref(); }

    /// Calls `.reserve()` on the underlying std::string, if using string-builder mode.
    void reserve(size_t new_cap) { bt_list_producer::reserve(new_cap); }

    /// Returns a const_span of the current serialized dict keys/values suitable for signing.  The
    /// returned value is the currently serialized dict data up to but not including the terminating
    /// `e` (since that `e` will be overwritten if another key is appended), and thus includes all
    /// keys and values added to the dict so far.  Typically this doesn't need to be used directly
    /// but rather can use `append_signature` to generate an append a signature over a dict's prior
    /// fields.
    template <typename CharT = char>
    const_span<CharT> span_for_signing() const {
        return bt_list_producer::span_for_signing<CharT>();
    }

    template <typename Char = char>
    std::basic_string_view<Char> view_for_signing() const {
        return bt_list_producer::view_for_signing<Char>();
    }

    /// Returns the end position in the buffer.
    const char* end() const { return bt_list_producer::end(); }

    /// Appends a key-value pair with a string literal value. The key must be > than the last key
    /// added, but this is only enforced (with an assertion) in debug builds.
    // template <basic_char T, size_t N>
    void append(std::string_view key, std::string_view value) {
        if (has_child)
            throw std::logic_error{"Cannot append to list when a sublist is active"};
        check_incrementing_key(key);
        append_impl(key);
        append_impl(value);
        append_intermediate_ends();
    }

    template <size_t N>
    void append(std::string_view key, const char (&h)[N]) {
        if (has_child)
            throw std::logic_error{"Cannot append to list when a sublist is active"};
        check_incrementing_key(key);
        append_impl(key);
        append_impl(h, N - 1);
        append_intermediate_ends();
    }

    /// Appends a key-value pair with a string or integer value.  The key must be > than the last
    /// key added, but this is only enforced (with an assertion) in debug builds.
    template <typename T>
        requires const_span_convertible<T> || std::integral<T>
    void append(std::string_view key, const T& value) {
        if (has_child)
            throw std::logic_error{"Cannot append to list when a sublist is active"};
        check_incrementing_key(key);
        append_impl(key);
        append_impl(value);
        append_intermediate_ends();
    }

    /// Appends a key-value pair with a const_span value.  The key must be > the last key added,
    /// but this is only enforced (with an assertion) in debug builds.
    template <basic_char T>
    void append(std::string_view key, const std::span<T>& value) {
        if (has_child)
            throw std::logic_error{"Cannot append to list when a sublist is active"};
        check_incrementing_key(key);
        append_impl(key);
        append_impl(value);
        append_intermediate_ends();
    }

    /// Appends an input list container as a sublist.  Equivalenet to append_list(tuple).  Note that
    /// this is not equivalent to the iterator pair overload above: that appends to the current
    /// list, while this one creates a new sublist and appends the container elements to that.
    template <bt_input_list_container L>
    void append(std::string_view key, const L& list) {
        append_list(key, list);
    }

    /// Appends a tuple/pair/array as a sublist.  Equivalent to append_list(tuple).
    template <tuple_like Tuple>
    void append(std::string_view key, const Tuple& tuple) {
        append_list(key, tuple);
    }

    /// Appends a key-value pair with an optional value, *if* the optional is set.  If the value is
    /// nullopt, nothing is appended.
    template <typename T>
    void append(std::string_view key, const std::optional<T>& value) {
        if (value)
            append(key, *value);
    }

    /// Appends pairs from the range [from, to) to the dict.  Elements must have a .first
    /// convertible to a string_view, and a .second that is either string view convertible or an
    /// integer.  This does *not* append the elements as a subdict: for that you should use
    /// something like: `l.append_dict().extend(key, from, to);`
    ///
    /// Also note that the range *must* be sorted by keys, which means either using an ordered
    /// container (e.g. std::map) or a manually ordered container (such as a vector or list of
    /// pairs).  unordered_map, however, is not acceptable.
    template <ordered_pair_iterator ForwardIt>
    void extend(ForwardIt from, ForwardIt to) {
        if (has_child)
            throw std::logic_error{"Cannot append to list when a sublist is active"};
        while (from != to) {
            const auto& [k, v] = *from++;
            check_incrementing_key(k);
            append_impl(k);
            append_impl(v);
        }
        append_intermediate_ends();
    }

    template <ordered_pair_iterator ForwardIt>
    [[deprecated("Use extend instead")]] void append(ForwardIt from, ForwardIt to) {
        extend(from, to);
    }

    /// Appends a sub-dict value to this dict with the given key.  Returns a new bt_dict_producer
    /// that references the parent dict.  The parent cannot be added to until the subdict is
    /// destroyed.  Key must be (ascii-comparison) larger than the previous key.
    ///
    /// This is meant to be used via RAII:
    ///
    ///     buf data[32];
    ///     bt_dict_producer dict{data, sizeof(data)};
    ///     {
    ///         auto subdict = dict.begin_dict("myKey");
    ///         subdict.append("x", 42);
    ///     }
    ///     dict.append("y", "");
    ///     // `data` now contains: `d5:myKeyd1:xi42ee1:y0:e`
    ///
    /// If doing more complex lifetime management, take care not to allow the child instance to
    /// outlive the parent.
    bt_dict_producer append_dict(std::string_view key) {
        if (has_child)
            throw std::logic_error{
                    "Cannot call append_dict while another nested list/dict is active"};
        check_incrementing_key(key);
        append_impl(key);
        return bt_dict_producer{this};
    }

    /// Appends a list to this dict with the given key (which must be ascii-larger than the previous
    /// key).  Returns a new bt_list_producer that references the parent dict.  The parent cannot be
    /// added to until the sublist is destroyed.
    ///
    /// This is meant to be used via RAII (see append_dict() for details).
    ///
    /// If doing more complex lifetime management, take care not to allow the child instance to
    /// outlive the parent.
    bt_list_producer append_list(std::string_view key) {
        if (has_child)
            throw std::logic_error{
                    "Cannot call append_list while another nested list/dict is active"};
        check_incrementing_key(key);
        append_impl(key);
        return bt_list_producer{this};
    }

    /// Appends a list to this dict with the given key (which must be ascii-larger than the previous
    /// key), and then appends the given iterator range to it.
    template <typename ForwardIt>
    void append_list(std::string_view key, ForwardIt from, ForwardIt to) {
        append_list(key).extend(from, to);
    }

    /// Appends a list to this dict with the given key (which must be ascii-larger than the previous
    /// key), and then appends all the element of the given input list compatible container to it.
    template <bt_input_list_container L>
    void append_list(std::string_view key, const L& list) {
        append_list(key, list.begin(), list.end());
    }

    /// Appends a tuple/pair/array as a sublist with the given key.
    template <tuple_like Tuple>
    void append_list(std::string_view key, const Tuple& tuple);

    /// Appends a bt_value, bt_dict, or bt_list to this bt_dict.  You must include the
    /// bt_value_producer.h header (either directly or via bt.h) to use this method.
    template <typename T>
    void append_bt(std::string_view key, const T& bt);

    /// Appends a signature of the previous dict keys/values to the list, calling the given
    /// invocable object to obtain the signature.
    ///
    /// The signing callable will be invoked with a std::basic_string_view<C> of the value to be
    /// signed, with C allowed to be any of `char`, `unsigned char`, or `std::byte`.
    ///
    /// The signing callable must return either a C string literal or a container of single-byte
    /// elements with contiguous storage with `data()` and `size()` members; e.g. `std::string`,
    /// `std::basic_string_view<std::byte>`, `std::array<unsigned char, 32>` and so on.
    ///
    /// Since the signature signs all previous values, it is typically recommended that the
    /// signature use a late-sorting key; "~" (which is 0x7e, and the last printable 7-bit ascii
    /// value) is suggested.

    template <detail::sign_func_hook SignFunc>
    void append_signature(std::string_view key, SignFunc&& sign) {
        detail::append_signature_helper(*this, std::forward<SignFunc>(sign), key);
    }

    /// Appends an already bt-encoded string as-is to the dict.  This is useful for signed
    /// encoded data which you may not know how to perfectly re-encode after decoding, i.e.
    /// a bt-dict where you only know about a subset of the keys.  In theory, one could iterate
    /// every key and decode the dict recursively, keeping every value without knowing what it
    /// is, and then reconstitute the original dict using the result of this.  That would be
    /// messy, tedious, and kinda a waste of time.
    ///
    /// Caveat emptor: this can *absolutely* be a foot-shotgun.  The rest of this class is
    /// designed such that if it *does* give you an output bt-encoded string, it *will* be
    /// valid bt-encoding.  This method violates that property.
    template <encoded_input T>
    void append_encoded(std::string_view key, T encoded) {
        check_incrementing_key(key);
        append_impl(key);
        bt_list_producer::append_encoded(encoded);
    }
};

inline bt_list_producer::bt_list_producer(bt_list_producer* parent, char prefix) :
        data{parent}, out{parent->out}, from{parent->next} {
    parent->has_child = true;
    buffer_append(std::string_view{&prefix, 1});
    append_intermediate_ends();
    for (; parent; parent = parent->parent())
        parent->next++;
}

inline bt_list_producer::bt_list_producer(bt_dict_producer* parent, char prefix) :
        bt_list_producer{static_cast<bt_list_producer*>(parent), prefix} {
    data = parent;
}

inline bt_list_producer::bt_list_producer(bt_list_producer&& other) :
        data{std::move(other.data)}, out{other.out}, from{other.from}, next{other.next} {
    if (other.has_child)
        throw std::logic_error{"Cannot move bt_list/dict_producer with active sublists/subdicts"};
    var::visit(
            [](auto& x) {
                if constexpr (!std::same_as<output&, decltype(x)>)
                    x = nullptr;
            },
            other.data);
}

inline bt_list_producer* bt_list_producer::parent() {
    if (auto* parent = std::get_if<bt_list_producer*>(&data))
        return *parent;
    if (auto* parent = std::get_if<bt_dict_producer*>(&data))
        return static_cast<bt_list_producer*>(*parent);
    return nullptr;
}

inline void bt_list_producer::buffer_append(const char* data, size_t size) {
    if (auto* s = std::get_if<std::string>(&out)) {
        s->resize(next);  // Truncate any trailing e's
        s->append(data, size);
    } else {
        auto* bs = std::get_if<buf_span>(&out);
        assert(bs);
        auto avail = static_cast<size_t>(std::distance(bs->init + next, bs->end));
        if (size > avail)
            throw std::length_error{"Cannot write bt_producer: buffer size exceeded"};
        std::copy(data, data + size, bs->init + next);
    }
    for (auto* p = this; p; p = p->parent())
        p->next += size;
}

inline void bt_list_producer::buffer_append(std::string_view d) {
    if (auto* s = std::get_if<std::string>(&out)) {
        s->resize(next);  // Truncate any trailing e's
        s->append(d);
    } else {
        auto* bs = std::get_if<buf_span>(&out);
        assert(bs);
        auto avail = static_cast<size_t>(std::distance(bs->init + next, bs->end));
        if (d.size() > avail)
            throw std::length_error{"Cannot write bt_producer: buffer size exceeded"};
        std::copy(d.begin(), d.end(), bs->init + next);
    }
    for (auto* p = this; p; p = p->parent())
        p->next += d.size();
}

inline void bt_list_producer::append_intermediate_ends() {
    size_t count = 0;
    for (auto* p = this; p; p = p->parent())
        count++;

    if (auto* s = std::get_if<std::string>(&out))
        s->append(count, 'e');
    else {
        auto* bs = std::get_if<buf_span>(&out);
        assert(bs);
        auto* begin = bs->init + next;
        auto* end = begin + count;
        if (end > bs->end)
            throw std::length_error{"Cannot write bt_producer: buffer size exceeded"};
        std::fill(begin, end, 'e');
    }
}

inline bt_list_producer::~bt_list_producer() {
    auto* p = parent();
    if (!p)
        return;
    assert(!has_child);
    assert(p->has_child);
    p->has_child = false;
}

inline bt_list_producer::bt_list_producer(char* begin, char* end, char prefix) :
        data{buf_span{begin, end}}, out{*std::get_if<output>(&data)}, from{0}, next{0} {
    buffer_append(std::string_view{&prefix, 1});
    append_intermediate_ends();
}

inline bt_list_producer::bt_list_producer(char prefix, size_t reserve) :
        data{std::string{}}, out{*std::get_if<output>(&data)}, from{0}, next{0} {
    if (reserve > 0)
        std::get_if<std::string>(&out)->reserve(reserve);
    buffer_append(std::string_view{&prefix, 1});
    append_intermediate_ends();
}

inline bt_list_producer bt_list_producer::append_list() {
    if (has_child)
        throw std::logic_error{"Cannot call append_list while another nested list/dict is active"};
    return bt_list_producer{this};
}

inline bt_dict_producer bt_list_producer::append_dict() {
    if (has_child)
        throw std::logic_error{"Cannot call append_dict while another nested list/dict is active"};
    return bt_dict_producer{this};
}

namespace detail {

    template <tuple_like Tuple, size_t... Is>
    inline void append_tuple(bt_list_producer l, const Tuple& t, std::index_sequence<Is...>) {
        (l.append(std::get<Is>(t)), ...);
    }

}  // namespace detail

template <tuple_like Tuple>
void bt_list_producer::append_list(const Tuple& t) {
    detail::append_tuple(append_list(), t, std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

template <tuple_like Tuple>
void bt_dict_producer::append_list(std::string_view key, const Tuple& t) {
    detail::append_tuple(append_list(key), t, std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

}  // namespace oxenc
