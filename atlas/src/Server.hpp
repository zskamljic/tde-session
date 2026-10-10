#pragma once

#include "Decoration.hpp"
#include "Listener.hpp"
#include "Settings.hpp"
#include "wlr.hpp"

#include <memory>
#include <string>
#include <vector>

namespace atlas {

class Keyboard;
class LayerSurface;
class Output;
class Popup;
class Pointer;
class SessionLock;
class Unmanaged;
class View;

// What a scene tree belongs to; set as the data of the tree at the root of a window or layer
// surface, so the owner of the surface under the pointer can be found by walking up.
struct NodeOwner {
    enum class Kind { View, Layer, Unmanaged };
    explicit NodeOwner(Kind kind)
        : kind(kind)
    {
    }
    virtual ~NodeOwner() = default;
    Kind kind;
};

enum class Tile { None, Maximized, Left, Right };

// The compositor: the Wayland display, its outputs and input devices, and the windows on it.
class Server {
public:
    Server();
    ~Server();

    // Opens the display socket and starts the backend; false when something is missing.
    bool start();
    void run();
    void terminate();
    const std::string& socketName() const { return m_socket; }

    // Runs `command` with the shell, detached from the compositor, `argument` as its $1 when
    // given.
    static void spawn(const std::string& command, const std::string& argument = {});
    // The session lock: while locked, only the lock screen is seen and gets input.
    bool isLocked() const { return m_locked; }
    void unlocked();
    void lockAbandoned();
    void outputFramed(const Output& output);

    // Whether a layer surface, such as the overview, holds the keyboard.
    bool keyboardHeldByLayer() const { return m_layerFocus != nullptr; }

    // Windows ---------------------------------------------------------------------------

    // Gives a window the keyboard and raises it; it moves to the front of the switch order.
    void focus(View* view);
    // Focuses the window used most recently that is not minimized, if any.
    void focusTopmost();
    View* focusedView() const;
    // The window of another program that a new one is shown for: the one its program last
    // imported through xdg-foreign and still holds on to, as a portal's file picker does with
    // the window of the program that asked for it. Null for the rest.
    View* importedParent(const View& view) const;
    // Gives the keyboard to a surface that is no window, such as an X11 menu that asks for it.
    void focusSurface(wlr_surface* surface);
    const std::vector<View*>& stackingOrder() const { return m_order; }

    void viewMapped(View& view);
    void viewUnmapped(View& view);
    void viewDestroyed(View& view);

    void beginMove(View& view);
    void beginResize(View& view, uint32_t edges);

    // Puts a window on `output`, at the same place in it as where it is.
    void moveToOutput(View& view, wlr_output& output);

    // The part of the output at the given point that windows may use.
    wlr_box usableArea(double x, double y) const;
    Output* outputAt(double x, double y) const;

    // Layers ----------------------------------------------------------------------------

    void arrange(Output& output);
    void layerSurfaceDestroyed(LayerSurface& surface);
    void updateLayerFocus();
    void outputDestroyed(Output& output);
    void turnOn(Output& output);
    void newPopup(wlr_xdg_popup* popup);
    void popupDestroyed(Popup& popup);
    void keyboardDestroyed(Keyboard& keyboard);
    void pointerDestroyed(Pointer& pointer);
    void unmanagedDestroyed(Unmanaged& surface);

    // Public to the parts of the compositor ---------------------------------------------

    wl_display* display = nullptr;
    wl_event_loop* eventLoop = nullptr;
    wlr_backend* backend = nullptr;
    wlr_session* session = nullptr;
    wlr_renderer* renderer = nullptr;
    wlr_allocator* allocator = nullptr;
    wlr_compositor* compositor = nullptr;
    wlr_scene* scene = nullptr;
    wlr_scene_output_layout* sceneLayout = nullptr;
    wlr_output_layout* outputLayout = nullptr;
    wlr_seat* seat = nullptr;
    wlr_cursor* cursor = nullptr;
    wlr_xcursor_manager* xcursor = nullptr;
    wlr_foreign_toplevel_manager_v1* foreignToplevels = nullptr;
    wlr_ext_foreign_toplevel_list_v1* toplevelList = nullptr;
    wlr_xdg_foreign_v1* foreignV1 = nullptr; // windows shared between programs
    wlr_xdg_foreign_v2* foreignV2 = nullptr;
    wlr_idle_notifier_v1* idleNotifier = nullptr;
    int windowAnimationTime = 200; // ms windows take to move into a tile; 0 moves them at once

