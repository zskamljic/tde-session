#include "Parts.hpp"

#include <algorithm>

namespace atlas {

// Output ----------------------------------------------------------------------------------

Output::Output(Server& server, wlr_output* output)
    : server(server)
    , output(output)
{
    m_frame.connect(output->events.frame, [this] {
        wlr_scene_output* sceneOutput = wlr_scene_get_scene_output(this->server.scene, this->output);
        if (!sceneOutput)
            return;
        wlr_scene_output_commit(sceneOutput, nullptr);
        timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        wlr_scene_output_send_frame_done(sceneOutput, &now);
        this->server.outputFramed(*this);
    });
    // Nested backends ask for a new size when their window is resized.
    m_requestState.connect<wlr_output_event_request_state>(output->events.request_state,
        [this](wlr_output_event_request_state* event) { wlr_output_commit_state(this->output, event->state); });
    m_destroy.connect(output->events.destroy, [this] { this->server.outputDestroyed(*this); });
}

Output::~Output()
{
    if (m_retry)
        wl_event_source_remove(m_retry);
}

void Output::retry(int delay)
{
    if (!m_retry) {
        m_retry = wl_event_loop_add_timer(
            server.eventLoop,
            [](void* data) {
                auto* self = static_cast<Output*>(data);
                // Turned on meanwhile, as by the settings: nothing to do.
                if (!self->output->enabled)
                    self->server.turnOn(*self);
                return 0;
            },
            this);
    }
    wl_event_source_timer_update(m_retry, delay);
}

wlr_box Output::box() const
{
    wlr_box box {};
    wlr_output_layout_get_box(server.outputLayout, output, &box);
    return box;
}

// Popup -----------------------------------------------------------------------------------

Popup::Popup(Server& server, wlr_xdg_popup* popup)
    : popup(popup)
    , m_server(server)
{
    m_commit.connect(popup->base->surface->events.commit, [this] {
        if (this->popup->base->initial_commit) {
            unconstrain();
            wlr_xdg_surface_schedule_configure(this->popup->base);
        }
    });
    m_destroy.connect(popup->events.destroy, [this] { m_server.popupDestroyed(*this); });
}

void Popup::unconstrain()
{
    // Keeps the popup on the screen; the box is relative to the window it belongs to.
    wlr_surface* parent = popup->parent;
    while (wlr_xdg_surface* surface = wlr_xdg_surface_try_from_wlr_surface(parent)) {
        if (surface->role != WLR_XDG_SURFACE_ROLE_POPUP || !surface->popup)
            break;
        parent = surface->popup->parent;
    }

    int rootX = 0;
    int rootY = 0;
    if (wlr_xdg_toplevel* toplevel = wlr_xdg_toplevel_try_from_wlr_surface(parent)) {
        // The window's own tree, below any title bar drawn for it.
        auto* tree = static_cast<wlr_scene_tree*>(toplevel->base->data);
        if (!tree)
            return;
        wlr_scene_node_coords(&tree->node, &rootX, &rootY);
    } else if (wlr_layer_surface_v1* layer = wlr_layer_surface_v1_try_from_wlr_surface(parent)) {
        auto* tree = static_cast<wlr_scene_tree*>(layer->data);
        if (!tree)
            return;
        wlr_scene_node_coords(&tree->node, &rootX, &rootY);
    } else {
        return;
    }

    const Output* output = m_server.outputAt(rootX, rootY);
    if (!output)
        return;
    wlr_box box = output->box();
    box.x -= rootX;
    box.y -= rootY;
    wlr_xdg_popup_unconstrain_from_box(popup, &box);
}

// Pointer ---------------------------------------------------------------------------------

Pointer::Pointer(Server& server, wlr_input_device* device)
    : device(device)
    , touchpad(false)
{
    if (wlr_input_device_is_libinput(device)) {
        libinput_device* handle = wlr_libinput_get_device_handle(device);
        touchpad = libinput_device_config_tap_get_finger_count(handle) > 0;
    }
    m_destroy.connect(device->events.destroy, [this, &server] { server.pointerDestroyed(*this); });
}

// LayerSurface ----------------------------------------------------------------------------

LayerSurface::LayerSurface(Server& server, wlr_layer_surface_v1* surface, Output& output)
    : NodeOwner(Kind::Layer)
    , server(server)
    , surface(surface)
    , scene(nullptr)
    , output(&output)
{
    scene = wlr_scene_layer_surface_v1_create(treeForLayer(), surface);
    scene->tree->node.data = static_cast<NodeOwner*>(this);
    surface->data = scene->tree;

    m_map.connect(surface->surface->events.map, [this] {
        mapped = true;
        if (this->output)
            this->server.arrange(*this->output);
    });
    m_unmap.connect(surface->surface->events.unmap, [this] {
        mapped = false;
        // Clicked into for the keyboard, it gives it back to the window in use.
        if (this->server.seat->keyboard_state.focused_surface == this->surface->surface)
            this->server.focusTopmost();
        if (this->output)
            this->server.arrange(*this->output);
    });
    m_commit.connect(surface->surface->events.commit, [this] {
        if (!this->output)
            return;
        const uint32_t committed = this->surface->current.committed;
        if (committed & WLR_LAYER_SURFACE_V1_STATE_LAYER)
            wlr_scene_node_reparent(&scene->tree->node, treeForLayer());
        if (this->surface->initial_commit || committed)
            this->server.arrange(*this->output);
    });
    m_newPopup.connect<wlr_xdg_popup>(
        surface->events.new_popup, [this](wlr_xdg_popup* popup) { this->server.newPopup(popup); });
    m_destroy.connect(surface->events.destroy, [this] { this->server.layerSurfaceDestroyed(*this); });
}

wlr_scene_tree* LayerSurface::treeForLayer() const
{
    switch (surface->pending.layer) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
        return server.layers.background;
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
        return server.layers.bottom;
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
        return server.layers.top;
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
    default:
        return server.layers.overlay;
    }
}

} // namespace atlas
