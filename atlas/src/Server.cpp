#include "Server.hpp"

#include "Lock.hpp"
#include "Parts.hpp"

#include <algorithm>
#include <cstdlib>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace atlas {

Server::Server()
{
    display = wl_display_create();
    eventLoop = wl_display_get_event_loop(display);
}

Server::~Server()
{
    if (!display)
        return;
    wl_display_destroy_clients(display);
    m_popups.clear();
    m_views.clear();
    m_unmanaged.clear();
    if (m_xwayland)
        wlr_xwayland_destroy(m_xwayland);
    layerSurfaces.clear();
    m_keyboards.clear();
    if (scene)
        wlr_scene_node_destroy(&scene->tree.node);
    if (xcursor)
        wlr_xcursor_manager_destroy(xcursor);
    if (cursor)
        wlr_cursor_destroy(cursor);
    outputs.clear();
    if (allocator)
        wlr_allocator_destroy(allocator);
    if (renderer)
        wlr_renderer_destroy(renderer);
    if (backend)
        wlr_backend_destroy(backend);
    wl_display_destroy(display);
}

bool Server::start()
{
    backend = wlr_backend_autocreate(eventLoop, &session);
    if (!backend) {
        wlr_log(WLR_ERROR, "no backend to run on");
        return false;
    }
    renderer = wlr_renderer_autocreate(backend);
    if (!renderer) {
        wlr_log(WLR_ERROR, "no renderer");
        return false;
    }
    wlr_renderer_init_wl_display(renderer, display);
    allocator = wlr_allocator_autocreate(backend, renderer);
    if (!allocator) {
        wlr_log(WLR_ERROR, "no allocator");
        return false;
    }

    compositor = wlr_compositor_create(display, 6, renderer);
    wlr_subcompositor_create(display);
    wlr_data_device_manager_create(display);

    scene = wlr_scene_create();
    layers.background = wlr_scene_tree_create(&scene->tree);
    layers.bottom = wlr_scene_tree_create(&scene->tree);
    layers.views = wlr_scene_tree_create(&scene->tree);
    layers.unmanaged = wlr_scene_tree_create(&scene->tree);
    layers.top = wlr_scene_tree_create(&scene->tree);
    layers.fullscreen = wlr_scene_tree_create(&scene->tree);
    layers.overlay = wlr_scene_tree_create(&scene->tree);
    layers.feedback = wlr_scene_tree_create(&scene->tree);
    layers.lock = wlr_scene_tree_create(&scene->tree);

    setUpOutputs();
    setUpShells();
    setUpInput();
    setUpProtocols();
    setUpXwayland();
    setUpLock();

    const char* socket = wl_display_add_socket_auto(display);
    if (!socket) {
        wlr_log(WLR_ERROR, "cannot open a Wayland socket");
        return false;
    }
    m_socket = socket;
    if (!wlr_backend_start(backend)) {
        wlr_log(WLR_ERROR, "cannot start the backend");
        return false;
    }
    setenv("WAYLAND_DISPLAY", socket, true);
    return true;
}

void Server::run()
{
    wl_display_run(display);
}

void Server::terminate()
{
    wl_display_terminate(display);
}

void Server::spawn(const std::string& command, const std::string& argument)
{
    // Fork twice, so the program is not the compositor's child and nobody needs to wait for it.
    const pid_t child = fork();
    if (child == 0) {
        setsid();
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        if (fork() == 0) {
            if (argument.empty())
                execl("/bin/sh", "/bin/sh", "-c", command.c_str(), nullptr);
            else
                execl("/bin/sh", "/bin/sh", "-c", command.c_str(), "sh", argument.c_str(), nullptr);
            _exit(127);
        }
        _exit(0);
    }
    if (child > 0)
        waitpid(child, nullptr, 0);
}

// Outputs -------------------------------------------------------------------------------

void Server::setUpOutputs()
{
    outputLayout = wlr_output_layout_create(display);
    sceneLayout = wlr_scene_attach_output_layout(scene, outputLayout);
    wlr_xdg_output_manager_v1_create(display, outputLayout);

    m_newOutput.connect<wlr_output>(backend->events.new_output, [this](wlr_output* output) { newOutput(output); });
    m_layoutChange.connect(outputLayout->events.change, [this] { outputLayoutChanged(); });

    // Lets tools such as wlr-randr change modes, scale and positions.
    m_outputManager = wlr_output_manager_v1_create(display);
    m_outputManagerApply.connect<wlr_output_configuration_v1>(m_outputManager->events.apply,
        [this](wlr_output_configuration_v1* config) { applyOutputConfiguration(config, false); });
    m_outputManagerTest.connect<wlr_output_configuration_v1>(m_outputManager->events.test,
        [this](wlr_output_configuration_v1* config) { applyOutputConfiguration(config, true); });
}

