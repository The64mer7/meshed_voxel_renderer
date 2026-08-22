#pragma once
#include <string>
#include <iostream>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#define BITMASK(n) ((1ull << (n)) - 1ull)
#define MAKE_COLOR(r,g,b,a) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))
#define MAKE_COLOR_OFFSET(r, g, b, a, offset) ( \
    ((uint32_t)((r) + (offset)) & 0xFFu)        | \
    (((uint32_t)((g) + (offset)) & 0xFFu) << 8)  | \
    (((uint32_t)((b) + (offset)) & 0xFFu) << 16) | \
    (((uint32_t)(a) & 0xFFu) << 24)                \
)

template <glm::length_t L, typename T, glm::qualifier Q>
void print_vec(const std::string& label, const glm::vec<L, T, Q>& v) {
    std::cout << label << ": [ ";

    const T* data = glm::value_ptr(v);

    for (glm::length_t i = 0; i < L; ++i) {
        std::cout << data[i] << (i < L - 1 ? ", " : "");
    }

    std::cout << " ]" << std::endl;
}

template<typename... Args>
void LOG(std::format_string<Args...> fmt, Args&&... args)
{
    std::cout << std::format(fmt, std::forward<Args>(args)...) << '\n';
}

inline float hash(float n)
{
    return glm::fract(191122.518925 + glm::sin(n) * 43758.5453123);
}

static void transpose_matrix(uint64_t matrix[64])
{
    uint64_t mask32 = 0x00000000FFFFFFFFULL;
    for (int i = 0; i < 32; i++) {
        uint64_t t = ((matrix[i] >> 32) ^ matrix[i + 32]) & mask32;
        matrix[i] ^= (t << 32);
        matrix[i + 32] ^= t;
    }

    uint64_t mask16 = 0x0000FFFF0000FFFFULL;
    for (int i = 0; i < 64; i += 32) {
        for (int j = 0; j < 16; j++) {
            int idx = i + j;
            uint64_t t = ((matrix[idx] >> 16) ^ matrix[idx + 16]) & mask16;
            matrix[idx] ^= (t << 16);
            matrix[idx + 16] ^= t;
        }
    }

    uint64_t mask8 = 0x00FF00FF00FF00FFULL;
    for (int i = 0; i < 64; i += 16) {
        for (int j = 0; j < 8; j++) {
            int idx = i + j;
            uint64_t t = ((matrix[idx] >> 8) ^ matrix[idx + 8]) & mask8;
            matrix[idx] ^= (t << 8);
            matrix[idx + 8] ^= t;
        }
    }

    uint64_t mask4 = 0x0F0F0F0F0F0F0F0FULL;
    for (int i = 0; i < 64; i += 8) {
        for (int j = 0; j < 4; j++) {
            int idx = i + j;
            uint64_t t = ((matrix[idx] >> 4) ^ matrix[idx + 4]) & mask4;
            matrix[idx] ^= (t << 4);
            matrix[idx + 4] ^= t;
        }
    }

    uint64_t mask2 = 0x3333333333333333ULL;
    for (int i = 0; i < 64; i += 4) {
        for (int j = 0; j < 2; j++) {
            int idx = i + j;
            uint64_t t = ((matrix[idx] >> 2) ^ matrix[idx + 2]) & mask2;
            matrix[idx] ^= (t << 2);
            matrix[idx + 2] ^= t;
        }
    }

    uint64_t mask1 = 0x5555555555555555ULL;
    for (int i = 0; i < 64; i += 2) {
        uint64_t t = ((matrix[i] >> 1) ^ matrix[i + 1]) & mask1;
        matrix[i] ^= (t << 1);
        matrix[i + 1] ^= t;
    }
}