#include "LoginHint.hpp"

#include "Owned.hpp"

#include <systemd/sd-bus.h>

#include <cstdio>

namespace cerberus {

void setLockedHint(bool locked)
{
    sd_bus* opened = nullptr;
    if (sd_bus_open_system(&opened) < 0)
        return;
    const Owned<sd_bus, sd_bus_flush_close_unref> bus(opened);
    // Not long: the lock screen waits for it.
    sd_bus_set_method_call_timeout(bus.get(), 2'000'000);
    // "auto" is the session of the caller, or, for one started outside any session, as by the
    // compositor or the bar, the user's graphical session.
    sd_bus_error error = SD_BUS_ERROR_NULL;
    if (sd_bus_call_method(bus.get(), "org.freedesktop.login1", "/org/freedesktop/login1/session/auto",
            "org.freedesktop.login1.Session", "SetLockedHint", &error, nullptr, "b", int(locked))
        < 0) {
        std::fprintf(stderr, "tde-cerberus: could not tell logind the session is %s: %s\n",
            locked ? "locked" : "unlocked", error.message ? error.message : "no answer");
    }
    sd_bus_error_free(&error);
}

} // namespace cerberus