void Server::newOutput(wlr_output* output)
{
    if (!wlr_output_init_render(output, allocator, renderer)) {
        wlr_log(WLR_ERROR, "cannot render to %s", output->name);
        return;
    }

    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    if (wlr_output_mode* mode = wlr_output_preferred_mode(output))
        wlr_output_state_set_mode(&state, mode);
    const bool enabled = wlr_output_commit_state(output, &state);
    wlr_output_state_finish(&state);
    if (!enabled)
        wlr_log(WLR_ERROR, "cannot turn on %s", output->name);

    outputs.push_back(std::make_unique<Output>(*this, output));
    if (enabled)
        placeOutput(wlr_output_layout_add_auto(outputLayout, output));
    arrange(*outputs.back());
}

// Shows an output that is in the layout. Its scene output goes whenever it leaves the layout,
// as when it is turned off, so one is made each time it comes back.
void Server::placeOutput(wlr_output_layout_output* placed)
{
    if (!placed || wlr_scene_get_scene_output(scene, placed->output))
        return;
    wlr_scene_output* sceneOutput = wlr_scene_output_create(scene, placed->output);
    wlr_scene_output_layout_add_output(sceneLayout, placed, sceneOutput);
}

void Server::outputDestroyed(Output& output)
{
    // Its bars and backgrounds are closed: their programs make new ones where they want them.
    std::vector<wlr_layer_surface_v1*> orphans;
    for (const auto& surface : layerSurfaces) {
        if (surface->output == &output) {
            surface->output = nullptr;
            orphans.push_back(surface->surface);
        }
    }
    for (wlr_layer_surface_v1* surface : orphans)
        wlr_layer_surface_v1_destroy(surface);
    for (const auto& view : m_views)
        view->forgetOutput(output.output);
    std::erase_if(outputs, [&](const auto& o) { return o.get() == &output; });
}

void Server::outputLayoutChanged()
{
    coverOutputs();
    if (m_lock)
        m_lock->arrange();
    wlr_output_configuration_v1* config = wlr_output_configuration_v1_create();
    for (const auto& output : outputs) {
        wlr_output_configuration_head_v1* head = wlr_output_configuration_head_v1_create(config, output->output);
        wlr_box box;
        wlr_output_layout_get_box(outputLayout, output->output, &box);
        head->state.enabled = !wlr_box_empty(&box);
        head->state.x = box.x;
        head->state.y = box.y;
    }
    wlr_output_manager_v1_set_configuration(m_outputManager, config);

    for (const auto& output : outputs)
        arrange(*output);
}

void Server::applyOutputConfiguration(wlr_output_configuration_v1* config, bool testOnly)
{
    // All outputs at once: either the whole configuration takes or none of it does.
    std::vector<wlr_backend_output_state> states;
    wlr_output_configuration_head_v1* head;
    wl_list_for_each(head, &config->heads, link)
    {
        wlr_backend_output_state state {};
        state.output = head->state.output;
        wlr_output_state_init(&state.base);
        wlr_output_head_v1_state_apply(&head->state, &state.base);
        states.push_back(state);
    }
    const bool ok = testOnly ? wlr_backend_test(backend, states.data(), states.size())
                             : wlr_backend_commit(backend, states.data(), states.size());
    for (wlr_backend_output_state& state : states)
        wlr_output_state_finish(&state.base);

    if (ok && !testOnly) {
        wl_list_for_each(head, &config->heads, link)
        {
            if (head->state.enabled)
                placeOutput(wlr_output_layout_add(outputLayout, head->state.output, head->state.x, head->state.y));
            else
                wlr_output_layout_remove(outputLayout, head->state.output);
        }
    }
    if (ok)
        wlr_output_configuration_v1_send_succeeded(config);
    else
        wlr_output_configuration_v1_send_failed(config);
    wlr_output_configuration_v1_destroy(config);
    if (!testOnly)
        outputLayoutChanged();
}

Output* Server::outputAt(double x, double y) const
{
    wlr_output* found = wlr_output_layout_output_at(outputLayout, x, y);
    if (!found) {
        double closestX = 0;
        double closestY = 0;
        wlr_output_layout_closest_point(outputLayout, nullptr, x, y, &closestX, &closestY);
        found = wlr_output_layout_output_at(outputLayout, closestX, closestY);
    }
    for (const auto& output : outputs) {
        if (output->output == found)
            return output.get();
    }
    return outputs.empty() ? nullptr : outputs.front().get();
}