    // From bottom to top.
    struct {
        wlr_scene_tree* background = nullptr;
        wlr_scene_tree* bottom = nullptr;
        wlr_scene_tree* views = nullptr;
        wlr_scene_tree* unmanaged = nullptr; // X11 menus and tooltips
        wlr_scene_tree* top = nullptr;
        wlr_scene_tree* fullscreen = nullptr; // windows covering the screen, above its bars
        wlr_scene_tree* overlay = nullptr;
        wlr_scene_tree* feedback = nullptr; // snap previews, drag icons
        wlr_scene_tree* lock = nullptr; // the lock screen, over everything
    } layers;

    std::vector<std::unique_ptr<Output>> outputs;
    std::vector<std::unique_ptr<LayerSurface>> layerSurfaces;

private:
    enum class CursorMode { Passthrough, Move, Resize };

    void setUpOutputs();
    void addVirtualOutputs();
    void setUpShells();
    void setUpInput();
    void setUpProtocols();
    void setUpXwayland();
    void setUpLock();
    void setUpWindowInfo();
    void newLock(wlr_session_lock_v1* lock);
    void coverOutputs();

    void newOutput(wlr_output* output);
    void placeOutput(wlr_output_layout_output* placed, wlr_scene_output* made = nullptr);
    void layoutChanged();
    void outputLayoutChanged();
    void applyOutputConfiguration(wlr_output_configuration_v1* config, bool testOnly);
    wlr_box onScreen(const wlr_box& box) const;
    void keepWindowsOnScreen();
    void moveWindowsWithOutputs();

    // Input.cpp
    void newInput(wlr_input_device* device);
    void newKeyboard(wlr_keyboard* keyboard, bool virtualKeyboard);
    // Follows the system's keyboard layout as localectl changes it, on the keyboards there are.
    void watchKeyboardLayout();
    void keyboardLayoutChanged();
    // Follows the session's settings as Settings saves them.
    void watchSettings();
    void applySettings();
    void configurePointer(const Pointer& pointer) const;
    void updateCapabilities();
    void cursorMotion(uint32_t timeMsec);
    void cursorButton(wlr_pointer_button_event* event);
    NodeOwner* ownerAt(double x, double y, wlr_surface** surface, double* sx, double* sy) const;
    void updateMove();
    void updateResize();
    void finishGrab();
    void showSnapPreview(Tile tile, const wlr_box& area);
    void stepSnapPreview();
    bool decorationButton(View& view, wlr_pointer_button_event* event);
    void decorationReleased(View& view);
    void setDecorationHover(View* view, Decoration::Part part);
    void forgetPointerTargets(const View& view);
    void inhibitorDestroyed(wlr_idle_inhibitor_v1* inhibitor);

public: // called by Keyboard
    bool handleKey(Keyboard& keyboard, uint32_t keycode, bool pressed);
    void modifiersChanged(wlr_keyboard& keyboard);

private:
    bool runBinding(uint32_t modifiers, xkb_keysym_t sym, uint32_t keycode);

    std::string m_socket;
    wlr_xdg_shell* m_xdgShell = nullptr;
    wlr_layer_shell_v1* m_layerShell = nullptr;
    wlr_output_manager_v1* m_outputManager = nullptr;
    wl_event_source* m_layoutIdle = nullptr; // to see layout changes once they are all done
    int m_keyboardWatch = -1; // inotify, on where the system keeps the keyboard layout
    wl_event_source* m_keyboardWatchSource = nullptr;
    Settings m_settings;
    int m_settingsWatch = -1; // inotify, on the folders of the session's settings
    wl_event_source* m_settingsWatchSource = nullptr;
    wlr_idle_inhibit_manager_v1* m_idleInhibit = nullptr;
    wlr_xwayland* m_xwayland = nullptr;

