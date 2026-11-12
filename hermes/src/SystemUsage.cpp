#include "SystemUsage.hpp"

#include <QFile>
#include <QList>

#include <algorithm>

namespace hermes {
namespace {

constexpr qint64 Fresh = 250; // ms that a reading is told again rather than taken anew

QByteArray readProc(const char* path)
{
    // Files of /proc tell no size, so they are read until they end.
    QFile file(QString::fromLatin1(path));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

uint64_t number(const QByteArray& text)
{
    bool ok = false;
    const qulonglong value = text.toULongLong(&ok);
    return ok ? value : 0;
}

} // namespace

std::vector<SystemUsage::CpuTimes> SystemUsage::parseStat(const QByteArray& text)
{
    std::vector<CpuTimes> times;
    for (const QByteArray& line : text.split('\n')) {
        if (!line.startsWith("cpu"))
            continue;
        const QList<QByteArray> fields = line.simplified().split(' ');
        // user nice system idle iowait irq softirq steal; guests are counted in user already.
        CpuTimes cpu;
        for (qsizetype i = 1; i < std::min<qsizetype>(fields.size(), 9); ++i) {
            const uint64_t value = number(fields[i]);
            cpu.total += value;
            if (i == 4 || i == 5)
                cpu.idle += value;
        }
        times.push_back(cpu);
    }
    return times;
}

MemoryUsage SystemUsage::parseMeminfo(const QByteArray& text)
{
    uint64_t total = 0, available = 0, swapTotal = 0, swapFree = 0;
    for (const QByteArray& line : text.split('\n')) {
        const QList<QByteArray> fields = line.simplified().split(' ');
        if (fields.size() < 2)
            continue;
        const uint64_t bytes = number(fields[1]) * 1024; // given in kB
        if (fields[0] == "MemTotal:")
            total = bytes;
        else if (fields[0] == "MemAvailable:")
            available = bytes;
        else if (fields[0] == "SwapTotal:")
            swapTotal = bytes;
        else if (fields[0] == "SwapFree:")
            swapFree = bytes;
    }
    return {
        .total = total,
        .used = total - std::min(available, total),
        .swapTotal = swapTotal,
        .swapUsed = swapTotal - std::min(swapFree, swapTotal),
    };
}

SystemUsage::NetworkTotals SystemUsage::parseNetDev(const QByteArray& text)
{
    NetworkTotals totals;
    for (const QByteArray& line : text.split('\n')) {
        const qsizetype colon = line.indexOf(':');
        if (colon < 0)
            continue;
        if (line.left(colon).trimmed() == "lo")
            continue;
        // Received: bytes packets errs drop fifo frame compressed multicast; then sent: bytes ...
        const QList<QByteArray> fields = line.mid(colon + 1).simplified().split(' ');
        if (fields.size() < 9)
            continue;
        totals.received += number(fields[0]);
        totals.sent += number(fields[8]);
    }
    return totals;
}

CpuUsage SystemUsage::usage(const std::vector<CpuTimes>& before, const std::vector<CpuTimes>& after)
{
    const auto percent = [](const CpuTimes& from, const CpuTimes& to) {
        const uint64_t total = to.total > from.total ? to.total - from.total : 0;
        const uint64_t idle = to.idle > from.idle ? to.idle - from.idle : 0;
        return total ? 100.0 * double(total - std::min(idle, total)) / double(total) : 0.0;
    };
    CpuUsage usage;
    for (size_t i = 0; i < after.size(); ++i) {
        const double value = percent(i < before.size() ? before[i] : CpuTimes {}, after[i]);
        if (i == 0)
            usage.total = value;
        else
            usage.cores.push_back(value);
    }
    return usage;
}

CpuUsage SystemUsage::cpu()
{
    if (m_cpuRead.isValid() && m_cpuRead.elapsed() < Fresh)
        return m_cpu;
    // The first reading tells the share since the computer started.
    std::vector<CpuTimes> times = parseStat(readProc("/proc/stat"));
    m_cpu = usage(m_cpuTimes, times);
    m_cpuTimes = std::move(times);
    m_cpuRead.start();
    return m_cpu;
}

MemoryUsage SystemUsage::memory()
{
    return parseMeminfo(readProc("/proc/meminfo"));
}

NetworkSpeed SystemUsage::network()
{
    if (m_networkRead.isValid() && m_networkRead.elapsed() < Fresh)
        return m_network;
    const NetworkTotals totals = parseNetDev(readProc("/proc/net/dev"));
    if (m_networkTotals && m_networkRead.isValid()) {
        const double seconds = double(m_networkRead.elapsed()) / 1000.0;
        // Counters start again when an interface goes away; that counts as nothing.
        const auto rate
            = [seconds](uint64_t from, uint64_t to) { return to > from ? double(to - from) / seconds : 0.0; };
        m_network = {rate(m_networkTotals->received, totals.received), rate(m_networkTotals->sent, totals.sent)};
    }
    m_networkTotals = totals;
    m_networkRead.start();
    return m_network;
}

} // namespace hermes
