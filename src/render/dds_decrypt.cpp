#include "render/dds_decrypt.h"

#include <array>
#include <cstring>

namespace dds {
namespace {

constexpr uint32_t kMagic = 0x06091812;
constexpr uint8_t kBaseKey[16] = {0xC9, 0x59, 0x46, 0xCA, 0xD9, 0xF0, 0x4F, 0x0A,
                                  0xA1, 0x00, 0xAA, 0xB8, 0xCB, 0xE8, 0xDB, 0x6B};

constexpr int kSeedBitOrder[32] = {12, 23, 10, 25, 8,  27, 6,  29, 4,  30, 1,  22, 9,  13, 21, 0,
                                   17, 26, 5,  15, 18, 28, 11, 2,  14, 3,  24, 7,  19, 16, 20, 31};

uint32_t ReadU32(const uint8_t* data)
{
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

uint32_t Crc32(const uint8_t* data, size_t length)
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> result = {};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int bit = 0; bit < 8; ++bit) {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            result[i] = c;
        }
        return result;
    }();

    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

class ParkMiller {
public:
    explicit ParkMiller(uint32_t seed)
    {
        uint32_t s = seed & 0x7FFFFFFFu;
        int bits = 0;
        for (uint32_t v = s; v != 0; v >>= 1) {
            bits += static_cast<int>(v & 1u);
        }
        if (bits < 8) {
            for (int i = 0; i < 8 - bits; ++i) {
                s |= 1u << kSeedBitOrder[i];
            }
        }
        if (bits > 24) {
            for (int i = 0; i < 32 - bits; ++i) {
                s &= ~(1u << kSeedBitOrder[i]);
            }
        }
        s &= 0x7FFFFFFFu;
        state_ = s != 0 ? s : 1;
    }

    uint32_t Next()
    {
        const uint32_t hi = (state_ >> 16) * 16807u;
        uint32_t n = (state_ & 0xFFFFu) * 16807u + ((hi & 0x7FFFu) << 16);
        if (n > 0x7FFFFFFFu) {
            n = (n & 0x7FFFFFFFu) + 1u;
        }
        n += hi >> 15;
        if (n > 0x7FFFFFFFu) {
            n = (n & 0x7FFFFFFFu) + 1u;
        }
        state_ = n;
        return n;
    }

private:
    uint32_t state_ = 1;
};

std::string KeyName(std::string name)
{
    const size_t slash = name.find_last_of("\\/");
    if (slash != std::string::npos) {
        name.erase(0, slash + 1);
    }
    for (char& c : name) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return name;
}

void DeriveKey(const std::string& file_name, uint8_t key[16])
{
    memcpy(key, kBaseKey, 16);
    const std::string name = KeyName(file_name);
    if (name.size() >= 4) {
        const std::string ext = name.substr(name.size() - 4);
        if (ext == ".s2m" || ext == ".sav") {
            return;
        }
    }

    ParkMiller rng(Crc32(reinterpret_cast<const uint8_t*>(name.data()), name.size()));
    for (int i = 0; i < 16; ++i) {
        key[i] ^= static_cast<uint8_t>(rng.Next() & 0xFFu);
    }
}

void DecryptScramble(std::vector<uint8_t>& data, const uint8_t key[16])
{
    if (data.empty()) {
        return;
    }

    ParkMiller rng(Crc32(key, 16));
    const size_t flavor1_size = (rng.Next() & 0x7Fu) + 0x80u;
    std::vector<uint8_t> flavor1(flavor1_size);
    for (size_t i = 0; i < flavor1_size; ++i) {
        flavor1[i] = static_cast<uint8_t>(rng.Next() & 0xFFu);
    }

    const size_t flavor2_size = (rng.Next() & 0xFu) + 0x11u;
    std::vector<uint8_t> flavor2(flavor2_size);
    for (size_t i = 0; i < flavor2_size; ++i) {
        flavor2[i] = static_cast<uint8_t>(rng.Next() & 0xFFu);
    }

    const size_t start = rng.Next() % data.size();
    const size_t step = (rng.Next() & 0x1FFFu) + 0x2000u;

    for (size_t i = 0; i < data.size(); ++i) {
        data[i] ^= flavor1[i % flavor1_size];
        if (i >= start && ((i - start) % step) == 0) {
            data[i] ^= flavor2[(key[i % 16] ^ i) % flavor2_size];
        }
    }
}

bool LzssDecode(const std::vector<uint8_t>& input, size_t expected_size, std::vector<uint8_t>& output)
{
    output.clear();
    output.reserve(expected_size);

    uint8_t ring[1024];
    memset(ring, ' ', sizeof(ring));
    size_t ring_pos = 0x3F0;
    size_t input_pos = 0;
    uint32_t flags = 0;

    for (;;) {
        flags >>= 1;
        if ((flags & 0x100u) == 0) {
            if (input_pos >= input.size()) {
                break;
            }
            flags = static_cast<uint32_t>(input[input_pos++]) | 0xFF00u;
        }

        if ((flags & 1u) != 0) {
            if (input_pos >= input.size()) {
                break;
            }
            const uint8_t c = input[input_pos++];
            output.push_back(c);
            ring[ring_pos] = c;
            ring_pos = (ring_pos + 1) & 0x3FFu;
        } else {
            if (input_pos + 1 >= input.size()) {
                break;
            }
            const size_t position = static_cast<size_t>(input[input_pos]) |
                                    (static_cast<size_t>(input[input_pos + 1] & 0xF0u) << 4);
            const size_t length = static_cast<size_t>(input[input_pos + 1] & 0x0Fu) + 3;
            input_pos += 2;
            for (size_t i = 0; i < length; ++i) {
                const uint8_t c = ring[(position + i) & 0x3FFu];
                output.push_back(c);
                ring[ring_pos] = c;
                ring_pos = (ring_pos + 1) & 0x3FFu;
            }
        }

        if (output.size() > expected_size + 4096) {
            return false;
        }
    }

    return output.size() == expected_size;
}

} // namespace

bool DecryptDng(const std::vector<uint8_t>& input, const std::string& key_name, std::vector<uint8_t>& output)
{
    output.clear();
    if (input.size() >= 4 && memcmp(input.data(), "DDS ", 4) == 0) {
        output = input;
        return true;
    }
    if (input.size() < 20) {
        return false;
    }

    const uint32_t magic = ReadU32(input.data());
    const bool is_rc00 = input[4] == 'r' && input[5] == 'c' && input[6] == '0' && input[7] == '0';
    if (magic != kMagic || !is_rc00) {
        return false;
    }

    const uint32_t file_crc = ReadU32(input.data() + 8);
    const uint32_t name_crc = ReadU32(input.data() + 12);
    const uint32_t plain_size = ReadU32(input.data() + 16);
    if (plain_size > 64u * 1024u * 1024u) {
        return false;
    }

    uint8_t key[16] = {};
    DeriveKey(key_name, key);
    if (Crc32(key, 16) != name_crc) {
        return false;
    }

    std::vector<uint8_t> payload(input.begin() + 20, input.end());
    DecryptScramble(payload, key);

    std::vector<uint8_t> plain;
    if (!LzssDecode(payload, plain_size, plain)) {
        return false;
    }
    if (Crc32(plain.data(), plain.size()) != file_crc) {
        return false;
    }

    output = std::move(plain);
    return true;
}

} // namespace dds