    std::vector<std::unique_ptr<View>> m_views;
    std::vector<View*> m_order; // mapped windows, most recently used first
    std::vector<std::unique_ptr<Popup>> m_popups;
    std::vector<std::unique_ptr<Keyboard>> m_keyboards;
    std::vector<std::unique_ptr<Pointer>> m_pointers;
    std::vector<std::unique_ptr<Unmanaged>> m_unmanaged;
    LayerSurface* m_layerFocus = nullptr; // a layer surface that holds the keyboard

    // Pointer grabs: moving or resizing a window.
    CursorMode m_cursorMode = CursorMode::Passthrough;
    View* m_grabbed = nullptr;
    double m_grabX = 0;
    double m_grabY = 0;
    wlr_box m_grabBox {};
    uint32_t m_resizeEdges = 0;
    Tile m_snapTarget = Tile::None;
    wlr_scene_rect* m_snapPreview = nullptr;
    wl_event_source* m_snapPreviewTimer = nullptr; // while it grows into place
    wlr_box m_snapPreviewFrom {};
    wlr_box m_snapPreviewTo {};
    uint64_t m_snapPreviewStart = 0; // ms, of the monotonic clock

    // Title bars drawn here: the one under the pointer, the one whose button is held, and a
    // title held down, which becomes a move once the pointer goes.
    View* m_decorationHover = nullptr;
    View* m_decorationPress = nullptr;
    View* m_titlePress = nullptr;
    double m_titlePressX = 0;
    double m_titlePressY = 0;
    const View* m_titleClickView = nullptr;
    uint32_t m_titleClickTime = 0;

    // The surface a button was pressed on, which keeps the pointer until it is let go, and
    // where it was in the layout then.
    wlr_surface* m_pressedSurface = nullptr;
    double m_pressedX = 0;
    double m_pressedY = 0;

    // The modifier holding a window switcher of the overview open, until it is let go.
    uint32_t m_pickingWith = 0;

    // Super pressed and released on its own opens the overview.
    bool m_superAlone = false;

    wlr_scene_tree* m_dragIcon = nullptr;

    Listener m_newOutput;
    Listener m_layoutChange;
    Listener m_outputManagerApply;
    Listener m_outputManagerTest;
    Listener m_newToplevel;
    Listener m_newPopup;
    Listener m_newDecoration;

    // Locked by a lock screen; still locked when it went away without unlocking, until
    // another takes its place.
    bool m_locked = false;
    std::unique_ptr<SessionLock> m_lock;
    wlr_scene_rect* m_lockCover = nullptr; // hides everything while the lock screen has not drawn
    wl_event_source* m_relock = nullptr; // starts a lock screen again when one went away
    Listener m_newLock;
    Listener m_newLayerSurface;
    Listener m_newInput;
    Listener m_newVirtualKeyboard;
    Listener m_newVirtualPointer;
    Listener m_cursorMotion;
    Listener m_cursorMotionAbsolute;
    Listener m_cursorButton;
    Listener m_cursorAxis;
    Listener m_cursorFrame;
    Listener m_requestCursor;
    Listener m_requestCursorShape;
    Listener m_requestSelection;
    Listener m_requestPrimarySelection;
    Listener m_requestStartDrag;
    Listener m_startDrag;
    Listener m_dragIconDestroy;
    Listener m_requestActivate;
    Listener m_newInhibitor;
    struct Inhibitor {
        wlr_idle_inhibitor_v1* inhibitor = nullptr;
        Listener destroy;
    };
    std::vector<std::unique_ptr<Inhibitor>> m_inhibitors;
    Listener m_newCaptureSource;
    Listener m_outputPowerMode;
    Listener m_newXwaylandSurface;
    Listener m_xwaylandReady;
};

} // namespace atlas
