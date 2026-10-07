#include "View.hpp"

#include "Parts.hpp"
#include "Placement.hpp"

#include <algorithm>
#include <vector>

namespace atlas {

// View ------------------------------------------------------------------------------------

View::View(Server& server)
    : NodeOwner(Kind::View)
    , server(server)
    , m_captureScene(wlr_scene_create())
{
    tree = wlr_scene_tree_create(server.layers.views);
    tree->node.data = static_cast<NodeOwner*>(this);
    m_captureTree = wlr_scene_tree_create(&m_captureScene->tree);
}

View::~View()
{
    unpublish();
    m_decoration.reset();
    wlr_scene_node_destroy(&tree->node);
    wlr_scene_node_destroy(&m_captureScene->tree.node);
}

void View::onMapped()
{
    place();
    mapped = true;
    publish();
    server.viewMapped(*this);
}

void View::onUnmapped()
{
    mapped = false;
    unpublish();
    server.viewUnmapped(*this);
}

void View::place()
{
    // Dialogs over their parent, those another program shows for a window, such as a portal's
    // file picker, over that window, and everything else where it covers no other window, the
    // middle of the screen when that is free.
    wlr_box ownSize = size();
    ownSize.height += decorationHeight();
    wlr_box area = server.usableArea(server.cursor->x, server.cursor->y);
    View* parent = parentView();
    if (!parent)
        parent = server.importedParent(*this);
    if (parent)
        area = parent->geometry();
    int x = area.x + (area.width - ownSize.width) / 2;
    int y = area.y + (area.height - ownSize.height) / 2;
    const wlr_box usable = server.usableArea(area.x + area.width / 2.0, area.y + area.height / 2.0);
    // On the screen even when the parent is partly off it, the top left showing when it is too big.
    x = std::max(std::min(x, usable.x + usable.width - ownSize.width), usable.x);
    y = std::max(std::min(y, usable.y + usable.height - ownSize.height), usable.y);

    if (!parent) {
        // Clear of the other windows on the screen where possible.
        std::vector<Rect> others;
        for (View* other : server.stackingOrder()) {
            if (other == this || !other->mapped || other->minimized)
                continue;
            // Those on this screen; whatever lies beside it does not matter.
            const wlr_box box = other->geometry();
            wlr_box common;
            if (wlr_box_intersection(&common, &box, &usable))
                others.push_back({box.x, box.y, box.width, box.height});
        }
        const Rect placed
            = placeWindow(ownSize.width, ownSize.height, {usable.x, usable.y, usable.width, usable.height}, others);
        x = placed.x;
        y = placed.y;
    }
    moveTo(x, y);
}

// The scene tree starts where the window does; shadows drawn by the window lie outside it.
wlr_box View::geometry() const
{
    const wlr_box ownSize = size();
    return {tree->node.x, tree->node.y, ownSize.width, ownSize.height + decorationHeight()};
}

wlr_box View::contentGeometry() const
{
    const wlr_box ownSize = size();
    return {tree->node.x, tree->node.y + decorationHeight(), ownSize.width, ownSize.height};
}

int View::decorationHeight() const
{
    return m_decoration && !fullscreen ? Decoration::TitleHeight : 0;
}

void View::setDecorated(bool decorated)
{
    if (decorated == bool(m_decoration))
        return;
    const wlr_box content = contentGeometry();
    if (decorated)
        m_decoration = std::make_unique<Decoration>(*this);
    else
        m_decoration.reset();
    updateFrame();
    if (!mapped || fullscreen)
        return;
    // The program's part stays where it is, the title bar coming or going above it.
    if (isTiled())
        setTile(tile);
    else
        moveTo(content.x, content.y - decorationHeight());
}

void View::updateFrame()
{
    const int top = decorationHeight();
    if (m_content)
        wlr_scene_node_set_position(&m_content->node, 0, top);
    if (m_captureContent)
        wlr_scene_node_set_position(&m_captureContent->node, 0, top);
    if (m_decoration) {
        m_decoration->setVisible(!fullscreen);
        m_decoration->update();
    }
}

void View::moveTo(int x, int y)
{
    wlr_scene_node_set_position(&tree->node, x, y);
    moved();
    updateOutput();
}

void View::setGeometry(const wlr_box& box)
{
    wlr_scene_node_set_position(&tree->node, box.x, box.y);
    requestSize(box.width, std::max(1, box.height - decorationHeight()));
    updateOutput();
}

void View::setActivated(bool activated)
{
    this->activated = activated;
    updateFrame();
    sendActivated(activated);
    if (m_foreign)
        wlr_foreign_toplevel_handle_v1_set_activated(m_foreign, activated);
}

void View::setTile(Tile wanted)
{
    if (fullscreen)
        return;
    // Not tiled already: nothing to restore, the program only hears so.
    if (wanted == Tile::None && tile == Tile::None) {
        sendTiled(Tile::None);
        return;
    }
    const wlr_box current = geometry();
    if (wanted != Tile::None && tile == Tile::None)
        restore = current;

    const wlr_box area = server.usableArea(current.x + current.width / 2.0, current.y + current.height / 2.0);
    wlr_box box = area;
    switch (wanted) {
    case Tile::None:
        box = wlr_box_empty(&restore) ? current : restore;
        break;
    case Tile::Maximized:
        break;
    case Tile::Left:
        box.width = area.width / 2;
        break;
    case Tile::Right:
        box.width = area.width / 2;
        box.x += area.width - box.width;
        break;
    }
    tile = wanted;
    sendTiled(wanted);
    setGeometry(box);
    updateFrame();
    if (m_foreign)
        wlr_foreign_toplevel_handle_v1_set_maximized(m_foreign, wanted == Tile::Maximized);
}

void View::setFullscreen(bool wanted)
{
    if (wanted == fullscreen) {
        sendFullscreen(wanted);
        return;
    }
    const wlr_box current = geometry();
    fullscreen = wanted;
    updateFrame();
    sendFullscreen(wanted);
    if (wanted) {
        if (tile == Tile::None)
            restore = current;
        if (Output* output = server.outputAt(current.x + current.width / 2.0, current.y + current.height / 2.0))
            setGeometry(output->box());
        // Above the bars while it covers the screen.
        wlr_scene_node_reparent(&tree->node, server.layers.fullscreen);
    } else {
        wlr_scene_node_reparent(&tree->node, server.layers.views);
        if (tile != Tile::None)
            setTile(tile);
        else
            setGeometry(restore);
    }
    if (m_foreign)
        wlr_foreign_toplevel_handle_v1_set_fullscreen(m_foreign, wanted);
}

void View::setMinimized(bool wanted)
{
    if (wanted == minimized)
        return;
    const bool wasFocused = server.focusedView() == this;
    minimized = wanted;
    wlr_scene_node_set_enabled(&tree->node, !wanted);
    sendMinimized(wanted);
    if (m_foreign)
        wlr_foreign_toplevel_handle_v1_set_minimized(m_foreign, wanted);
    if (wanted && wasFocused) {
        setActivated(false);
        server.focusTopmost();
    }
}

// Lists the window for taskbars and the overview, with handles to switch to it, close it,
// and take pictures of it.
void View::publish()
{
    m_foreign = wlr_foreign_toplevel_handle_v1_create(server.foreignToplevels);
    m_foreign->data = this;
    m_foreignActivate.connect(m_foreign->events.request_activate, [this] { server.focus(this); });
    m_foreignClose.connect(m_foreign->events.request_close, [this] { close(); });
    m_foreignMinimize.connect<wlr_foreign_toplevel_handle_v1_minimized_event>(
        m_foreign->events.request_minimize, [this](wlr_foreign_toplevel_handle_v1_minimized_event* event) {
            if (event->minimized)
                setMinimized(true);
            else
                server.focus(this);
        });
    m_foreignMaximize.connect<wlr_foreign_toplevel_handle_v1_maximized_event>(
        m_foreign->events.request_maximize, [this](wlr_foreign_toplevel_handle_v1_maximized_event* event) {
            setTile(event->maximized ? Tile::Maximized : Tile::None);
        });
    m_foreignFullscreen.connect<wlr_foreign_toplevel_handle_v1_fullscreen_event>(m_foreign->events.request_fullscreen,
        [this](wlr_foreign_toplevel_handle_v1_fullscreen_event* event) { setFullscreen(event->fullscreen); });

    const std::string ownTitle = title();
    const std::string ownAppId = appId();
    const wlr_ext_foreign_toplevel_handle_v1_state state {ownTitle.c_str(), ownAppId.c_str()};
    m_listed = wlr_ext_foreign_toplevel_handle_v1_create(server.toplevelList, &state);
    if (m_listed)
        m_listed->data = this;
    updatePublished();
}

void View::updatePublished()
{
    if (m_decoration)
        m_decoration->update();
    const std::string ownTitle = title();
    const std::string ownAppId = appId();
    if (m_foreign) {
        wlr_foreign_toplevel_handle_v1_set_title(m_foreign, ownTitle.c_str());
        wlr_foreign_toplevel_handle_v1_set_app_id(m_foreign, ownAppId.c_str());
        updateOutput();
    }
    if (m_listed) {
        const wlr_ext_foreign_toplevel_handle_v1_state state {ownTitle.c_str(), ownAppId.c_str()};
        wlr_ext_foreign_toplevel_handle_v1_update_state(m_listed, &state);
    }
}

// The output under the window's middle, for the window list, once the window got there.
void View::updateOutput()
{
    if (!m_foreign)
        return;
    const wlr_box box = geometry();
    const Output* output = server.outputAt(box.x + box.width / 2.0, box.y + box.height / 2.0);
    wlr_output* now = output ? output->output : nullptr;
    if (now == m_foreignOutput)
        return;
    if (m_foreignOutput)
        wlr_foreign_toplevel_handle_v1_output_leave(m_foreign, m_foreignOutput);
    if (now)
        wlr_foreign_toplevel_handle_v1_output_enter(m_foreign, now);
    m_foreignOutput = now;
}

void View::unpublish()
{
    m_foreignActivate.disconnect();
    m_foreignClose.disconnect();
    m_foreignMinimize.disconnect();
    m_foreignMaximize.disconnect();
    m_foreignFullscreen.disconnect();
    if (m_foreign) {
        wlr_foreign_toplevel_handle_v1_destroy(m_foreign);
        m_foreign = nullptr;
        m_foreignOutput = nullptr;
    }
    if (m_listed) {
        wlr_ext_foreign_toplevel_handle_v1_destroy(m_listed);
        m_listed = nullptr;
    }
}

// XdgView ---------------------------------------------------------------------------------

XdgView::XdgView(Server& server, wlr_xdg_toplevel* toplevel)
    : View(server)
    , toplevel(toplevel)
{
    m_content = wlr_scene_xdg_surface_create(tree, toplevel->base);
    // Popups go under the surface they belong to.
    toplevel->base->data = m_content;
    m_captureContent = wlr_scene_xdg_surface_create(m_captureTree, toplevel->base);

    wlr_surface* surface = toplevel->base->surface;
    m_map.connect(surface->events.map, [this] {
        onMapped();
        if (this->toplevel->requested.fullscreen)
            setFullscreen(true);
        else if (this->toplevel->requested.maximized)
            setTile(Tile::Maximized);
    });
    m_unmap.connect(surface->events.unmap, [this] { onUnmapped(); });
    m_commit.connect(surface->events.commit, [this] { commit(); });
    m_destroy.connect(toplevel->events.destroy, [this] { this->server.viewDestroyed(*this); });

    m_requestMove.connect(toplevel->events.request_move, [this] {
        if (mapped)
            this->server.beginMove(*this);
    });
    m_requestResize.connect<wlr_xdg_toplevel_resize_event>(
        toplevel->events.request_resize, [this](wlr_xdg_toplevel_resize_event* event) {
            if (mapped)
                this->server.beginResize(*this, event->edges);
        });
    m_requestMaximize.connect(toplevel->events.request_maximize, [this] {
        if (mapped)
            setTile(this->toplevel->requested.maximized ? Tile::Maximized : Tile::None);
        else if (this->toplevel->base->initialized)
            wlr_xdg_surface_schedule_configure(this->toplevel->base);
    });
    m_requestFullscreen.connect(toplevel->events.request_fullscreen, [this] {
        if (mapped)
            setFullscreen(this->toplevel->requested.fullscreen);
        else if (this->toplevel->base->initialized)
            wlr_xdg_surface_schedule_configure(this->toplevel->base);
    });
    m_requestMinimize.connect(toplevel->events.request_minimize, [this] {
        if (mapped)
            setMinimized(true);
    });
    m_setTitle.connect(toplevel->events.set_title, [this] { updatePublished(); });
    m_setAppId.connect(toplevel->events.set_app_id, [this] { updatePublished(); });
}

void XdgView::commit()
{
    if (toplevel->base->initial_commit) {
        // The window picks its own size, and learns what it may ask for.
        wlr_xdg_toplevel_set_wm_capabilities(toplevel,
            WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE | WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE
                | WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
        wlr_xdg_toplevel_set_size(toplevel, 0, 0);
        applyDecorationMode();
    }
    // Pictures show the window without its shadow.
    wlr_scene_subsurface_tree_set_clip(&m_captureContent->node, &toplevel->base->geometry);
    if (decoration())
        decoration()->update();
}

void XdgView::setDecorationObject(wlr_xdg_toplevel_decoration_v1* object)
{
    m_decorationObject = object;
    m_decorationRequest.connect(object->events.request_mode, [this] { applyDecorationMode(); });
    m_decorationDestroy.connect(object->events.destroy, [this] {
        m_decorationRequest.disconnect();
        m_decorationDestroy.disconnect();
        m_decorationObject = nullptr;
        setDecorated(false);
    });
    applyDecorationMode();
}

void XdgView::applyDecorationMode()
{
    // Programs that draw their own title bar keep it; those that leave it to us, or have no
    // preference, get one. The mode can only be sent once the window is set up.
    if (!m_decorationObject || !toplevel->base->initialized)
        return;
    const bool clientSide = m_decorationObject->requested_mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
    wlr_xdg_toplevel_decoration_v1_set_mode(m_decorationObject,
        clientSide ? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE : WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    setDecorated(!clientSide);
}

std::string XdgView::title() const
{
    return toplevel->title ? toplevel->title : "";
}

std::string XdgView::appId() const
{
    return toplevel->app_id ? toplevel->app_id : "";
}

View* XdgView::parentView() const
{
    if (!toplevel->parent)
        return nullptr;
    for (View* other : server.stackingOrder()) {
        if (auto* xdg = dynamic_cast<XdgView*>(other); xdg && xdg->toplevel == toplevel->parent)
            return other;
    }
    return nullptr;
}

int XdgView::minimumWidth() const
{
    return std::max(toplevel->current.min_width, 1);
}

int XdgView::minimumHeight() const
{
    return std::max(toplevel->current.min_height, 1);
}

void XdgView::close()
{
    wlr_xdg_toplevel_send_close(toplevel);
}

void XdgView::requestSize(int width, int height)
{
    wlr_xdg_toplevel_set_size(toplevel, width, height);
}

void XdgView::sendActivated(bool activated)
{
    wlr_xdg_toplevel_set_activated(toplevel, activated);
}

void XdgView::sendTiled(Tile tile)
{
    uint32_t edges = WLR_EDGE_NONE;
    switch (tile) {
    case Tile::None:
        break;
    case Tile::Maximized:
        edges = WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT;
        break;
    case Tile::Left:
        edges = WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT;
        break;
    case Tile::Right:
        edges = WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT;
        break;
    }
    wlr_xdg_toplevel_set_maximized(toplevel, tile == Tile::Maximized);
    wlr_xdg_toplevel_set_tiled(toplevel, edges);
}

void XdgView::sendFullscreen(bool fullscreen)
{
    wlr_xdg_toplevel_set_fullscreen(toplevel, fullscreen);
    wlr_xdg_surface_schedule_configure(toplevel->base);
}

// XwaylandView ----------------------------------------------------------------------------

XwaylandView::XwaylandView(Server& server, wlr_xwayland_surface* surface)
    : View(server)
    , xsurface(surface)
{
    wlr_scene_node_set_enabled(&tree->node, false);
    m_content = wlr_scene_tree_create(tree);
    m_captureContent = wlr_scene_tree_create(m_captureTree);

    m_destroy.connect(surface->events.destroy, [this] { this->server.viewDestroyed(*this); });
    m_associate.connect(surface->events.associate, [this] { associate(); });
    m_dissociate.connect(surface->events.dissociate, [this] { dissociate(); });

    // Before it is shown, a window may put itself where it likes; afterwards it is placed.
    m_requestConfigure.connect<wlr_xwayland_surface_configure_event>(
        surface->events.request_configure, [this](wlr_xwayland_surface_configure_event* event) {
            if (!mapped || (!isTiled() && !fullscreen)) {
                wlr_xwayland_surface_configure(xsurface, event->x, event->y, event->width, event->height);
                if (mapped) {
                    wlr_scene_node_set_position(&tree->node, event->x, event->y - decorationHeight());
                    updateOutput();
                }
            } else {
                const wlr_box box = contentGeometry();
                wlr_xwayland_surface_configure(xsurface, box.x, box.y, box.width, box.height);
            }
        });
    m_requestMove.connect(surface->events.request_move, [this] {
        if (mapped)
            this->server.beginMove(*this);
    });
    m_requestResize.connect<wlr_xwayland_resize_event>(
        surface->events.request_resize, [this](wlr_xwayland_resize_event* event) {
            if (mapped)
                this->server.beginResize(*this, event->edges);
        });
    m_requestMaximize.connect(surface->events.request_maximize, [this] {
        if (mapped)
            setTile(xsurface->maximized_horz && xsurface->maximized_vert ? Tile::Maximized : Tile::None);
    });
    m_requestFullscreen.connect(surface->events.request_fullscreen, [this] {
        if (mapped)
            setFullscreen(xsurface->fullscreen);
    });
    m_requestMinimize.connect<wlr_xwayland_minimize_event>(
        surface->events.request_minimize, [this](wlr_xwayland_minimize_event* event) {
            if (mapped)
                setMinimized(event->minimize);
        });
    m_requestActivate.connect(surface->events.request_activate, [this] {
        if (mapped)
            this->server.focus(this);
    });
    m_setTitle.connect(surface->events.set_title, [this] { updatePublished(); });
    m_setClass.connect(surface->events.set_class, [this] { updatePublished(); });
    m_setDecorations.connect(surface->events.set_decorations, [this] {
        if (mapped)
            setDecorated(xsurface->decorations == WLR_XWAYLAND_SURFACE_DECORATIONS_ALL);
    });
}

void XwaylandView::associate()
{
    // The X11 window got its Wayland surface; it is shown with it from now on.
    wlr_scene_subsurface_tree_create(m_content, xsurface->surface);
    wlr_scene_subsurface_tree_create(m_captureContent, xsurface->surface);
    m_commit.connect(xsurface->surface->events.commit, [this] {
        if (decoration())
            decoration()->update();
    });
    m_map.connect(xsurface->surface->events.map, [this] {
        // Programs that leave their frame to the window manager, as most X11 ones do, get a
        // title bar; those that draw their own say so in their Motif hints.
        setDecorated(xsurface->decorations == WLR_XWAYLAND_SURFACE_DECORATIONS_ALL);
        wlr_scene_node_set_enabled(&tree->node, true);
        onMapped();
        if (xsurface->fullscreen)
            setFullscreen(true);
        else if (xsurface->maximized_horz && xsurface->maximized_vert)
            setTile(Tile::Maximized);
    });
    m_unmap.connect(xsurface->surface->events.unmap, [this] {
        wlr_scene_node_set_enabled(&tree->node, false);
        onUnmapped();
    });
}

void XwaylandView::dissociate()
{
    // The surface trees go with the surface.
    m_commit.disconnect();
    m_map.disconnect();
    m_unmap.disconnect();
}

std::string XwaylandView::title() const
{
    return xsurface->title ? xsurface->title : "";
}

std::string XwaylandView::appId() const
{
    return xsurface->class_ ? xsurface->class_ : "";
}

View* XwaylandView::parentView() const
{
    if (!xsurface->parent)
        return nullptr;
    for (View* other : server.stackingOrder()) {
        if (auto* x = dynamic_cast<XwaylandView*>(other); x && x->xsurface == xsurface->parent)
            return other;
    }
    return nullptr;
}

int XwaylandView::minimumWidth() const
{
    const xcb_size_hints_t* hints = xsurface->size_hints;
    return hints && (hints->flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) ? std::max(hints->min_width, 1) : 1;
}

int XwaylandView::minimumHeight() const
{
    const xcb_size_hints_t* hints = xsurface->size_hints;
    return hints && (hints->flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) ? std::max(hints->min_height, 1) : 1;
}

void XwaylandView::close()
{
    wlr_xwayland_surface_close(xsurface);
}

void XwaylandView::moved()
{
    // X11 programs place their menus from where they believe their window is.
    const wlr_box content = contentGeometry();
    wlr_xwayland_surface_configure(xsurface, content.x, content.y, xsurface->width, xsurface->height);
}

void XwaylandView::requestSize(int width, int height)
{
    const wlr_box content = contentGeometry();
    wlr_xwayland_surface_configure(xsurface, content.x, content.y, width, height);
}

void XwaylandView::sendActivated(bool activated)
{
    wlr_xwayland_surface_activate(xsurface, activated);
    if (activated)
        wlr_xwayland_surface_restack(xsurface, nullptr, XCB_STACK_MODE_ABOVE);
}

void XwaylandView::sendTiled(Tile tile)
{
    wlr_xwayland_surface_set_maximized(xsurface, tile == Tile::Maximized, tile != Tile::None);
}

void XwaylandView::sendFullscreen(bool fullscreen)
{
    wlr_xwayland_surface_set_fullscreen(xsurface, fullscreen);
}

void XwaylandView::sendMinimized(bool minimized)
{
    wlr_xwayland_surface_set_minimized(xsurface, minimized);
}

// Unmanaged -------------------------------------------------------------------------------

Unmanaged::Unmanaged(Server& server, wlr_xwayland_surface* surface)
    : NodeOwner(Kind::Unmanaged)
    , xsurface(surface)
    , m_server(server)
{
    m_destroy.connect(surface->events.destroy, [this] { m_server.unmanagedDestroyed(*this); });
    m_associate.connect(surface->events.associate, [this] {
        m_map.connect(xsurface->surface->events.map, [this] { map(); });
        m_unmap.connect(xsurface->surface->events.unmap, [this] { unmap(); });
    });
    m_dissociate.connect(surface->events.dissociate, [this] {
        m_map.disconnect();
        m_unmap.disconnect();
    });
    m_requestConfigure.connect<wlr_xwayland_surface_configure_event>(
        surface->events.request_configure, [this](wlr_xwayland_surface_configure_event* event) {
            wlr_xwayland_surface_configure(xsurface, event->x, event->y, event->width, event->height);
        });
    m_setGeometry.connect(surface->events.set_geometry, [this] {
        if (m_tree)
            wlr_scene_node_set_position(&m_tree->node, xsurface->x, xsurface->y);
    });
}

void Unmanaged::map()
{
    m_tree = wlr_scene_tree_create(m_server.layers.unmanaged);
    m_tree->node.data = static_cast<NodeOwner*>(this);
    wlr_scene_subsurface_tree_create(m_tree, xsurface->surface);
    wlr_scene_node_set_position(&m_tree->node, xsurface->x, xsurface->y);
    // Menus take the keyboard, unless the overview or the like holds it.
    if (wlr_xwayland_surface_override_redirect_wants_focus(xsurface) && !m_server.keyboardHeldByLayer())
        m_server.focusSurface(xsurface->surface);
}

void Unmanaged::unmap()
{
    if (m_tree) {
        wlr_scene_node_destroy(&m_tree->node);
        m_tree = nullptr;
    }
    if (m_server.seat->keyboard_state.focused_surface == xsurface->surface)
        m_server.focusTopmost();
}

} // namespace atlas
