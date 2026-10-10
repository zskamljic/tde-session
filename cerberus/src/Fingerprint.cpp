#include "Fingerprint.hpp"

#include <systemd/sd-bus.h>

#include <poll.h>

#include <cstdio>
#include <cstring>
#include <string_view>

namespace cerberus {
namespace {

constexpr const char* Service = "net.reactivated.Fprint";
constexpr const char* DeviceInterface = "net.reactivated.Fprint.Device";

} // namespace

Fingerprint::Fingerprint(const std::string& user)
    : m_user(user)
{
    if (sd_bus_open_system(&m_bus) < 0) {
        m_bus = nullptr;
        return;
    }
    // The reader fprintd uses by default, if there is one; fprintd starts when asked.
    sd_bus_message* reply = nullptr;
    const char* path = nullptr;
    if (sd_bus_call_method(m_bus, Service, "/net/reactivated/Fprint/Manager", "net.reactivated.Fprint.Manager",
            "GetDefaultDevice", nullptr, &reply, "")
            < 0
        || sd_bus_message_read(reply, "o", &path) < 0) {
        sd_bus_message_unref(reply);
        return;
    }
    m_device = path;
    sd_bus_message_unref(reply);

    // Only with fingers to match.
    reply = nullptr;
    if (sd_bus_call_method(m_bus, Service, m_device.c_str(), DeviceInterface, "ListEnrolledFingers", nullptr, &reply,
            "s", m_user.c_str())
        < 0) {
        // No fingers is an error too, the usual one.
        sd_bus_message_unref(reply);
        return;
    }
    char** fingers = nullptr;
    const bool enrolled = sd_bus_message_read_strv(reply, &fingers) >= 0 && fingers && fingers[0];
    for (char** finger = fingers; finger && *finger; ++finger)
        free(*finger);
    free(fingers);
    sd_bus_message_unref(reply);
    if (!enrolled)
        return;

    sd_bus_match_signal(m_bus, &m_signal, Service, m_device.c_str(), DeviceInterface, "VerifyStatus", status, this);
    start();
}

Fingerprint::~Fingerprint()
{
    stop();
    if (m_claimed)
        call("Release");
    sd_bus_slot_unref(m_signal);
    if (m_bus)
        sd_bus_flush_close_unref(m_bus);
}

bool Fingerprint::call(const char* method, const char* types, const char* argument)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    const int result = types
        ? sd_bus_call_method(
              m_bus, Service, m_device.c_str(), DeviceInterface, method, &error, nullptr, types, argument)
        : sd_bus_call_method(m_bus, Service, m_device.c_str(), DeviceInterface, method, &error, nullptr, "");
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
    return m_bus && m_signal ? sd_bus_get_fd(m_bus) : -1;
}

short Fingerprint::events() const
{
    const int events = m_bus ? sd_bus_get_events(m_bus) : 0;
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
    while (sd_bus_process(m_bus, nullptr) > 0) { }
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
