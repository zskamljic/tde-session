#pragma once

#include "Decoration.hpp"
#include "Server.hpp"

#include <memory>
#include <string>

namespace atlas {

// An application window, of a Wayland program or an X11 one. This part decides where windows
// go and what state they are in; the kinds of windows below tell their program about it.
class View : public NodeOwner {
public:
    explicit View(Server& server);
    ~View() override;

    View(const View&) = delete;
    View& operator=(const View&) = delete;

    virtual wlr_surface* surface() const = 0;
    virtual std::string title() const = 0;
    virtual std::string appId() const = 0;
    // The window this one is a dialog of, if any.
    virtual View* parentView() const = 0;
    virtual int minimumWidth() const { return 1; }
    virtual int minimumHeight() const { return 1; }
    virtual void close() = 0;

    // The window without its shadow, and with the title bar drawn for it, in layout
    // coordinates; what is placed, tiled and moved.
    wlr_box geometry() const;
    // The program's own part of the window, below any title bar drawn for it.
    wlr_box contentGeometry() const;
    void moveTo(int x, int y);
    // Asks the window to take this geometry; it moves at once and resizes when it can.
    void setGeometry(const wlr_box& box);

    void setActivated(bool activated);
    void setTile(Tile tile);
    void setFullscreen(bool fullscreen);
    void setMinimized(bool minimized);

    bool isTiled() const { return tile != Tile::None; }

    // Whether the compositor draws the title bar, for programs that do not.
    void setDecorated(bool decorated);
    Decoration* decoration() const { return m_decoration.get(); }
    int decorationHeight() const;
    // Lays out and draws the title bar again after the window changed.
    void updateFrame();
    // The output is going away; the window is no longer said to be on it.
    void forgetOutput(wlr_output* output)
    {
        if (m_foreignOutput == output)
            m_foreignOutput = nullptr;
    }

    // The window alone, to take pictures of: the main scene would show whatever overlaps it.
    wlr_scene_node* captureNode() const { return &m_captureTree->node; }
    wlr_scene_tree* captureTree() const { return m_captureTree; }

    Server& server;
    wlr_scene_tree* tree = nullptr; // where the window is, title bar and all
    bool mapped = false;
    bool activated = false;
    bool minimized = false;
    bool fullscreen = false;
    Tile tile = Tile::None;
    wlr_box restore {}; // the geometry to return to from a tile or full screen

protected:
    virtual wlr_box size() const = 0; // width and height; x and y are ignored
    virtual void moved() { }
    virtual void requestSize(int width, int height) = 0;
    virtual void sendActivated(bool activated) = 0;
    virtual void sendTiled(Tile tile) = 0;
    virtual void sendFullscreen(bool fullscreen) = 0;
    virtual void sendMinimized(bool) { }

    void onMapped();
    void onUnmapped();
    void updatePublished();
    void updateOutput();

    wlr_scene_tree* m_content = nullptr; // the program's surfaces, in the tree
    wlr_scene* m_captureScene;
    wlr_scene_tree* m_captureTree = nullptr;
    wlr_scene_tree* m_captureContent = nullptr;

private:
    void place();
    void publish();
    void unpublish();

    std::unique_ptr<Decoration> m_decoration;
    wlr_foreign_toplevel_handle_v1* m_foreign = nullptr;
    wlr_output* m_foreignOutput = nullptr; // the one the handle says the window is on
    wlr_ext_foreign_toplevel_handle_v1* m_listed = nullptr;
    Listener m_foreignActivate;
    Listener m_foreignClose;
    Listener m_foreignMinimize;
    Listener m_foreignMaximize;
    Listener m_foreignFullscreen;
};

// A window of xdg-shell, the Wayland way.
class XdgView : public View {
public:
    XdgView(Server& server, wlr_xdg_toplevel* toplevel);

    wlr_surface* surface() const override { return toplevel->base->surface; }
    std::string title() const override;
    std::string appId() const override;
    View* parentView() const override;
    int minimumWidth() const override;
    int minimumHeight() const override;
    void close() override;

    // The window's say in who draws its title bar, through xdg-decoration.
    void setDecorationObject(wlr_xdg_toplevel_decoration_v1* object);

    wlr_xdg_toplevel* toplevel;

protected:
    wlr_box size() const override { return toplevel->base->geometry; }
    void requestSize(int width, int height) override;
    void sendActivated(bool activated) override;
    void sendTiled(Tile tile) override;
    void sendFullscreen(bool fullscreen) override;

private:
    void commit();
    void applyDecorationMode();

    wlr_xdg_toplevel_decoration_v1* m_decorationObject = nullptr;
    Listener m_decorationRequest;
    Listener m_decorationDestroy;
    Listener m_map;
    Listener m_unmap;
    Listener m_commit;
    Listener m_destroy;
    Listener m_requestMove;
    Listener m_requestResize;
    Listener m_requestMaximize;
    Listener m_requestFullscreen;
    Listener m_requestMinimize;
    Listener m_setTitle;
    Listener m_setAppId;
};

// A window of an X11 program, through Xwayland.
class XwaylandView : public View {
public:
    XwaylandView(Server& server, wlr_xwayland_surface* surface);

    wlr_surface* surface() const override { return xsurface->surface; }
    std::string title() const override;
    std::string appId() const override;
    View* parentView() const override;
    int minimumWidth() const override;
    int minimumHeight() const override;
    void close() override;

    wlr_xwayland_surface* xsurface;

protected:
    wlr_box size() const override { return {0, 0, xsurface->width, xsurface->height}; }
    void moved() override;
    void requestSize(int width, int height) override;
    void sendActivated(bool activated) override;
    void sendTiled(Tile tile) override;
    void sendFullscreen(bool fullscreen) override;
    void sendMinimized(bool minimized) override;

private:
    void associate();
    void dissociate();

    Listener m_destroy;
    Listener m_associate;
    Listener m_dissociate;
    Listener m_map;
    Listener m_unmap;
    Listener m_requestConfigure;
    Listener m_requestMove;
    Listener m_requestResize;
    Listener m_requestMaximize;
    Listener m_requestFullscreen;
    Listener m_requestMinimize;
    Listener m_requestActivate;
    Listener m_setTitle;
    Listener m_setClass;
    Listener m_setDecorations;
    Listener m_commit;
};

// A menu, tooltip or the like of an X11 program, which places itself and is no window of its own.
class Unmanaged : public NodeOwner {
public:
    Unmanaged(Server& server, wlr_xwayland_surface* surface);

    wlr_xwayland_surface* xsurface;

private:
    void map();
    void unmap();

    Server& m_server;
    wlr_scene_tree* m_tree = nullptr;
    Listener m_destroy;
    Listener m_associate;
    Listener m_dissociate;
    Listener m_map;
    Listener m_unmap;
    Listener m_setGeometry;
    Listener m_requestConfigure;
};

} // namespace atlas
