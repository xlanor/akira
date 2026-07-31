#include "test_util.hpp"

#include "util/sha256.hpp"

#include <string>

static std::string sha256Hex(const std::string& in)
{
    akira::sha256::Ctx ctx;
    akira::sha256::init(ctx);
    akira::sha256::update(ctx, reinterpret_cast<const uint8_t*>(in.data()), in.size());
    uint8_t out[32];
    akira::sha256::final(ctx, out);
    return akira::sha256::toHex(out);
}

TEST(sha256_known_vectors)
{
    CHECK_EQ(sha256Hex(""),
             std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(sha256Hex("abc"),
             std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

TEST(sha256_streaming_matches_single_shot)
{
    std::string big(200000, 'x');
    akira::sha256::Ctx a;
    akira::sha256::init(a);
    for (size_t i = 0; i < big.size(); i += 7)
        akira::sha256::update(a, reinterpret_cast<const uint8_t*>(big.data() + i),
                              std::min<size_t>(7, big.size() - i));
    uint8_t oa[32];
    akira::sha256::final(a, oa);

    CHECK_EQ(akira::sha256::toHex(oa), sha256Hex(big));
}
