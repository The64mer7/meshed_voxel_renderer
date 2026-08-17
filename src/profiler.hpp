#pragma once

#define PROFILER_DECLARE(name) \
double _profiler_##name##_count = 0.0;\
double _profiler_##name##_sum = 0.0;

#define PROFILER_BEGIN(name)\
double _profiler_##name##_t0 = glfwGetTime();

#define PROFILER_END(name)\
double _profiler_##name##_t1 = glfwGetTime();\
_profiler_##name##_count += 1.0;\
_profiler_##name##_sum += _profiler_##name##_t1 - _profiler_##name##_t0;

#define PROFILER_GET_AVG_NESTED(name, parent) parent._profiler_##name##_sum / parent._profiler_##name##_count
#define PROFILER_GET(name) (_profiler_##name##_t1 - _profiler_##name##_t0)
#define PROFILER_GET_AVG(name) _profiler_##name##_sum / _profiler_##name##_count