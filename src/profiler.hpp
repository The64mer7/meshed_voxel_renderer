#pragma once

#include <chrono>

#define PROFILER_DECLARE(name)                                                                     \
    double _profiler_##name##_count = 0.0;                                                         \
    double _profiler_##name##_sum = 0.0;

#define PROFILER_BEGIN(name) auto _profiler_##name##_t0 = std::chrono::high_resolution_clock::now();

#define PROFILER_END(name)                                                                         \
    auto _profiler_##name##_t1 = std::chrono::high_resolution_clock::now();                        \
    _profiler_##name##_count += 1.0;                                                               \
    _profiler_##name##_sum +=                                                                      \
        std::chrono::duration<double>(_profiler_##name##_t1 - _profiler_##name##_t0).count();

#define PROFILER_GET_AVG_NESTED(name, parent)                                                      \
    (parent._profiler_##name##_sum / parent._profiler_##name##_count)
#define PROFILER_GET(name)                                                                         \
    std::chrono::duration<double>(_profiler_##name##_t1 - _profiler_##name##_t0).count()
#define PROFILER_GET_AVG(name) (_profiler_##name##_sum / _profiler_##name##_count)

#define PROFILER_RESET(name)                                                                       \
    _profiler_##name##_count = 0.0;                                                                \
    _profiler_##name##_sum = 0.0;
