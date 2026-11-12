#include "Lift.hpp"

#include <sys/timerfd.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>

#include <unistd.h>

namespace cerberus {
namespace {

constexpr long FrameNs = 16'000'000; // about sixty frames a second

long long nowMs()
{
    timespec now {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000LL + now.tv_nsec / 1'000'000;
}

// Quick at first, slowing as it arrives.
double easeOut(double t)
{
    return 1 - std::pow(1 - t, 3);
}

} // namespace

Lift::Lift()
    : m_timer(timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK))
{
}

void Lift::set(bool lifted)
{
    if (lifted == m_lifted)
        return;
    // Turned around halfway, it goes back from where it is, and as quickly as it came.
    const double position = value();
    m_lifted = lifted;
    m_from = position;
    m_at = 0;
    m_started = nowMs();
    startTimer(true);
}

bool Lift::step()
{
    uint64_t expirations = 0;
    while (read(m_timer.get(), &expirations, sizeof(expirations)) > 0) { }
    const double distance = std::abs((m_lifted ? 1.0 : 0.0) - m_from);
    const double duration = std::max(1.0, Duration * distance);
    m_at = std::min(1.0, double(nowMs() - m_started) / duration);
    if (m_at >= 1)
        startTimer(false);
    return m_at < 1;
}

double Lift::value() const
{
    const double target = m_lifted ? 1.0 : 0.0;
    if (m_started == 0)
        return target;
    return m_from + (target - m_from) * easeOut(m_at);
}

void Lift::startTimer(bool on)
{
    itimerspec timer {};
    if (on) {
        timer.it_value.tv_nsec = FrameNs;
        timer.it_interval.tv_nsec = FrameNs;
    }
    timerfd_settime(m_timer.get(), 0, &timer, nullptr);
}

Shown clockShown(double lift, double rise)
{
    return {1 - lift, -lift * rise};
}

Shown underShown(double lift, double rise)
{
    return {lift, (1 - lift) * rise};
}

} // namespace cerberus