wlr_box Server::usableArea(double x, double y) const
{
    const Output* output = outputAt(x, y);
    if (!output)
        return {0, 0, 1280, 720};
    return wlr_box_empty(&output->usable) ? output->box() : output->usable;
}

// Layers ----------------------------------------------------------------------------------

void Server::arrange(Output& output)
{
    const wlr_box full = output.box();
    wlr_box usable = full;

    // Surfaces that reserve an edge go first, so the rest fit between them.
    const auto place = [&](bool exclusive) {
        for (auto layer : {ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, ZWLR_LAYER_SHELL_V1_LAYER_TOP,
                 ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND}) {
            for (const auto& surface : layerSurfaces) {
                if (surface->output != &output || !surface->surface->initialized
                    || surface->surface->current.layer != layer)
                    continue;
                if ((surface->surface->current.exclusive_zone > 0) == exclusive)
                    wlr_scene_layer_surface_v1_configure(surface->scene, &full, &usable);
            }
        }
    };
    place(true);
    place(false);
    output.usable = usable;

    for (const auto& view : m_views) {
        if (!view->mapped || view->minimized || !view->isTiled())
            continue;
        const wlr_box geometry = view->geometry();
        if (outputAt(geometry.x + geometry.width / 2.0, geometry.y + geometry.height / 2.0) == &output)
            view->setTile(view->tile);
    }
    updateLayerFocus();
}

void Server::layerSurfaceDestroyed(LayerSurface& surface)
{
    const bool hadKeyboard = m_layerFocus == &surface;
    if (hadKeyboard)
        m_layerFocus = nullptr;
    Output* output = surface.output;
    std::erase_if(layerSurfaces, [&](const auto& s) { return s.get() == &surface; });
    if (output)
        arrange(*output);
    // Gone with its output, nothing was arranged: the keyboard goes back all the same.
    if (hadKeyboard)
        updateLayerFocus();
}

void Server::inhibitorDestroyed(wlr_idle_inhibitor_v1* inhibitor)
{
    std::erase_if(m_inhibitors, [inhibitor](const auto& i) { return i->inhibitor == inhibitor; });
    wlr_idle_notifier_v1_set_inhibited(idleNotifier, !m_inhibitors.empty());
}

void Server::updateLayerFocus()
{
    // The topmost layer surface that asks for the keyboard gets it, above any window.
    LayerSurface* wanted = nullptr;
    for (auto layer : {ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, ZWLR_LAYER_SHELL_V1_LAYER_TOP}) {
        for (const auto& surface : layerSurfaces) {
            if (surface->mapped && surface->surface->current.layer == layer
                && surface->surface->current.keyboard_interactive
                    == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
                wanted = surface.get();
                break;
            }
        }
        if (wanted)
            break;
    }
    if (wanted == m_layerFocus)
        return;
    m_layerFocus = wanted;
    if (wanted)
        focusSurface(wanted->surface->surface);
    else
        focusTopmost();
}

// Windows ---------------------------------------------------------------------------------

void Server::setUpShells()
{
    m_xdgShell = wlr_xdg_shell_create(display, 6);
    m_newToplevel.connect<wlr_xdg_toplevel>(m_xdgShell->events.new_toplevel,
        [this](wlr_xdg_toplevel* toplevel) { m_views.push_back(std::make_unique<XdgView>(*this, toplevel)); });
    m_newPopup.connect<wlr_xdg_popup>(m_xdgShell->events.new_popup, [this](wlr_xdg_popup* popup) { newPopup(popup); });

    // Windows may leave their title bar to the compositor.
    auto* decorations = wlr_xdg_decoration_manager_v1_create(display);
    m_newDecoration.connect<wlr_xdg_toplevel_decoration_v1>(
        decorations->events.new_toplevel_decoration, [this](wlr_xdg_toplevel_decoration_v1* decoration) {
            for (const auto& view : m_views) {
                if (auto* xdg = dynamic_cast<XdgView*>(view.get()); xdg && xdg->toplevel == decoration->toplevel) {
                    xdg->setDecorationObject(decoration);
                    return;
                }
            }
        });

    m_layerShell = wlr_layer_shell_v1_create(display, 4);
    m_newLayerSurface.connect<wlr_layer_surface_v1>(
        m_layerShell->events.new_surface, [this](wlr_layer_surface_v1* surface) {
            if (!surface->output) {
                Output* output = outputAt(cursor->x, cursor->y);
                if (!output) {
                    wlr_layer_surface_v1_destroy(surface);
                    return;
                }
                surface->output = output->output;
            }
            for (const auto& output : outputs) {
                if (output->output == surface->output) {
                    layerSurfaces.push_back(std::make_unique<LayerSurface>(*this, surface, *output));
                    return;
                }
            }
            wlr_layer_surface_v1_destroy(surface);
        });

    foreignToplevels = wlr_foreign_toplevel_manager_v1_create(display);
    toplevelList = wlr_ext_foreign_toplevel_list_v1_create(display, 1);
}

