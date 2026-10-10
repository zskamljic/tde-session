#pragma once

#include "Owned.hpp"

#include <systemd/sd-bus.h>

#include <string>

namespace cerberus {

// Unlocking with a finger, through fprintd, when the computer has a reader and the user has
// fingers enrolled on it. The reader is held and listened to from the start; its bus is
// polled along with the rest of the lock screen.
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
    ~Fingerprint();

    Fingerprint(const Fingerprint&) = delete;
    Fingerprint& operator=(const Fingerprint&) = delete;

    State state() const { return m_state; }
    // For poll(): -1 while unavailable.
    int fd() const;
    short events() const;
    // Handles what the bus brought; returns whether the state changed.
    bool process();
    // Tries again after the reader went away, as it may over sleeping.
    void restart();

private:
    static int status(sd_bus_message* message, void* data, sd_bus_error* error);
    bool call(const char* method, const char* types = nullptr, const char* argument = nullptr);
    void start();
    void stop();

    std::string m_user;
    Owned<sd_bus, sd_bus_flush_close_unref> m_bus;
    Owned<sd_bus_slot, sd_bus_slot_unref> m_signal; // fprintd's word on a finger, while it is listened to
    std::string m_device; // its object path
    State m_state = State::Unavailable;
    bool m_claimed = false;
    bool m_verifying = false;
    bool m_again = false; // a verification ended without a match: another starts
};

} // namespace cerberus
