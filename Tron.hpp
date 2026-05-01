#ifndef HPP_TRON
#define HPP_TRON

#include <string>
#include <vector>
#include <stdint.h>
#include <algorithm>

class Tron {
public:
    class SHA256 {
    public:
        static void hash(const uint8_t* data, size_t len, uint8_t* out) {
            uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
            std::vector<uint8_t> p(data, data + len);
            p.push_back(0x80);
            while ((p.size() + 8) % 64 != 0) p.push_back(0x00);
            uint64_t bitlen = (uint64_t)len * 8;
            for (int i = 7; i >= 0; --i) p.push_back((bitlen >> (i * 8)) & 0xff);

            for (size_t i = 0; i < p.size(); i += 64) {
                uint32_t w[64];
                for (int j = 0; j < 16; ++j) w[j] = (p[i + j * 4] << 24) | (p[i + j * 4 + 1] << 16) | (p[i + j * 4 + 2] << 8) | p[i + j * 4 + 3];
                for (int j = 16; j < 64; ++j) {
                    uint32_t s0 = rotate_right(w[j - 15], 7) ^ rotate_right(w[j - 15], 18) ^ (w[j - 15] >> 3);
                    uint32_t s1 = rotate_right(w[j - 2], 17) ^ rotate_right(w[j - 2], 19) ^ (w[j - 2] >> 10);
                    w[j] = w[j - 16] + s0 + w[j - 7] + s1;
                }
                uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], i2 = h[7];
                static const uint32_t k[] = {
                    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
                    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
                    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
                    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
                    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
                    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
                    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
                    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
                };
                for (int j = 0; j < 64; ++j) {
                    uint32_t S1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
                    uint32_t ch = (e & f) ^ ((~e) & g);
                    uint32_t temp1 = i2 + S1 + ch + k[j] + w[j];
                    uint32_t S0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
                    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                    uint32_t temp2 = S0 + maj;
                    i2 = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
                }
                h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += i2;
            }
            for (int i = 0; i < 8; ++i) {
                out[i * 4] = (h[i] >> 24) & 0xff;
                out[i * 4 + 1] = (h[i] >> 16) & 0xff;
                out[i * 4 + 2] = (h[i] >> 8) & 0xff;
                out[i * 4 + 3] = h[i] & 0xff;
            }
        }
    private:
        static uint32_t rotate_right(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
    };

    static std::vector<uint8_t> decodeBase58(const std::string& input) {
        const char* ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
        std::vector<uint8_t> res;
        for (char c : input) {
            const char* p = std::find(ALPHABET, ALPHABET + 58, c);
            if (p == ALPHABET + 58) return {};
            int carry = p - ALPHABET;
            for (size_t i = 0; i < res.size(); ++i) {
                carry += (int)res[i] * 58;
                res[i] = carry % 256;
                carry /= 256;
            }
            while (carry) {
                res.push_back(carry % 256);
                carry /= 256;
            }
        }
        for (char c : input) {
            if (c == ALPHABET[0]) res.push_back(0);
            else break;
        }
        std::reverse(res.begin(), res.end());
        return res;
    }

    static std::string hexToAddress(const uint8_t* hash20) {
        std::vector<uint8_t> data;
        data.push_back(0x41);
        for (int i = 0; i < 20; ++i) data.push_back(hash20[i]);

        uint8_t hash1[32];
        uint8_t hash2[32];
        SHA256::hash(data.data(), data.size(), hash1);
        SHA256::hash(hash1, 32, hash2);

        for (int i = 0; i < 4; ++i) data.push_back(hash2[i]);
        return encodeBase58(data);
    }

private:
    static std::string encodeBase58(const std::vector<uint8_t>& input) {
        const char* ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
        std::vector<uint8_t> digits(input.size() * 138 / 100 + 1, 0);
        size_t digits_len = 1;
        for (uint8_t byte : input) {
            uint32_t carry = byte;
            for (size_t i = 0; i < digits_len; ++i) {
                carry += (uint32_t)digits[i] << 8;
                digits[i] = carry % 58;
                carry /= 58;
            }
            while (carry) {
                digits[digits_len++] = carry % 58;
                carry /= 58;
            }
        }
        std::string res = "";
        for (uint8_t byte : input) {
            if (byte == 0) res += ALPHABET[0];
            else break;
        }
        for (size_t i = 0; i < digits_len; ++i) res += ALPHABET[digits[digits_len - 1 - i]];
        return res;
    }
};

#endif