void Server::newPopup(wlr_xdg_popup* popup)
{
    // Popups of layer surfaces come without a parent, which they get a moment later; the
    // layer surface announces them again then.
    if (!popup->parent)
        return;
    // The popup's scene tree goes under its parent's, which every surface keeps in its data.
    wlr_scene_tree* parentTree = nullptr;
    if (wlr_xdg_surface* parent = wlr_xdg_surface_try_from_wlr_surface(popup->parent))
        parentTree = static_cast<wlr_scene_tree*>(parent->data);
    else if (wlr_layer_surface_v1* layer = wlr_layer_surface_v1_try_from_wlr_surface(popup->parent))
        parentTree = static_cast<wlr_scene_tree*>(layer->data);
    if (!parentTree)
        return;
    popup->base->data = wlr_scene_xdg_surface_create(parentTree, popup->base);
    m_popups.push_back(std::make_unique<Popup>(*this, popup));
}

void Server::popupDestroyed(Popup& popup)
{
    std::erase_if(m_popups, [&](const auto& p) { return p.get() == &popup; });
}

View* Server::focusedView() const
{
    wlr_surface* surface = seat->keyboard_state.focused_surface;
    for (View* view : m_order) {
        if (view->surface() == surface)
            return view;
    }
    return m_layerFocus && !m_order.empty() && !m_order.front()->minimized ? m_order.front() : nullptr;
}

void Server::focus(View* view)
{
    if (!view || !view->mapped)
        return;
    if (view->minimized)
        view->setMinimized(false);

    // A window covering the screen covers it only while it is the one in use.
    for (View* other : m_order) {
        if (other != view && other->fullscreen)
            wlr_scene_node_reparent(&other->tree->node, layers.views);
    }
    wlr_scene_node_reparent(&view->tree->node, view->fullscreen ? layers.fullscreen : layers.views);
    wlr_scene_node_raise_to_top(&view->tree->node);
    if (!m_cycling) {
        std::erase(m_order, view);
        m_order.insert(m_order.begin(), view);
    }
    for (View* other : m_order) {
        if (other != view)
            other->setActivated(false);
    }
    view->setActivated(true);

    if (m_layerFocus)
        return; // the window gets the keyboard back when the layer surface lets go
    focusSurface(view->surface());
}

void Server::focusSurface(wlr_surface* surface)
{
    if (!surface || seat->keyboard_state.focused_surface == surface)
        return;
    // While locked, the keyboard is the lock screen's alone.
    if (m_locked && !(m_lock && m_lock->owns(surface)))
        return;
    if (wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat))
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    else
        wlr_seat_keyboard_notify_enter(seat, surface, nullptr, 0, nullptr);
}

void Server::focusTopmost()
{
    for (View* view : m_order) {
        if (!view->minimized) {
            focus(view);
            return;
        }
    }
    if (!m_layerFocus)
        wlr_seat_keyboard_clear_focus(seat);
}

void Server::viewMapped(View& view)
{
    m_order.insert(m_order.begin(), &view);
    focus(&view);
}

void Server::viewUnmapped(View& view)
{
    if (m_grabbed == &view) {
        m_snapTarget = Tile::None; // a window going away is not tiled on the way
        finishGrab();
    }
    forgetPointerTargets(view);
    const bool wasFocused = !m_order.empty() && m_order.front() == &view;
    std::erase(m_order, &view);
    std::erase(m_cycle, &view);
    if (m_cycleIndex >= m_cycle.size())
        m_cycleIndex = 0;
    if (wasFocused || seat->keyboard_state.focused_surface == view.surface())
        focusTopmost();
}

// A window going away is no longer under the pointer, nor held by it.
void Server::forgetPointerTargets(const View& view)
{
    for (View** target : {&m_decorationHover, &m_decorationPress, &m_titlePress}) {
        if (*target == &view)
            *target = nullptr;
    }
    if (m_titleClickView == &view)
        m_titleClickView = nullptr;
}

