#include "Server.hpp"

#include "Lock.hpp"
#include "Parts.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>

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
    // wlroots wants nobody listening to what it destroys.
    for (Listener* listener : {&m_newOutput, &m_layoutChange, &m_outputManagerApply, &m_outputManagerTest,
             &m_newToplevel, &m_newPopup, &m_newDecoration, &m_newLock, &m_newLayerSurface, &m_newInput,
             &m_newVirtualKeyboard, &m_newVirtualPointer, &m_cursorMotion, &m_cursorMotionAbsolute, &m_cursorButton,
             &m_cursorAxis, &m_cursorFrame, &m_requestCursor, &m_requestCursorShape, &m_requestSelection,
             &m_requestPrimarySelection, &m_requestStartDrag, &m_startDrag, &m_dragIconDestroy, &m_requestActivate,
             &m_newInhibitor, &m_newCaptureSource, &m_outputPowerMode, &m_newXwaylandSurface, &m_xwaylandReady})
        listener->disconnect();
    // The loop's sources go before the loop does.
    for (EventSource* source :
        {&m_layoutIdle, &m_snapPreviewTimer, &m_keyboardWatchSource, &m_settingsWatchSource, &m_relock})
        source->reset();
    wl_display_destroy_clients(display);
    m_popups.clear();
    m_views.clear();
    m_unmanaged.clear();
    if (m_xwayland)
        wlr_xwayland_destroy(m_xwayland);
    layerSurfaces.clear();
    m_keyboards.clear();
    m_pointers.clear();
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
    addVirtualOutputs();
    setUpShells();
    setUpInput();
    setUpProtocols();
    setUpXwayland();
    setUpLock();
    setUpWindowInfo();

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

// Outputs that show on no screen, as many as ATLAS_VIRTUAL_OUTPUTS says: for a session seen
// only through screenshots or screen sharing, or for trying several displays without having
// them.
void Server::addVirtualOutputs()
{
    const char* text = std::getenv("ATLAS_VIRTUAL_OUTPUTS");
    int count = 0;
    if (!text || std::from_chars(text, text + std::strlen(text), count).ec != std::errc {} || count <= 0)
        return;
    wlr_backend* headless = wlr_headless_backend_create(eventLoop);
    if (!headless || !wlr_multi_backend_add(backend, headless)) {
        wlr_log(WLR_ERROR, "cannot make virtual outputs");
        return;
    }
    for (int i = 0; i < std::min(count, 8); ++i)
        wlr_headless_add_output(headless, 1280, 720);
}

void Server::setUpOutputs()
{
    outputLayout = wlr_output_layout_create(display);
    sceneLayout = wlr_scene_attach_output_layout(scene, outputLayout);
    wlr_xdg_output_manager_v1_create(display, outputLayout);

    m_newOutput.connect<wlr_output>(backend->events.new_output, [this](wlr_output* output) { newOutput(output); });
    m_layoutChange.connect(outputLayout->events.change, [this] { layoutChanged(); });

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
    outputs.push_back(std::make_unique<Output>(*this, output));
    turnOn(*outputs.back());
}

// Turns a new output on in the mode it likes best, or else the best one it takes; when none
// does, as when a dock brings up its displays one by one, it is tried again a little later.
void Server::turnOn(Output& output)
{
    constexpr int Attempts = 5;
    constexpr int Delay = 1000; // ms

    // The one it prefers, then as large as can be and as fast.
    wlr_output_mode* preferred = wlr_output_preferred_mode(output.output);
    std::vector<wlr_output_mode*> modes;
    wlr_output_mode* mode;
    wl_list_for_each(mode, &output.output->modes, link)
    {
        if (mode != preferred)
            modes.push_back(mode);
    }
    std::ranges::stable_sort(modes, [](const wlr_output_mode* a, const wlr_output_mode* b) {
        if (a->width * a->height != b->width * b->height)
            return a->width * a->height > b->width * b->height;
        return a->refresh > b->refresh;
    });
    if (preferred)
        modes.insert(modes.begin(), preferred);
    if (modes.empty())
        modes.push_back(nullptr); // nested and virtual outputs have no modes

    bool enabled = false;
    for (wlr_output_mode* candidate : modes) {
        wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, true);
        if (candidate)
            wlr_output_state_set_mode(&state, candidate);
        enabled = wlr_output_test_state(output.output, &state) && wlr_output_commit_state(output.output, &state);
        wlr_output_state_finish(&state);
        if (enabled)
            break;
    }

    if (enabled) {
        output.attempts = 0;
        placeOutput(wlr_output_layout_add_auto(outputLayout, output.output));
    } else if (++output.attempts < Attempts) {
        wlr_log(WLR_INFO, "cannot turn on %s yet, trying again", output.output->name);
        output.retry(Delay);
    } else {
        wlr_log(WLR_ERROR, "cannot turn on %s", output.output->name);
    }
    arrange(output);
}

