#pragma once

#include "Listener.hpp"
#include "wlr.hpp"

#include <memory>
#include <set>
#include <vector>

namespace atlas {

class Output;
class Server;

// A lock screen holding the session, through ext-session-lock: its surfaces cover the
// outputs, above everything else, and only they get the keyboard and the pointer.
class SessionLock {
public:
    SessionLock(Server& server, wlr_session_lock_v1* lock);
    ~SessionLock();

    SessionLock(const SessionLock&) = delete;
    SessionLock& operator=(const SessionLock&) = delete;

    // Whether `surface` is part of the lock screen, so it may have the keyboard and pointer.
    bool owns(wlr_surface* surface) const;
    // An output drew a frame; once every one has, with the lock showing, the lock screen
    // hears that the session is locked.
    void framed(const Output& output);
    // The outputs changed: each lock surface gets the size of its output again.
    void arrange();

private:
    struct Surface {
        wlr_session_lock_surface_v1* lockSurface = nullptr;
        wlr_scene_tree* tree = nullptr;
        Listener map;
        Listener destroy;
    };

    void addSurface(wlr_session_lock_surface_v1* lockSurface);
    void surfaceDestroyed(Surface& surface);
    void focusAny();

    Server& m_server;
    wlr_session_lock_v1* m_lock;
    bool m_sentLocked = false;
    std::set<const Output*> m_drawn;
    std::vector<std::unique_ptr<Surface>> m_surfaces;
    Listener m_newSurface;
    Listener m_unlock;
    Listener m_destroy;
};

} // namespace atlas
