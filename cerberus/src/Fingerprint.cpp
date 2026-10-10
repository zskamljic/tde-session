#include "Fingerprint.hpp"

#include <poll.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

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
    using Message = Owned<sd_bus_message, sd_bus_message_unref>;
    sd_bus* bus = nullptr;
    if (sd_bus_open_system(&bus) < 0)
        return;
    m_bus.reset(bus);

    // The reader fprintd uses by default, if there is one; fprintd starts when asked.
    sd_bus_message* answer = nullptr;
    const int found = sd_bus_call_method(m_bus.get(), Service, "/net/reactivated/Fprint/Manager",
        "net.reactivated.Fprint.Manager", "GetDefaultDevice", nullptr, &answer, "");
    Message reply(answer);
    const char* path = nullptr;
    if (found < 0 || sd_bus_message_read(reply.get(), "o", &path) < 0)
        return;
    m_device = path;

    // Only with fingers to match; none is an error too, the usual one.
    answer = nullptr;
    const int listed = sd_bus_call_method(m_bus.get(), Service, m_device.c_str(), DeviceInterface,
        "ListEnrolledFingers", nullptr, &answer, "s", m_user.c_str());
    reply.reset(answer);
    char** names = nullptr;
    if (listed < 0 || sd_bus_message_read_strv(reply.get(), &names) < 0)
        return;
    const Owned<char*, freeStrings> fingers(names);
    if (!fingers || !fingers.get()[0])
        return;

    sd_bus_slot* slot = nullptr;
    sd_bus_match_signal(m_bus.get(), &slot, Service, m_device.c_str(), DeviceInterface, "VerifyStatus", status, this);
    m_signal.reset(slot);
    start();
}

Fingerprint::~Fingerprint()
{
    stop();
    if (m_claimed)
        call("Release");
}

bool Fingerprint::call(const char* method, const char* types, const char* argument)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    const int result = types
        ? sd_bus_call_method(
              m_bus.get(), Service, m_device.c_str(), DeviceInterface, method, &error, nullptr, types, argument)
        : sd_bus_call_method(m_bus.get(), Service, m_device.c_str(), DeviceInterface, method, &error, nullptr, "");
    if (result < 0)
        std::fprintf(stderr, "tde-cerberus: the fingerprint reader: %s: %s\n", method,
            error.message ? error.message : std::strerror(-result));
    sd_bus_error_free(&error);
    return result >= 0;
}

void Fingerprint::start()
{
    if (!m_bus || m_device.empty())
        return;
    if (!m_claimed)
        m_claimed = call("Claim", "s", m_user.c_str());
    m_verifying = m_claimed && call("VerifyStart", "s", "any");
    m_state = m_verifying ? State::Waiting : State::Unavailable;
}

void Fingerprint::stop()
{
    if (m_verifying)
        call("VerifyStop");
    m_verifying = false;
}

void Fingerprint::restart()
{
    if (m_state != State::Unavailable || m_device.empty() || !m_signal)
        return;
    if (m_claimed) {
        call("Release");
        m_claimed = false;
    }
    start();
}

int Fingerprint::fd() const
{
    return m_bus && m_signal ? sd_bus_get_fd(m_bus.get()) : -1;
}

short Fingerprint::events() const
{
    const int events = m_bus ? sd_bus_get_events(m_bus.get()) : 0;
    return short(events > 0 ? events : POLLIN);
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
    if (!m_bus)
        return false;
    const State before = m_state;
    while (sd_bus_process(m_bus.get(), nullptr) > 0) { }
    // Calls wait until the signal's handling is over.
    if (m_again) {
        m_again = false;
        stop();
        m_verifying = call("VerifyStart", "s", "any");
        if (!m_verifying)
            m_state = State::Unavailable;
    } else if (m_state == State::Unavailable || m_state == State::Matched) {
        stop();
    }
    return m_state != before;
}

} // namespace cerberus
