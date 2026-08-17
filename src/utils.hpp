#pragma once
#include <string>
#include <iostream>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#define BITMASK(n) ((1 << (n)) - 1)
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
