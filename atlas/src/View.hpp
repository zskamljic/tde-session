#pragma once

#include "Decoration.hpp"
#include "Server.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace atlas {

class WindowCapture;

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
    // As setGeometry(), with a picture of the window going from where it was to there
    // meanwhile, until the window drew itself in its new size.
    void animateTo(const wlr_box& box);

    void setActivated(bool activated);
    // With `animate`, it goes there as animateTo() has it: for tiles the user asked for.
    void setTile(Tile tile, bool animate = false);
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
    // What copies of the window alone are taken from, made the first time one is asked for.
    wlr_ext_image_capture_source_v1* captureSource();
    // The window drew something new, which copies waiting for it get.
    void damaged();

    Server& server;
    wlr_scene_tree* tree = nullptr; // where the window is, title bar and all
    bool mapped = false;
    bool activated = false;
    bool minimized = false;
    bool fullscreen = false;
    Tile tile = Tile::None;
    wlr_box restore {}; // the geometry to return to from a tile or full screen
    bool handled = false; // moved or resized by the user since it was placed

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
    // Whether the window showed a moment ago and was left where it was put.
    bool placedRecently() const;
    // The window's size changed: right after it showed, it is placed again.
    void sizeCommitted();
    void updatePublished();
    void updateOutput();

    wlr_scene_tree* m_content = nullptr; // the program's surfaces, in the tree
    wlr_scene* m_captureScene;
    wlr_scene_tree* m_captureTree = nullptr;
    wlr_scene_tree* m_captureContent = nullptr;

private:
    static uint64_t now(); // ms, of the monotonic clock
    WindowCapture& capture();
    void stepMorph();
    void endMorph();
    void place();
    void publish();
    void unpublish();

    // The picture standing in for the window while it moves to a tile.
    wlr_scene_buffer* m_morph = nullptr;
    wl_event_source* m_morphTimer = nullptr;
    wlr_box m_morphFrom {};
    wlr_box m_morphTo {};
    wlr_box m_morphExtents {}; // of the picture, relative to the window where it started
    uint64_t m_morphStart = 0;
    uint64_t m_placedAt = 0;
    wlr_box m_placedSize {};
    std::unique_ptr<Decoration> m_decoration;
    std::unique_ptr<WindowCapture> m_capture;
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
