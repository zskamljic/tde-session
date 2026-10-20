#include "Fingerprint.hpp"

#include <poll.h>
#include <time.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <utility>

namespace cerberus {
namespace {

constexpr const char* Service = "net.reactivated.Fprint";
constexpr const char* DeviceInterface = "net.reactivated.Fprint.Device";

// A list of strings sd-bus made, each freed and then the list.
void freeStrings(char** strings)
{
    for (char** string = strings; *string; ++string)
        std::free(*string);
    std::free(strings);
}

} // namespace

Fingerprint::Fingerprint(const std::string& user)
    : m_user(user)
{
    sd_bus* bus = nullptr;
    if (sd_bus_open_system(&bus) < 0)
        return;
    m_bus.reset(bus);
    find();
}

void Fingerprint::find()
{
    // The reader fprintd uses by default, if there is one; fprintd starts when asked.
    m_signal.reset();
    m_device.clear();
    call("GetDefaultDevice", &Fingerprint::found, nullptr, "/net/reactivated/Fprint/Manager",
        "net.reactivated.Fprint.Manager");
}

void Fingerprint::call(const char* method, Then then, const char* argument, const char* path, const char* interface)
{
    if (!path)
        path = m_device.c_str();
    if (!interface)
        interface = DeviceInterface;
    // Without a slot of its own, the call's goes with the bus, should fprintd never answer.
    const int result = argument
        ? sd_bus_call_method_async(
              m_bus.get(), nullptr, Service, path, interface, method, answered, this, "s", argument)
        : sd_bus_call_method_async(m_bus.get(), nullptr, Service, path, interface, method, answered, this, "");
    if (result < 0) {
        std::fprintf(stderr, "tde-cerberus: the fingerprint reader: %s: %s\n", method, std::strerror(-result));
        (this->*then)(nullptr);
        return;
    }
    m_then = then;
}

int Fingerprint::answered(sd_bus_message* reply, void* data, sd_bus_error*)
{
    auto* self = static_cast<Fingerprint*>(data);
    const Then then = std::exchange(self->m_then, nullptr);
    if (const sd_bus_error* error = sd_bus_message_get_error(reply)) {
        std::fprintf(
            stderr, "tde-cerberus: the fingerprint reader: %s\n", error->message ? error->message : error->name);
        reply = nullptr;
    }
    (self->*then)(reply);
    return 0;
}

void Fingerprint::found(sd_bus_message* reply)
{
    const char* path = nullptr;
    if (!reply || sd_bus_message_read(reply, "o", &path) < 0)
        return;
    m_device = path;
    // Only with fingers to match; none is an error too, the usual one.
    call("ListEnrolledFingers", &Fingerprint::listed, m_user.c_str());
}

void Fingerprint::listed(sd_bus_message* reply)
{
    char** names = nullptr;
    if (reply)
        sd_bus_message_read_strv(reply, &names);
    const Owned<char*, freeStrings> fingers(names);
    if (!fingers || !fingers.get()[0]) {
        m_device.clear();
        return;
    }
    m_wanted = true;
    sd_bus_slot* slot = nullptr;
    sd_bus_match_signal_async(
        m_bus.get(), &slot, Service, m_device.c_str(), DeviceInterface, "VerifyStatus", status, nullptr, this);
    m_signal.reset(slot);
    claim();
}

void Fingerprint::claim()
{
    call("Claim", &Fingerprint::claimed, m_user.c_str());
}

void Fingerprint::claimed(sd_bus_message* reply)
{
    m_claimed = reply != nullptr;
    if (m_claimed)
        verify();
}

void Fingerprint::verify()
{
    call("VerifyStart", &Fingerprint::started, "any");
}

void Fingerprint::started(sd_bus_message* reply)
{
    m_state = reply ? State::Waiting : State::Unavailable;
}

void Fingerprint::stopped(sd_bus_message*)
{
    // Once the reader went away, it is let go of and claimed anew.
    if (m_state == State::Unavailable)
        call("Release", &Fingerprint::released);
    else
        verify();
}

void Fingerprint::released(sd_bus_message*)
{
    // A reader plugged in anew, as after sleeping, may be another object of fprintd's.
    m_claimed = false;
    find();
}

void Fingerprint::restart()
{
    if (m_state != State::Unavailable || !m_wanted || m_then)
        return;
    if (m_claimed)
        call("VerifyStop", &Fingerprint::stopped);
    else
        find();
}

int Fingerprint::fd() const
{
    return m_bus && (m_then || m_signal || m_wanted) ? sd_bus_get_fd(m_bus.get()) : -1;
}

short Fingerprint::events() const
{
    const int events = m_bus ? sd_bus_get_events(m_bus.get()) : 0;
    return short(events > 0 ? events : POLLIN);
}

int Fingerprint::timeout() const
{
    uint64_t until = UINT64_MAX;
    if (fd() < 0 || sd_bus_get_timeout(m_bus.get(), &until) < 0 || until == UINT64_MAX)
        return -1;
    timespec now {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    const uint64_t current = uint64_t(now.tv_sec) * 1'000'000 + uint64_t(now.tv_nsec) / 1000;
    if (until <= current)
        return 0;
    // Rounded up, so as not to wake before it is due.
    return int(std::min<uint64_t>((until - current + 999) / 1000, INT_MAX));
}

int Fingerprint::status(sd_bus_message* message, void* data, sd_bus_error*)
{
    auto* self = static_cast<Fingerprint*>(data);
    const char* result = nullptr;
    int done = 0;
    if (sd_bus_message_read(message, "sb", &result, &done) < 0)
        return 0;
    const std::string_view what = result;
    if (what == "verify-match") {
        self->m_state = State::Matched;
    } else if (what == "verify-no-match") {
        self->m_state = State::NoMatch;
    } else if (what == "verify-disconnected" || what == "verify-unknown-error") {
        self->m_state = State::Unavailable;
    } else {
        // Too short, off centre and the like: the finger once more.
        self->m_state = State::Retry;
    }
    // A verification that ended is stopped, and started again unless it matched.
    if (done)
        self->m_again = self->m_state != State::Matched && self->m_state != State::Unavailable;
    return 0;
}

bool Fingerprint::process()
{
    if (fd() < 0)
        return false;
    const State before = m_state;
    while (sd_bus_process(m_bus.get(), nullptr) > 0) { }
    if (m_again && !m_then) {
        m_again = false;
        call("VerifyStop", &Fingerprint::stopped);
    }
    return m_state != before;
}

} // namespace cerberus
