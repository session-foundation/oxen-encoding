
#include <limits>
#include <map>
#include <set>

#include "common.h"
#include "oxenc/rlp_serialize.h"

TEST_CASE("RLP serialization", "[rlp][serialization]") {
    CHECK(oxenc::to_hex(rlp_serialize("dog")) == "83646f67");

    std::vector<std::string> animals = {"cat", "dog"};
    CHECK(oxenc::to_hex(rlp_serialize(animals)) == "c88363617483646f67");

    CHECK(oxenc::to_hex(rlp_serialize("")) == "80");

    CHECK(oxenc::to_hex(rlp_serialize(uint32_t{0})) == "80");
    CHECK(oxenc::to_hex(rlp_serialize("\x0f"s)) == "0f");
    CHECK(oxenc::to_hex(rlp_serialize("\x7f"s)) == "7f");
    CHECK(oxenc::to_hex(rlp_serialize("\x04\x00"s)) == "820400");

    using V = std::variant<unsigned, std::string, std::array<unsigned, 2>, std::vector<unsigned>>;
    V v1 = unsigned{123};
    V v2 = "hello"s;
    V v3 = std::array{10u, 1000u};
    CHECK(oxenc::to_hex(rlp_serialize(v1)) == "7b");
    CHECK(oxenc::to_hex(rlp_serialize(v2)) == "8568656c6c6f");
    CHECK(oxenc::to_hex(rlp_serialize(v3)) == "c40a8203e8");

    auto big_int1 = "0000000000123456"_hex;
    auto big_int2 = "0100000000123456"_hex;
    auto big_int3 = "00000000001234560000000000000000"_hex;
    auto big_int4 = "0000000000000000000000000000000000000000000000000000000000abcdef"_hex;
    auto big_int5 = "000000000000000000000000000000000000000000000000000000000abcdef9"_hex;
    auto big_int6 =
            "800000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000abcdef9"_hex;
    auto big_int7 = "000000000000000000000000000000"_hex;

    CHECK(oxenc::to_hex(rlp_serialize(rlp_big_integer(big_int1))) == "83123456");
    CHECK(oxenc::to_hex(rlp_serialize(rlp_big_integer(big_int2))) == "880100000000123456");
    CHECK(oxenc::to_hex(rlp_serialize(rlp_big_integer(big_int3))) == "8b1234560000000000000000");
    CHECK(oxenc::to_hex(rlp_serialize(rlp_big_integer(big_int4))) == "83abcdef");
    CHECK(oxenc::to_hex(rlp_serialize(rlp_big_integer(big_int5))) == "840abcdef9");
    CHECK(oxenc::to_hex(rlp_serialize(rlp_big_integer(big_int6))) ==
          "b838800000000000000000000000000000000000000000000000000000000000000000000000000000000000"
          "000000000000000000000abcdef9");
    CHECK(oxenc::to_hex(rlp_serialize(rlp_big_integer(big_int7))) == "80");

    std::vector<V> big_v;
    CHECK(oxenc::to_hex(rlp_serialize(big_v)) == "c0");
    big_v.emplace_back(unsigned{1234});
    big_v.emplace_back(std::vector<unsigned>{1u, 2u, 3u});
    big_v.emplace_back(std::vector<unsigned>{});
    CHECK(oxenc::to_hex(rlp_serialize(big_v)) ==
          "c8"
          "8204d2"
          "c3010203"
          "c0");

    big_v.emplace_back("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"s);
    CHECK(oxenc::to_hex(rlp_serialize(big_v)) ==
          "f7"
          "8204d2"
          "c3010203"
          "c0"
          "ae"
          "4242424242424242424242424242424242424242424242424242424242424242424242424242424242424242"
          "4242");

    big_v.pop_back();
    big_v.emplace_back("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"s);
    CHECK(oxenc::to_hex(rlp_serialize(big_v)) ==
          "f8"
          "38"
          "8204d2"
          "c3010203"
          "c0"
          "af"
          "4242424242424242424242424242424242424242424242424242424242424242424242424242424242424242"
          "424242");

    CHECK(oxenc::to_hex(rlp_serialize("Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
                                      "Curabitur mauris magna, suscipit sed vehicula non, iaculis "
                                      "faucibus tortor. Proin suscipit ultricies malesuada. Duis "
                                      "tortor elit, dictum quis tristique eu, ultrices at risus. "
                                      "Morbi a est imperdiet mi ullamcorper aliquet suscipit nec "
                                      "lorem. Aenean quis leo mollis, vulputate elit varius, "
                                      "consequat enim. Nulla ultrices turpis justo, et posuere "
                                      "urna consectetur nec. Proin non convallis metus. Donec "
                                      "tempor ipsum in mauris congue sollicitudin. Vestibulum ante "
                                      "ipsum primis in faucibus orci luctus et ultrices posuere "
                                      "cubilia Curae; Suspendisse convallis sem vel massa "
                                      "faucibus, eget lacinia lacus tempor. Nulla quis ultricies "
                                      "purus. Proin auctor rhoncus nibh condimentum mollis. "
                                      "Aliquam consequat enim at metus luctus, a eleifend purus "
                                      "egestas. Curabitur at nibh metus. Nam bibendum, neque at "
                                      "auctor tristique, lorem libero aliquet arcu, non interdum "
                                      "tellus lectus sit amet eros. Cras rhoncus, metus ac ornare "
                                      "cursus, dolor justo ultrices metus, at ullamcorper "
                                      "volutpat")) ==
          "b904004c6f72656d20697073756d20646f6c6f722073697420616d65742c20636f6e73656374657475722061"
          "646970697363696e6720656c69742e20437572616269747572206d6175726973206d61676e612c2073757363"
          "6970697420736564207665686963756c61206e6f6e2c20696163756c697320666175636962757320746f7274"
          "6f722e2050726f696e20737573636970697420756c74726963696573206d616c6573756164612e2044756973"
          "20746f72746f7220656c69742c2064696374756d2071756973207472697374697175652065752c20756c7472"
          "696365732061742072697375732e204d6f72626920612065737420696d70657264696574206d6920756c6c61"
          "6d636f7270657220616c6971756574207375736369706974206e6563206c6f72656d2e2041656e65616e2071"
          "756973206c656f206d6f6c6c69732c2076756c70757461746520656c6974207661726975732c20636f6e7365"
          "7175617420656e696d2e204e756c6c6120756c74726963657320747572706973206a7573746f2c2065742070"
          "6f73756572652075726e6120636f6e7365637465747572206e65632e2050726f696e206e6f6e20636f6e7661"
          "6c6c6973206d657475732e20446f6e65632074656d706f7220697073756d20696e206d617572697320636f6e"
          "67756520736f6c6c696369747564696e2e20566573746962756c756d20616e746520697073756d207072696d"
          "697320696e206661756369627573206f726369206c756374757320657420756c74726963657320706f737565"
          "726520637562696c69612043757261653b2053757370656e646973736520636f6e76616c6c69732073656d20"
          "76656c206d617373612066617563696275732c2065676574206c6163696e6961206c616375732074656d706f"
          "722e204e756c6c61207175697320756c747269636965732070757275732e2050726f696e20617563746f7220"
          "72686f6e637573206e69626820636f6e64696d656e74756d206d6f6c6c69732e20416c697175616d20636f6e"
          "73657175617420656e696d206174206d65747573206c75637475732c206120656c656966656e642070757275"
          "7320656765737461732e20437572616269747572206174206e696268206d657475732e204e616d2062696265"
          "6e64756d2c206e6571756520617420617563746f72207472697374697175652c206c6f72656d206c69626572"
          "6f20616c697175657420617263752c206e6f6e20696e74657264756d2074656c6c7573206c65637475732073"
          "697420616d65742065726f732e20437261732072686f6e6375732c206d65747573206163206f726e61726520"
          "6375727375732c20646f6c6f72206a7573746f20756c747269636573206d657475732c20617420756c6c616d"
          "636f7270657220766f6c7574706174");

    CHECK(oxenc::to_hex(rlp_serialize(0u)) == "80");
    CHECK(oxenc::to_hex(rlp_serialize(1u)) == "01");
    CHECK(oxenc::to_hex(rlp_serialize(16u)) == "10");
    CHECK(oxenc::to_hex(rlp_serialize(79u)) == "4f");
    CHECK(oxenc::to_hex(rlp_serialize(127u)) == "7f");
    CHECK(oxenc::to_hex(rlp_serialize(128u)) == "8180");
    CHECK(oxenc::to_hex(rlp_serialize(1000u)) == "8203e8");
    CHECK(oxenc::to_hex(rlp_serialize(100000u)) == "830186a0");

    rlp_value twenty_three{23u};
    CHECK(oxenc::to_hex(rlp_serialize(twenty_three)) == "17");

    // [ [], [[]], [ [], [[]] ] ]
    rlp_list x;
    x.push_back(rlp_list{});
    rlp_list y;
    y.push_back(rlp_list{});
    x.push_back(std::move(y));
    y.clear();
    y.push_back(rlp_list{});
    rlp_list z;
    z.push_back(rlp_list{});
    y.push_back(std::move(z));
    x.push_back(std::move(y));

    CHECK(oxenc::to_hex(rlp_serialize(x)) == "c7c0c1c0c3c0c1c0");
}