void Server::viewDestroyed(View& view)
{
    // Unmapped before, as wlroots does it; but nothing may point to it once it is gone.
    forgetPointerTargets(view);
    std::erase(m_order, &view);
    std::erase(m_cycle, &view);
    if (m_cycleIndex >= m_cycle.size())
        m_cycleIndex = 0;
    if (m_grabbed == &view) {
        m_grabbed = nullptr;
        m_cursorMode = CursorMode::Passthrough;
    }
    std::erase_if(m_views, [&](const auto& v) { return v.get() == &view; });
}

// Other protocols -------------------------------------------------------------------------

void Server::setUpProtocols()
{
    wlr_viewporter_create(display);
    wlr_presentation_create(display, backend, 2);
    wlr_single_pixel_buffer_manager_v1_create(display);
    wlr_fractional_scale_manager_v1_create(display, 1);
    wlr_screencopy_manager_v1_create(display);
    wlr_export_dmabuf_manager_v1_create(display);
    wlr_data_control_manager_v1_create(display);
    wlr_ext_data_control_manager_v1_create(display, 1);
    wlr_primary_selection_v1_device_manager_create(display);

    idleNotifier = wlr_idle_notifier_v1_create(display);
    m_idleInhibit = wlr_idle_inhibit_v1_create(display);
    // Programs playing a video keep the screen on while they ask, and not a moment longer.
    m_newInhibitor.connect<wlr_idle_inhibitor_v1>(
        m_idleInhibit->events.new_inhibitor, [this](wlr_idle_inhibitor_v1* inhibitor) {
            auto& entry = m_inhibitors.emplace_back(std::make_unique<Inhibitor>());
            entry->inhibitor = inhibitor;
            // The listener goes with the entry, so the work is done by the server, from copies.
            entry->destroy.connect(inhibitor->events.destroy, [this, inhibitor] { inhibitorDestroyed(inhibitor); });
            wlr_idle_notifier_v1_set_inhibited(idleNotifier, true);
        });

    // Programs that were started from another one, such as a link opened from a terminal,
    // come to the front.
    wlr_xdg_activation_v1* activation = wlr_xdg_activation_v1_create(display);
    m_requestActivate.connect<wlr_xdg_activation_v1_request_activate_event>(
        activation->events.request_activate, [this](wlr_xdg_activation_v1_request_activate_event* event) {
            for (View* view : std::vector<View*>(m_order)) {
                if (view->surface() == event->surface)
                    focus(view);
            }
        });

    // Pictures of windows, for the overview.
    wlr_ext_image_copy_capture_manager_v1_create(display, 1);
    wlr_ext_output_image_capture_source_manager_v1_create(display, 1);
    auto* sources = wlr_ext_foreign_toplevel_image_capture_source_manager_v1_create(display, 1);
    m_newCaptureSource.connect<wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request>(
        sources->events.new_request, [this](wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request* request) {
            auto* view = static_cast<View*>(request->toplevel_handle->data);
            if (!view)
                return;
            wlr_ext_image_capture_source_v1* source = wlr_ext_image_capture_source_v1_create_with_scene_node(
                view->captureNode(), eventLoop, allocator, renderer);
            if (source)
                wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request_accept(request, source);
        });
}

} // namespace atlas

namespace atlas {

// X11 programs ----------------------------------------------------------------------------

void Server::setUpXwayland()
{
    // Started when the first X11 program connects, so sessions without one never run it.
    m_xwayland = wlr_xwayland_create(display, compositor, true);
    if (!m_xwayland) {
        wlr_log(WLR_INFO, "no Xwayland, X11 programs will not run");
        return;
    }
    setenv("DISPLAY", m_xwayland->display_name, true);

    m_xwaylandReady.connect(m_xwayland->events.ready, [this] {
        wlr_xwayland_set_seat(m_xwayland, seat);
        if (wlr_xcursor* cursorImage = wlr_xcursor_manager_get_xcursor(xcursor, "default", 1)) {
            wlr_xcursor_image* image = cursorImage->images[0];
            wlr_xwayland_set_cursor(
                m_xwayland, wlr_xcursor_image_get_buffer(image), int32_t(image->hotspot_x), int32_t(image->hotspot_y));
        }
    });
    m_newXwaylandSurface.connect<wlr_xwayland_surface>(
        m_xwayland->events.new_surface, [this](wlr_xwayland_surface* surface) {
            if (surface->override_redirect)
                m_unmanaged.push_back(std::make_unique<Unmanaged>(*this, surface));
            else
                m_views.push_back(std::make_unique<XwaylandView>(*this, surface));
        });
}

void Server::unmanagedDestroyed(Unmanaged& surface)
{
    std::erase_if(m_unmanaged, [&](const auto& s) { return s.get() == &surface; });
}

} // namespace atlas
