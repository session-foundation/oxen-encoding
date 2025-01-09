/**
    Oxen-encoding endian method testing binary
*/

// NOTE: purposely NOT including oxenc headers

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>

extern "C" {
#include <byteswap.h>
}

using namespace std::literals;
using time_point = std::chrono::steady_clock::time_point;

#define test_bswap_16(x) __builtin_bswap16(x)
#define test_bswap_32(x) __builtin_bswap32(x)
#define test_bswap_64(x) __builtin_bswap64(x)

namespace test {
constexpr bool little_endian = std::endian::native == std::endian::little;
constexpr bool big_endian = !little_endian;

template <typename T>
concept endian_swappable_int =
        std::integral<T> && (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);

template <std::integral T>
    requires std::has_unique_object_representations_v<T> && std::is_unsigned_v<T>
[[nodiscard]] inline constexpr T byteswap_ranges(T val) noexcept {
    auto value = std::bit_cast<std::array<std::byte, sizeof(T)>>(val);
    std::ranges::reverse(value);
    return std::bit_cast<T>(value);
}

template <std::integral T>
    requires std::has_unique_object_representations_v<T> && std::is_unsigned_v<T>
[[nodiscard]] inline constexpr T byteswap_fallback(T val) noexcept {
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

template <endian_swappable_int T>
constexpr void byteswap_(T& val) {
    if constexpr (sizeof(T) == 2)
        val = std::bit_cast<T>(test_bswap_16(std::bit_cast<uint16_t>(val)));
    else if constexpr (sizeof(T) == 4)
        val = std::bit_cast<T>(test_bswap_32(std::bit_cast<uint32_t>(val)));
    else if constexpr (sizeof(T) == 8)
        val = std::bit_cast<T>(test_bswap_64(std::bit_cast<uint64_t>(val)));
}

template <endian_swappable_int T>
constexpr void host_to_little_inplace(T& val) {
    if constexpr (!little_endian)
        byteswap_(val);
}

template <endian_swappable_int T>
constexpr void little_to_host_inplace(T& val) {
    if constexpr (!little_endian)
        byteswap_(val);
}

template <endian_swappable_int T>
constexpr void host_to_big_inplace(T& val) {
    if constexpr (!big_endian)
        byteswap_(val);
}

template <endian_swappable_int T>
constexpr void big_to_host_inplace(T& val) {
    if constexpr (!big_endian)
        byteswap_(val);
}
}  // namespace test

static constexpr std::size_t N{100000};

std::array<uint16_t, N> rand_uint16s{};
std::array<uint32_t, N> rand_uint32s{};
std::array<uint64_t, N> rand_uint64s{};

std::array<std::array<time_point, 3>, 4> times{};

static constexpr auto CONTROL = "Control Group"sv;
static constexpr auto RANGES = "std::ranges Group"sv;
static constexpr auto FALLBACK = "Fallback Group"sv;
static constexpr auto SYSTEMBSWAP = "byteswap.h Group"sv;

int main(int /* argc */, char** /* argv */) {
    std::random_device rd;
    std::mt19937 gen{rd()};

    std::uniform_int_distribution<uint16_t> u16generator{};
    std::uniform_int_distribution<uint32_t> u32generator{};
    std::uniform_int_distribution<uint64_t> u64generator{};

    auto run_test = [&](std::string_view trial, std::array<time_point, 3>& time_log) {
        std::cout << "Beginning byteswap test group: " << trial << std::endl;

        std::cout << "Randomly generating uint{16,32,64} arrays..." << std::endl;

        std::generate(
                rand_uint16s.begin(), rand_uint16s.end(), [&]() { return u16generator(gen); });
        std::generate(
                rand_uint32s.begin(), rand_uint32s.end(), [&]() { return u32generator(gen); });
        std::generate(
                rand_uint64s.begin(), rand_uint64s.end(), [&]() { return u64generator(gen); });

        time_log[0] = std::chrono::steady_clock::now();

        for (auto& u16 : rand_uint16s)
            test::host_to_little_inplace(u16);

        for (auto& u32 : rand_uint32s)
            test::host_to_little_inplace(u32);

        for (auto& u64 : rand_uint64s)
            test::host_to_little_inplace(u64);

        for (auto& u16 : rand_uint16s)
            test::little_to_host_inplace(u16);

        for (auto& u32 : rand_uint32s)
            test::little_to_host_inplace(u32);

        for (auto& u64 : rand_uint64s)
            test::little_to_host_inplace(u64);

        time_log[1] = std::chrono::steady_clock::now();

        for (auto& u16 : rand_uint16s)
            test::host_to_big_inplace(u16);

        for (auto& u32 : rand_uint32s)
            test::host_to_big_inplace(u32);

        for (auto& u64 : rand_uint64s)
            test::host_to_big_inplace(u64);

        for (auto& u16 : rand_uint16s)
            test::big_to_host_inplace(u16);

        for (auto& u32 : rand_uint32s)
            test::big_to_host_inplace(u32);

        for (auto& u64 : rand_uint64s)
            test::big_to_host_inplace(u64);

        time_log[2] = std::chrono::steady_clock::now();

        std::cout << "Finished test group: " << trial << "\n" << std::endl;
    };

#define test_bswap_16(x) __builtin_bswap16(x)
#define test_bswap_32(x) __builtin_bswap32(x)
#define test_bswap_64(x) __builtin_bswap64(x)

    run_test(CONTROL, times[0]);

#undef test_bswap_16
#undef test_bswap_32
#undef test_bswap_64

#define test_bswap_16(x) byteswap_ranges<uint16_t>(x)
#define test_bswap_32(x) byteswap_ranges<uint32_t>(x)
#define test_bswap_64(x) byteswap_ranges<uint64_t>(x)

    run_test(RANGES, times[1]);

#undef test_bswap_16
#undef test_bswap_32
#undef test_bswap_64

#define test_bswap_16(x) byteswap_fallback<uint16_t>(x)
#define test_bswap_32(x) byteswap_fallback<uint32_t>(x)
#define test_bswap_64(x) byteswap_fallback<uint64_t>(x)

    run_test(FALLBACK, times[2]);

#undef test_bswap_16
#undef test_bswap_32
#undef test_bswap_64

#define test_bswap_16(x) bswap_16(x)
#define test_bswap_32(x) bswap_32(x)
#define test_bswap_64(x) bswap_64(x)

    run_test(SYSTEMBSWAP, times[3]);

    auto& control_times = times[0];

    std::cout << "TEST GROUP: " << CONTROL << std::endl;
    std::cout << "\tStart: " << control_times[0].time_since_epoch() << std::endl;
    std::cout << "\tMidpoint: " << control_times[1].time_since_epoch() << std::endl;
    std::cout << "\tFinish: " << control_times[2].time_since_epoch() << std::endl;
    auto control_firsthalf =
            control_times[1].time_since_epoch() - control_times[0].time_since_epoch();
    auto control_secondhalf =
            control_times[2].time_since_epoch() - control_times[1].time_since_epoch();
    auto control_total = control_firsthalf + control_secondhalf;
    auto control_avghalf = control_total / 2;
    std::cout << "\tFirst Half: " << control_firsthalf << ", Second Half: " << control_secondhalf
              << ", Avg: " << control_avghalf << std::endl;
    std::cout << "\tTotal: " << control_total << "\n" << std::endl;

    auto& range_times = times[1];

    std::cout << "TEST GROUP: " << RANGES << std::endl;
    std::cout << "\tStart: " << range_times[0].time_since_epoch() << std::endl;
    std::cout << "\tMidpoint: " << range_times[1].time_since_epoch() << std::endl;
    std::cout << "\tFinish: " << range_times[2].time_since_epoch() << std::endl;
    auto range_firsthalf = range_times[1].time_since_epoch() - range_times[0].time_since_epoch();
    auto range_secondhalf = range_times[2].time_since_epoch() - range_times[1].time_since_epoch();
    auto range_total = range_firsthalf + range_secondhalf;
    auto range_avghalf = range_total / 2;
    auto range_diff =
            range_total > control_total ? range_total - control_total : control_total - range_total;
    auto range_delta = double(range_diff.count()) / double(control_total.count()) * 100.0;
    std::cout << "\tFirst Half: " << range_firsthalf << ", Second Half: " << range_secondhalf
              << ", Avg: " << range_avghalf << std::endl;
    std::cout << "\tTotal: " << range_total << std::endl;
    std::cout << "\tPercent Diff: " << range_delta << "\%\n" << std::endl;

    auto& fallback_times = times[2];

    std::cout << "TEST GROUP: " << FALLBACK << std::endl;
    std::cout << "\tStart: " << fallback_times[0].time_since_epoch() << std::endl;
    std::cout << "\tMidpoint: " << fallback_times[1].time_since_epoch() << std::endl;
    std::cout << "\tFinish: " << fallback_times[2].time_since_epoch() << std::endl;
    auto fallback_firsthalf =
            fallback_times[1].time_since_epoch() - fallback_times[0].time_since_epoch();
    auto fallback_secondhalf =
            fallback_times[2].time_since_epoch() - fallback_times[1].time_since_epoch();
    auto fallback_total = fallback_firsthalf + fallback_secondhalf;
    auto fallback_avghalf = fallback_total / 2;
    auto fallback_diff = fallback_total > control_total ? fallback_total - control_total
                                                        : control_total - fallback_total;
    auto fallback_delta = double(fallback_diff.count()) / double(control_total.count()) * 100.0;
    std::cout << "\tFirst Half: " << fallback_firsthalf << ", Second Half: " << fallback_secondhalf
              << ", Avg: " << fallback_avghalf << std::endl;
    std::cout << "\tTotal: " << fallback_total << std::endl;
    std::cout << "\tPercent Diff: " << fallback_delta << "\%\n" << std::endl;

    auto& sys_times = times[3];

    std::cout << "TEST GROUP: " << SYSTEMBSWAP << std::endl;
    std::cout << "\tStart: " << sys_times[0].time_since_epoch() << std::endl;
    std::cout << "\tMidpoint: " << sys_times[1].time_since_epoch() << std::endl;
    std::cout << "\tFinish: " << sys_times[2].time_since_epoch() << std::endl;
    auto sys_firsthalf = sys_times[1].time_since_epoch() - sys_times[0].time_since_epoch();
    auto sys_secondhalf = sys_times[2].time_since_epoch() - sys_times[1].time_since_epoch();
    auto sys_total = sys_firsthalf + sys_secondhalf;
    auto sys_avghalf = sys_total / 2;
    auto sys_diff =
            sys_total > control_total ? sys_total - control_total : control_total - sys_total;
    auto sys_delta = double(sys_diff.count()) / double(control_total.count()) * 100.0;
    std::cout << "\tFirst Half: " << sys_firsthalf << ", Second Half: " << sys_secondhalf
              << ", Avg: " << sys_avghalf << std::endl;
    std::cout << "\tTotal: " << sys_total << std::endl;
    std::cout << "\tPercent Diff: " << sys_delta << "\%\n" << std::endl;

    return 0;
}
