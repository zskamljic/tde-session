#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>

namespace atlas {

// How often what moves is moved on, in milliseconds.
constexpr int FrameInterval = 8;

// Milliseconds of the monotonic clock, to time what moves.
inline uint64_t monotonicMs()
{
    timespec time {};
    clock_gettime(CLOCK_MONOTONIC, &time);
    return uint64_t(time.tv_sec) * 1000 + uint64_t(time.tv_nsec) / 1'000'000;
}

// How far something taking `duration` ms has come after `elapsed`, from 0 to 1, slowing down
// towards the end; all the way at once without a duration.
inline double progress(uint64_t elapsed, int duration)
{
    if (duration <= 0)
        return 1;
    const double t = std::min(1.0, double(elapsed) / duration);
    return 1 - std::pow(1 - t, 3);
}

// The point `t` of the way from `from` to `to`.
inline double between(double from, double to, double t)
{
    return from + (to - from) * t;
}

} // namespace atlas
