#pragma once

#include <QByteArray>
#include <QElapsedTimer>

#include <cstdint>
#include <optional>
#include <vector>

namespace hermes {

// How busy the processor is, in percent, since it was last asked.
struct CpuUsage {
    double total = 0;
    std::vector<double> cores;
};

// Memory and swap in bytes.
struct MemoryUsage {
    uint64_t total = 0;
    uint64_t used = 0; // all that is not available to programs
    uint64_t swapTotal = 0;
    uint64_t swapUsed = 0;
};

// Bytes per second through every network interface but the loopback, since last asked.
struct NetworkSpeed {
    double down = 0;
    double up = 0;
};

// What the kernel counts in /proc, read as applets ask for it. Asked again sooner than a
// quarter of a second, it tells what it told last, so several applets see the same.
class SystemUsage {
public:
    CpuUsage cpu();
    MemoryUsage memory();
    NetworkSpeed network();

    // A processor's time, in the kernel's ticks: all of it, and that spent idle.
    struct CpuTimes {
        uint64_t total = 0;
        uint64_t idle = 0;
    };
    // The whole processor first, then each core, as /proc/stat lists them.
    static std::vector<CpuTimes> parseStat(const QByteArray& text);
    static MemoryUsage parseMeminfo(const QByteArray& text);
    // Bytes received and sent in all, as /proc/net/dev counts them.
    struct NetworkTotals {
        uint64_t received = 0;
        uint64_t sent = 0;
    };
    static NetworkTotals parseNetDev(const QByteArray& text);
    // The share of time busy between two readings, in percent.
    static CpuUsage usage(const std::vector<CpuTimes>& before, const std::vector<CpuTimes>& after);

private:
    std::vector<CpuTimes> m_cpuTimes;
    CpuUsage m_cpu;
    QElapsedTimer m_cpuRead;

    std::optional<NetworkTotals> m_networkTotals;
    NetworkSpeed m_network;
    QElapsedTimer m_networkRead;
};

} // namespace hermes