// Shows an output that is in the layout. Its scene output goes whenever it leaves the layout,
// as when it is turned off, so one is made each time it comes back.
void Server::placeOutput(wlr_output_layout_output* placed, wlr_scene_output* made)
{
    if (!placed || (!made && wlr_scene_get_scene_output(scene, placed->output)))
        return;
    wlr_scene_output_layout_add_output(
        sceneLayout, placed, made ? made : wlr_scene_output_create(scene, placed->output));
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

// Everything the layout changes at once is seen together, once it is done.
void Server::layoutChanged()
{
    if (m_layoutIdle)
        return;
    m_layoutIdle.reset(wl_event_loop_add_idle(
        eventLoop,
        [](void* data) {
            auto* server = static_cast<Server*>(data);
            // An idle source goes by itself once it ran.
            [[maybe_unused]] wl_event_source* ran = server->m_layoutIdle.release();
            server->outputLayoutChanged();
        },
        this));
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

    moveWindowsWithOutputs();
    for (const auto& output : outputs)
        arrange(*output);
    keepWindowsOnScreen();
}

// Windows stay on their output where it moved to, in the same place on it.
void Server::moveWindowsWithOutputs()
{
    for (const auto& view : m_views) {
        if (!view->mapped)
            continue;
        const wlr_box current = view->geometry();
        const double centreX = current.x + current.width / 2.0;
        const double centreY = current.y + current.height / 2.0;
        for (const auto& output : outputs) {
            const wlr_box now = output->box();
            if (wlr_box_empty(&output->placed) || wlr_box_empty(&now)
                || !wlr_box_contains_point(&output->placed, centreX, centreY))
                continue;
            const int dx = now.x - output->placed.x;
            const int dy = now.y - output->placed.y;
            if (dx != 0 || dy != 0) {
                view->moveTo(current.x + dx, current.y + dy);
                if (!wlr_box_empty(&view->restore)) {
                    view->restore.x += dx;
                    view->restore.y += dy;
                }
            }
            break;
        }
    }
    for (const auto& output : outputs)
        output->placed = output->box();
}

// A box as it is when it shows on some output, or else moved into the nearest one, as large
// as it was as far as it fits.
wlr_box Server::onScreen(const wlr_box& box) const
{
    if (wlr_box_empty(&box) || wlr_output_layout_intersects(outputLayout, nullptr, &box))
        return box;
    const wlr_box area = usableArea(box.x + box.width / 2.0, box.y + box.height / 2.0);
    if (wlr_box_empty(&area))
        return box;
    wlr_box moved = box;
    moved.width = std::min(box.width, area.width);
    moved.height = std::min(box.height, area.height);
    moved.x = std::clamp(box.x, area.x, area.x + area.width - moved.width);
    moved.y = std::clamp(box.y, area.y, area.y + area.height - moved.height);
    return moved;
}

// After outputs went away or moved, no window is left where nothing shows it, and windows
// covering their output cover it as it is now.
void Server::keepWindowsOnScreen()
{
    // With every output off, there is nowhere to keep them; they stay where they were.
    if (wl_list_empty(&outputLayout->outputs))
        return;
    for (const auto& view : m_views) {
        if (!view->mapped)
            continue;
        view->restore = onScreen(view->restore);
        const wlr_box current = view->geometry();
        if (view->fullscreen) {
            if (const Output* output = outputAt(current.x + current.width / 2.0, current.y + current.height / 2.0))
                view->setGeometry(output->box());
        } else if (!view->isTiled()) {
            if (const wlr_box moved = onScreen(current); moved.x != current.x || moved.y != current.y
                || moved.width != current.width || moved.height != current.height)
                view->setGeometry(moved);
        }
    }
}

void Server::applyOutputConfiguration(wlr_output_configuration_v1* config, bool testOnly)
{
    // All outputs at once, each with a frame of the size it is to have: either the whole
    // configuration takes or none of it does.
    std::vector<wlr_backend_output_state> states;
    wlr_output_configuration_head_v1* head;
    wl_list_for_each(head, &config->heads, link)
    {
        wlr_backend_output_state& state = states.emplace_back(wlr_backend_output_state {.output = head->state.output});
        wlr_output_state_init(&state.base);
        wlr_output_head_v1_state_apply(&head->state, &state.base);
    }

    wlr_output_swapchain_manager swapchains;
    wlr_output_swapchain_manager_init(&swapchains, backend);
    // Outputs turned on are drawn by scene outputs made for them now, placed once they are on.
    std::vector<std::pair<wlr_output*, wlr_scene_output*>> made;
    bool ok = wlr_output_swapchain_manager_prepare(&swapchains, states.data(), states.size());
    for (wlr_backend_output_state& state : states) {
        const bool on = state.base.committed & WLR_OUTPUT_STATE_ENABLED ? state.base.enabled : state.output->enabled;
        if (!ok || !on)
            continue;
        wlr_scene_output* sceneOutput = wlr_scene_get_scene_output(scene, state.output);
        if (!sceneOutput)
            sceneOutput = made.emplace_back(state.output, wlr_scene_output_create(scene, state.output)).second;
        const wlr_scene_output_state_options options {
            .swapchain = wlr_output_swapchain_manager_get_swapchain(&swapchains, state.output)};
        ok = wlr_scene_output_build_state(sceneOutput, &state.base, &options);
    }
    if (ok)
        ok = testOnly ? wlr_backend_test(backend, states.data(), states.size())
                      : wlr_backend_commit(backend, states.data(), states.size());
    if (ok && !testOnly)
        wlr_output_swapchain_manager_apply(&swapchains);
    wlr_output_swapchain_manager_finish(&swapchains);
    for (wlr_backend_output_state& state : states)
        wlr_output_state_finish(&state.base);

    if (ok && !testOnly) {
        wl_list_for_each(head, &config->heads, link)
        {
            if (!head->state.enabled) {
                wlr_output_layout_remove(outputLayout, head->state.output);
                continue;
            }
            const auto fresh
                = std::ranges::find(made, head->state.output, &std::pair<wlr_output*, wlr_scene_output*>::first);
            placeOutput(wlr_output_layout_add(outputLayout, head->state.output, head->state.x, head->state.y),
                fresh == made.end() ? nullptr : fresh->second);
            if (fresh != made.end())
                made.erase(fresh);
        }
    }
    // Made for a configuration that did not take, or only tried.
    for (const auto& [output, sceneOutput] : made)
        wlr_scene_output_destroy(sceneOutput);
    if (ok)
        wlr_output_configuration_v1_send_succeeded(config);
    else
        wlr_output_configuration_v1_send_failed(config);
    wlr_output_configuration_v1_destroy(config);
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

void Server::moveToOutput(View& view, wlr_output& output)
{
    const auto target = std::ranges::find(outputs, &output, [](const auto& o) { return o->output; });
    if (target == outputs.end())
        return;
    const wlr_box current = view.geometry();
    const wlr_box targetBox = (*target)->box();
    const Output* source = outputAt(current.x + current.width / 2.0, current.y + current.height / 2.0);
    if (source == target->get() || wlr_box_empty(&targetBox))
        return;
    const wlr_box from = usableArea(current.x + current.width / 2.0, current.y + current.height / 2.0);
    const wlr_box to = wlr_box_empty(&(*target)->usable) ? targetBox : (*target)->usable;
    // The same place relative to the area, as large as it was as far as it fits.
    const auto relocate = [&](const wlr_box& box) {
        wlr_box moved = box;
        moved.width = std::min(box.width, to.width);
        moved.height = std::min(box.height, to.height);
        const double fractionX = from.width > box.width ? double(box.x - from.x) / (from.width - box.width) : 0.5;
        const double fractionY = from.height > box.height ? double(box.y - from.y) / (from.height - box.height) : 0.5;
        moved.x = to.x + int(std::lround(std::clamp(fractionX, 0.0, 1.0) * (to.width - moved.width)));
        moved.y = to.y + int(std::lround(std::clamp(fractionY, 0.0, 1.0) * (to.height - moved.height)));
        return moved;
    };
    if (!wlr_box_empty(&view.restore))
        view.restore = relocate(view.restore);
    if (view.fullscreen) {
        view.setGeometry(targetBox);
    } else if (view.isTiled()) {
        // Tiled along the new output's area, which the tile is worked out from.
        view.moveTo(to.x + (to.width - current.width) / 2, to.y + (to.height - current.height) / 2);
        view.setTile(view.tile);
    } else {
        view.setGeometry(relocate(current));
    }
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

View* Server::importedParent(const View& view) const
{
    if (!view.surface())
        return nullptr;
    // The latest imports come first.
    const wl_client* client = wl_resource_get_client(view.surface()->resource);
    const wlr_xdg_toplevel* parent = nullptr;
    wlr_xdg_imported_v2* imported;
    wl_list_for_each(imported, &foreignV2->importer.objects, link)
    {
        if (imported->exported && wl_resource_get_client(imported->resource) == client) {
            parent = imported->exported->toplevel;
            break;
        }
    }
    if (!parent) {
        wlr_xdg_imported_v1* importedV1;
        wl_list_for_each(importedV1, &foreignV1->importer.objects, link)
        {
            if (importedV1->exported && wl_resource_get_client(importedV1->resource) == client) {
                parent = importedV1->exported->toplevel;
                break;
            }
        }
    }
    for (View* other : m_order) {
        if (auto* xdg = dynamic_cast<XdgView*>(other); xdg && xdg != &view && xdg->toplevel == parent)
            return other;
    }
    return nullptr;
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
    std::erase(m_order, view);
    m_order.insert(m_order.begin(), view);
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

    // A program's dialog shown by another, as the portals show theirs, sits on its window.
    wlr_xdg_foreign_registry* foreign = wlr_xdg_foreign_registry_create(display);
    foreignV1 = wlr_xdg_foreign_v1_create(display, foreign);
    foreignV2 = wlr_xdg_foreign_v2_create(display, foreign);

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

    // Displays turned off and on again, as the bar does after a while without input. They keep
    // their place in the layout, so nothing moves; one turned off in the settings stays off.
    auto* power = wlr_output_power_manager_v1_create(display);
    m_outputPowerMode.connect<wlr_output_power_v1_set_mode_event>(
        power->events.set_mode, [this](wlr_output_power_v1_set_mode_event* event) {
            const bool on = event->mode == ZWLR_OUTPUT_POWER_V1_MODE_ON;
            if (on && !wlr_output_layout_get(outputLayout, event->output))
                return;
            wlr_output_state state;
            wlr_output_state_init(&state);
            wlr_output_state_set_enabled(&state, on);
            if (!wlr_output_commit_state(event->output, &state))
                wlr_log(WLR_ERROR, "cannot turn %s %s", event->output->name, on ? "on" : "off");
            wlr_output_state_finish(&state);
        });

    // Pictures of screens and windows, for the overview and screenshots.
    wlr_ext_image_copy_capture_manager_v1_create(display, 1);
    wlr_ext_output_image_capture_source_manager_v1_create(display, 1);
    auto* sources = wlr_ext_foreign_toplevel_image_capture_source_manager_v1_create(display, 1);
    m_newCaptureSource.connect<wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request>(
        sources->events.new_request, [this](wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request* request) {
            auto* view = static_cast<View*>(request->toplevel_handle->data);
            if (!view)
                return;
            wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request_accept(request, view->captureSource());
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
