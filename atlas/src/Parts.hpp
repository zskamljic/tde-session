#pragma once

#include "Server.hpp"
#include "View.hpp"

#include <string>

namespace atlas {

// A screen.
class Output {
public:
    Output(Server& server, wlr_output* output);

    // Where the output is in the layout.
    wlr_box box() const;

    Server& server;
    wlr_output* output;
    wlr_box usable {}; // the part not taken by panels, in layout coordinates

private:
    Listener m_frame;
    Listener m_requestState;
    Listener m_destroy;
};

// A menu or other popup of a window or layer surface.
class Popup {
public:
    Popup(Server& server, wlr_xdg_popup* popup);

    wlr_xdg_popup* popup;

private:
    void unconstrain();

    Server& m_server;
    Listener m_commit;
    Listener m_destroy;
};

// A surface of wlr-layer-shell: backgrounds, panels, the overview.
class LayerSurface : public NodeOwner {
public:
    LayerSurface(Server& server, wlr_layer_surface_v1* surface, Output& output);

    wlr_scene_tree* treeForLayer() const;

    Server& server;
    wlr_layer_surface_v1* surface;
    wlr_scene_layer_surface_v1* scene;
    Output* output;
    bool mapped = false;

private:
    Listener m_map;
    Listener m_unmap;
    Listener m_commit;
    Listener m_newPopup;
    Listener m_destroy;
};

class Keyboard {
public:
    Keyboard(Server& server, wlr_keyboard* keyboard);

    wlr_keyboard* keyboard;

private:
    Server& m_server;
    Listener m_key;
    Listener m_modifiers;
    Listener m_destroy;
};

} // namespace atlas
