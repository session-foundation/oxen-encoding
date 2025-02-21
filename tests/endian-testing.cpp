/**
    Oxen-encoding endian method testing binary
*/

// NOTE: purposely NOT including oxenc headers

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <climits>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>
#include <type_traits>

#ifdef __linux__
extern "C" {
#include <byteswap.h>
}
#endif

using namespace std::literals;
using time_point = std::chrono::steady_clock::time_point;

namespace test {
constexpr bool little_endian = std::endian::native == std::endian::little;
constexpr bool big_endian = !little_endian;

template <typename T>
concept endian_swappable_int =
        std::integral<T> && (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);

#if defined(__GNUC__) || defined(__clang__)
#define OXENC_HARD_INLINE [[gnu::always_inline]]
#elif defined(_MSC_VER)
#define OXENC_HARD_INLINE __forceinline
#else
#define OXENC_HARD_INLINE
#endif

template <endian_swappable_int T>
[[nodiscard]] OXENC_HARD_INLINE inline constexpr T byteswap_ranges(T val) noexcept {
    auto value = std::bit_cast<std::array<std::byte, sizeof(T)>>(val);
    std::ranges::reverse(value);
    return std::bit_cast<T>(value);
}

template <std::unsigned_integral T>
[[nodiscard]] OXENC_HARD_INLINE inline constexpr T byteswap_fallback1(T val) noexcept {
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
template <std::signed_integral T>
[[nodiscard]] OXENC_HARD_INLINE inline constexpr T byteswap_fallback1(T val) noexcept {
    return std::bit_cast<T>(byteswap_fallback1(std::bit_cast<std::make_unsigned_t<T>>(val)));
}

template <std::unsigned_integral T>
[[nodiscard]] OXENC_HARD_INLINE inline constexpr T byteswap_fallback2(T x) noexcept {
    if constexpr (sizeof(T) == 2)
        return (x >> 8) | ((x & 0xff) << 8);
    else if constexpr (sizeof(T) == 4)
        return ((x & 0xff000000u) >> 24)  // (comments for formatting)
             | ((x & 0x00ff0000u) >> 8)   //
             | ((x & 0x0000ff00u) << 8)   //
             | ((x & 0x000000ffu) << 24);
    else if constexpr (sizeof(T) == 8)
        return ((x & 0xff00000000000000ull) >> 56)  // (comments for formatting)
             | ((x & 0x00ff000000000000ull) >> 40)  //
             | ((x & 0x0000ff0000000000ull) >> 24)  //
             | ((x & 0x000000ff00000000ull) >> 8)   //
             | ((x & 0x00000000ff000000ull) << 8)   //
             | ((x & 0x0000000000ff0000ull) << 24)  //
             | ((x & 0x000000000000ff00ull) << 40)  //
             | ((x & 0x00000000000000ffull) << 56);
    else
        static_assert(sizeof(T) == 1);
}

template <std::signed_integral T>
[[nodiscard]] OXENC_HARD_INLINE inline constexpr T byteswap_fallback2(T val) noexcept {
    return std::bit_cast<T>(byteswap_fallback2(std::bit_cast<std::make_unsigned_t<T>>(val)));
}

enum class Mode { builtin, ranges, fallback1, fallback2, system };

#if defined(__GNUC__) || defined(__clang__)
#define OXENC_BYTESWAP16 __builtin_bswap16
#define OXENC_BYTESWAP32 __builtin_bswap32
#define OXENC_BYTESWAP64 __builtin_bswap64
#define OXENC_BUILTIN_BYTESWAP_IS_CONSTEXPR
#elif defined(_MSC_VER)
#define OXENC_BYTESWAP16 _byteswap_ushort
#define OXENC_BYTESWAP32 _byteswap_ulong
#define OXENC_BYTESWAP64 _byteswap_uint64
#endif

#ifdef OXENC_BYTESWAP64
template <endian_swappable_int T>
constexpr T byteswap_builtin(T val) {
#ifndef OXENC_BUILTIN_BYTESWAP_IS_CONSTEPXR
    if (std::is_constant_evaluated())
        return byteswap_fallback2(val);
#endif
    if constexpr (sizeof(T) == 2)
        return std::bit_cast<T>(OXENC_BYTESWAP16(std::bit_cast<uint16_t>(val)));
    else if constexpr (sizeof(T) == 4)
        return std::bit_cast<T>(OXENC_BYTESWAP32(std::bit_cast<uint32_t>(val)));
    else if constexpr (sizeof(T) == 8)
        return std::bit_cast<T>(OXENC_BYTESWAP64(std::bit_cast<uint64_t>(val)));
}
#endif

#ifdef __linux__
#define OXENC_HAS_BYTESWAP_SYSTEM
template <endian_swappable_int T>
constexpr inline T byteswap_system(T val) {
    if constexpr (sizeof(T) == 2)
        return std::bit_cast<T>(bswap_16(std::bit_cast<uint16_t>(val)));
    else if constexpr (sizeof(T) == 4)
        return std::bit_cast<T>(bswap_32(std::bit_cast<uint32_t>(val)));
    else if constexpr (sizeof(T) == 8)
        return std::bit_cast<T>(bswap_64(std::bit_cast<uint64_t>(val)));
}
#endif

template <Mode mode, endian_swappable_int T>
constexpr inline void byteswap_(T& val) {
    if constexpr (mode == Mode::builtin)
#ifdef OXENC_BYTESWAP64
        val = byteswap_builtin(val);
#else
        assert(!"builtin not supported on this platform");
#endif
    else if constexpr (mode == Mode::ranges)
        val = byteswap_ranges(val);
    else if constexpr (mode == Mode::fallback1)
        val = byteswap_fallback1(val);
    else if constexpr (mode == Mode::fallback2)
        val = byteswap_fallback2(val);
    else {
        static_assert(mode == Mode::system);
#ifdef OXENC_HAS_BYTESWAP_SYSTEM
        val = byteswap_system(val);
#else
        assert(!"system not supported on this platform");
#endif
    }
}

template <Mode mode, endian_swappable_int T>
constexpr void host_to_little_inplace(T& val) {
    if constexpr (!little_endian)
        byteswap_<mode>(val);
}

template <Mode mode, endian_swappable_int T>
constexpr void host_to_big_inplace(T& val) {
    if constexpr (!big_endian)
        byteswap_<mode>(val);
}

}  // namespace test

constexpr std::size_t N{100'000'000};
constexpr int ROUNDS =
#ifdef NDEBUG
        11;
#else
        1;
#endif

static constexpr auto PRECONTROL = "Pre-Control Warmup"sv;
static constexpr auto CONTROL = "Control"sv;
static constexpr auto RANGES = "std::ranges"sv;
static constexpr auto FALLBACK1 = "Fallback1"sv;
static constexpr auto FALLBACK2 = "Fallback2"sv;
static constexpr auto SYSTEMBSWAP = "byteswap.h"sv;

consteval inline uint64_t swapped_builtin(uint64_t x) {
    test::host_to_big_inplace<test::Mode::builtin>(x);
    return x;
}

static constexpr auto x1 = swapped_builtin(123);
static_assert(x1 == 0x7b00000000000000ul);

int main(int /* argc */, char** /* argv */) {
    std::mt19937_64 gen{12345};

    static_assert(N % 4 == 0, "N must be a multiple of 4 for RNG generation");

    using test::Mode;

    std::cout << "Randomly generating uint{16,32,64} arrays..." << std::endl;

    std::vector<uint16_t> rand_uint16s{};
    std::vector<uint32_t> rand_uint32s{};
    std::vector<uint64_t> rand_uint64s{};

    std::array<std::array<std::array<time_point, 2>, 3>, 7> times{};

    rand_uint16s.resize(N);
    rand_uint32s.resize(N);
    rand_uint64s.resize(N);
    for (size_t i = 0; i < N / 4; i++) {
        auto x = gen();
        rand_uint64s[i] = x;
        rand_uint32s[i] = x & 0xffffffff;
        rand_uint16s[i] = x & 0xffff;
    }

    auto run_test = []<Mode mode, std::integral T>(
                            std::string_view trial,
                            std::array<time_point, 2>& time_log,
                            std::vector<T>& rand_uints) {
        std::cout << "Starting test group: " << trial << std::endl;

        time_log[0] = std::chrono::steady_clock::now();

        T tmp{0};

        for (int i = 0; i < ROUNDS; i++) {
            for (auto& u : rand_uints)
                test::host_to_big_inplace<mode>(u);
            // Accumulate one of the values known only at runtime to prevent the compiler from
            // optimizing away double-swap iterations:
            tmp += rand_uints[rand_uints[0] % rand_uints.size()];
        }

        time_log[1] = std::chrono::steady_clock::now();

        // Actually use tmp so that the compiler can't optimize it away:
        fmt::print("Finished test group: {}{}\n", trial, tmp == 123 ? "" : ".");
    };

    auto run_tests = [&times, &run_test]<typename T>(std::vector<T>& rand_uints) {
        size_t time_idx = sizeof(T) == 2 ? 0 : sizeof(T) == 4 ? 1 : 2;

        fmt::print("Running uint{}_t tests ({} iterations)\n", 8 * sizeof(T), ROUNDS * N);
        std::array<T, 6> tmp;

        tmp[0] = std::accumulate(rand_uints.begin(), rand_uints.end(), T{0});
        fmt::print("pre-test accumulation: {:0{}x}\n", tmp[0], 2 * sizeof(T));

        // Dummy run to warm up the memory locations
        run_test.template operator()<Mode::builtin>(PRECONTROL, times[0][time_idx], rand_uints);
        tmp[0] = std::accumulate(rand_uints.begin(), rand_uints.end(), T{0});
        run_test.template operator()<Mode::builtin>(CONTROL, times[1][time_idx], rand_uints);
        tmp[1] = std::accumulate(rand_uints.begin(), rand_uints.end(), T{0});
        run_test.template operator()<Mode::ranges>(RANGES, times[2][time_idx], rand_uints);
        tmp[2] = std::accumulate(rand_uints.begin(), rand_uints.end(), T{0});
        run_test.template operator()<Mode::fallback1>(FALLBACK1, times[3][time_idx], rand_uints);
        tmp[3] = std::accumulate(rand_uints.begin(), rand_uints.end(), T{0});
        run_test.template operator()<Mode::fallback2>(FALLBACK2, times[4][time_idx], rand_uints);
        tmp[4] = std::accumulate(rand_uints.begin(), rand_uints.end(), T{0});
#ifdef OXENC_HAS_BYTESWAP_SYSTEM
        run_test.template operator()<Mode::system>(SYSTEMBSWAP, times[5][time_idx], rand_uints);
        tmp[5] = std::accumulate(rand_uints.begin(), rand_uints.end(), T{0});
#endif

        fmt::print("Done; (accumulated values: {:0{}x})\n", fmt::join(tmp, ","), 2 * sizeof(T));
    };

    run_tests(rand_uint16s);
    run_tests(rand_uint32s);
    run_tests(rand_uint64s);

    auto print_result = [&times](int i, int j, auto& name) {
        auto& control = times[1][i];
        auto control_total = control[1] - control[0];
        auto& t = times[j][i];
        auto total = t[1].time_since_epoch() - t[0].time_since_epoch();
        auto diff = total - control_total;
        auto delta = double(diff.count()) / double(control_total.count()) * 100.0;
        fmt::print(
                "{:25s} [uint{}_t]: {:12.9f}s elapsed, {:6.2f}ns/swap, {:10.03f}% vs control\n",
                name,
                16 << i,
                total.count() / 1e9,
                total.count() / (double)(N * ROUNDS),
                delta);
    };

    for (int i : {0, 1, 2}) {
        std::cout << "\n";
        print_result(i, 0, PRECONTROL);
        print_result(i, 1, CONTROL);
        print_result(i, 2, RANGES);
        print_result(i, 3, FALLBACK1);
        print_result(i, 4, FALLBACK2);
#ifdef __linux__
        print_result(i, 5, SYSTEMBSWAP);
#endif
    }
}
