#pragma once

#include "Owned.hpp"
#include "wlr.hpp"

#include <string>

namespace atlas {

// Which output X11 programs take for the primary one, through RandR as GNOME has it. Wine puts
// the primary output at 0,0 and the others around it; games that take their window to begin
// there, as World of Warcraft does when it puts the pointer back in the middle to look around,
// only work on that output.
class PrimaryOutput {
public:
    PrimaryOutput() = default;
    ~PrimaryOutput();

    PrimaryOutput(const PrimaryOutput&) = delete;
    PrimaryOutput& operator=(const PrimaryOutput&) = delete;

    // Xwayland is up at `display`, heard from in `loop`; once it is gone, nothing is sent
    // until it is up again.
    void connect(wl_event_loop* loop, const char* display);
    // The output of this name is to be the primary one, now and whenever Xwayland's outputs
    // change.
    void set(std::string name);
    // Xwayland is about to go.
    void disconnect();

private:
    void read(uint32_t mask);
    void apply();

    xcb_connection_t* m_connection = nullptr;
    EventSource m_source;
    uint8_t m_randrEvents = 0; // where RandR's events begin
    std::string m_name;
};

} // namespace atlas
