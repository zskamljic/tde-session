#include "Lock.hpp"

#include "Parts.hpp"
#include "Server.hpp"

#include <algorithm>

namespace atlas {

SessionLock::SessionLock(Server& server, wlr_session_lock_v1* lock)
    : m_server(server)
    , m_lock(lock)
{
    m_newSurface.connect<wlr_session_lock_surface_v1>(
        lock->events.new_surface, [this](wlr_session_lock_surface_v1* surface) { addSurface(surface); });
    // Both end this object; nothing of it may run after the server is told.
    m_unlock.connect(lock->events.unlock, [this] { m_server.unlocked(); });
    m_destroy.connect(lock->events.destroy, [this] { m_server.lockAbandoned(); });

    // Without outputs there is nothing to show first.
    if (m_server.outputs.empty()) {
        wlr_session_lock_v1_send_locked(m_lock);
        m_sentLocked = true;
    }
}

SessionLock::~SessionLock()
{
    m_surfaces.clear();
}

bool SessionLock::owns(wlr_surface* surface) const
{
    if (!surface)
        return false;
    wlr_surface* root = wlr_surface_get_root_surface(surface);
    return std::ranges::any_of(m_surfaces, [root](const auto& s) { return s->lockSurface->surface == root; });
}

void SessionLock::framed(const Output& output)
{
    if (m_sentLocked)
        return;
    m_drawn.insert(&output);
    const bool all = std::ranges::all_of(
        m_server.outputs, [this](const auto& o) { return m_drawn.contains(o.get()) || !o->output->enabled; });
    if (all) {
        wlr_session_lock_v1_send_locked(m_lock);
        m_sentLocked = true;
    }
}

void SessionLock::arrange()
{
    for (const auto& surface : m_surfaces) {
        wlr_box box {};
        wlr_output_layout_get_box(m_server.outputLayout, surface->lockSurface->output, &box);
        wlr_scene_node_set_position(&surface->tree->node, box.x, box.y);
        wlr_session_lock_surface_v1_configure(surface->lockSurface, uint32_t(box.width), uint32_t(box.height));
    }
}

void SessionLock::addSurface(wlr_session_lock_surface_v1* lockSurface)
{
    auto surface = std::make_unique<Surface>();
    surface->lockSurface = lockSurface;
    surface->tree = wlr_scene_subsurface_tree_create(m_server.layers.lock, lockSurface->surface);

    wlr_box box {};
    wlr_output_layout_get_box(m_server.outputLayout, lockSurface->output, &box);
    wlr_scene_node_set_position(&surface->tree->node, box.x, box.y);
    wlr_session_lock_surface_v1_configure(lockSurface, uint32_t(box.width), uint32_t(box.height));

    Surface* raw = surface.get();
    surface->map.connect(lockSurface->surface->events.map, [this] { focusAny(); });
    surface->destroy.connect(lockSurface->events.destroy, [this, raw] { surfaceDestroyed(*raw); });
    m_surfaces.push_back(std::move(surface));
}

void SessionLock::surfaceDestroyed(Surface& surface)
{
    // Its scene tree goes with the surface.
    const bool hadKeyboard = m_server.seat->keyboard_state.focused_surface == surface.lockSurface->surface;
    std::erase_if(m_surfaces, [&](const auto& s) { return s.get() == &surface; });
    if (hadKeyboard)
        focusAny();
}

// The keyboard goes to a lock surface, unless one has it already.
void SessionLock::focusAny()
{
    if (owns(m_server.seat->keyboard_state.focused_surface))
        return;
    for (const auto& surface : m_surfaces) {
        if (surface->lockSurface->surface->mapped) {
            m_server.focusSurface(surface->lockSurface->surface);
            return;
        }
    }
}

// Server ----------------------------------------------------------------------------------

void Server::setUpLock()
{
    auto* manager = wlr_session_lock_manager_v1_create(display);
    m_newLock.connect<wlr_session_lock_v1>(
        manager->events.new_lock, [this](wlr_session_lock_v1* lock) { newLock(lock); });
}

void Server::newLock(wlr_session_lock_v1* lock)
{
    // One lock screen at a time; another may only take the place of one that went away.
    if (m_lock) {
        wlr_session_lock_v1_destroy(lock);
        return;
    }
    if (!m_locked) {
        m_locked = true;
        // Nothing of what was going on stays in hand.
        if (m_cursorMode != CursorMode::Passthrough)
            finishGrab();
        if (m_cycling)
            finishCycling();
        wlr_seat_keyboard_clear_focus(seat);
        wlr_seat_pointer_clear_focus(seat);
    }
    coverOutputs();
    m_lock = std::make_unique<SessionLock>(*this, lock);
}

// Everything is hidden behind the cover until the lock screen draws over it, and stays so if
// the lock screen goes away without unlocking.
void Server::coverOutputs()
{
    if (!m_locked) {
        if (m_lockCover) {
            wlr_scene_node_destroy(&m_lockCover->node);
            m_lockCover = nullptr;
        }
        return;
    }
    wlr_box box {};
    wlr_output_layout_get_box(outputLayout, nullptr, &box);
    static const float color[4] {0x26 / 255.0f, 0x2a / 255.0f, 0x33 / 255.0f, 1};
    if (!m_lockCover) {
        m_lockCover = wlr_scene_rect_create(layers.lock, std::max(box.width, 1), std::max(box.height, 1), color);
        wlr_scene_node_lower_to_bottom(&m_lockCover->node);
    }
    wlr_scene_rect_set_size(m_lockCover, std::max(box.width, 1), std::max(box.height, 1));
    wlr_scene_node_set_position(&m_lockCover->node, box.x, box.y);
}

void Server::unlocked()
{
    m_lock.reset();
    m_locked = false;
    coverOutputs();
    // The keyboard goes back where it was: a layer surface that asks for it, or a window.
    m_layerFocus = nullptr;
    updateLayerFocus();
    if (!m_layerFocus)
        focusTopmost();
}

void Server::lockAbandoned()
{
    // The lock screen died without unlocking: the session stays locked, and a new one is
    // started to let the user back in.
    m_lock.reset();
    if (!m_relock) {
        m_relock = wl_event_loop_add_timer(
            eventLoop,
            [](void*) {
                Server::spawn("tde-cerberus");
                return 0;
            },
            nullptr);
    }
    // A moment later, so one that fails at once is not started over and over.
    wl_event_source_timer_update(m_relock, 1000);
}

void Server::outputFramed(const Output& output)
{
    if (m_lock)
        m_lock->framed(output);
}

} // namespace atlas
