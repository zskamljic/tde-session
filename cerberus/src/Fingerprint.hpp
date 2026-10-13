#pragma once

#include "Owned.hpp"

#include <systemd/sd-bus.h>

#include <string>

namespace cerberus {

// Unlocking with a finger, through fprintd, when the computer has a reader and the user has
// fingers enrolled on it. The reader is held and listened to from the start; its bus is
// polled along with the rest of the lock screen. Nothing here waits for fprintd, which may
// take its time answering, so typing the password goes on meanwhile.
class Fingerprint {
public:
    enum class State {
        Unavailable, // no reader, no fingers enrolled, or fprintd is not there
        Waiting, // for a finger
        Matched,
        NoMatch, // a finger that is not one of the user's
        Retry, // the reader could not read it well
    };

    explicit Fingerprint(const std::string& user);

    Fingerprint(const Fingerprint&) = delete;
    Fingerprint& operator=(const Fingerprint&) = delete;

    State state() const { return m_state; }
    // For poll(): -1 while there is nothing to listen to.
    int fd() const;
    short events() const;
    // In milliseconds, until process() is due though fd() said nothing; -1 for never.
    int timeout() const;
    // Handles what the bus brought and calls fprintd took too long to answer; returns
    // whether the state changed.
    bool process();
    // Tries again after the reader went away, as it may over sleeping.
    void restart();

private:
    using Then = void (Fingerprint::*)(sd_bus_message* reply);

    static int answered(sd_bus_message* reply, void* data, sd_bus_error* error);
    static int status(sd_bus_message* message, void* data, sd_bus_error* error);
    // Calls `method` of the reader, or of what `path` and `interface` name, without waiting;
    // `then` gets the reply, or nullptr for an error.
    void call(const char* method, Then then, const char* argument = nullptr, const char* path = nullptr,
        const char* interface = nullptr);

    void found(sd_bus_message* reply);
    void listed(sd_bus_message* reply);
    void claim();
    void claimed(sd_bus_message* reply);
    void verify();
    void started(sd_bus_message* reply);
    void stopped(sd_bus_message* reply);
    void released(sd_bus_message* reply);

    std::string m_user;
    Owned<sd_bus, sd_bus_flush_close_unref> m_bus;
    Owned<sd_bus_slot, sd_bus_slot_unref> m_signal; // fprintd's word on a finger, while it is listened to
    std::string m_device; // its object path; empty without a reader or fingers to match
    State m_state = State::Unavailable;
    Then m_then = nullptr; // for the call fprintd is yet to answer, the only one at a time
    bool m_claimed = false;
    bool m_again = false; // a verification ended without a match: another starts
};

} // namespace cerberus
