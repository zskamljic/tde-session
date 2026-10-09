#include "Server.hpp"

#include "Lock.hpp"
#include "Parts.hpp"

#include <linux/input-event-codes.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <regex>
#include <set>
#include <string_view>
#include <utility>

namespace atlas {
namespace {

constexpr uint32_t BindingModifiers = WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO;
constexpr int SnapEdge = 4; // pixels from the screen edge where a dragged window snaps

// Keys that were taken for a binding; their release does not go to the window either.
std::set<uint32_t> s_consumedKeys;

std::string environment(const char* name)
{
    const char* value = std::getenv(name);
    return value ? value : "";
}

// The keyboard layout: from XKB_DEFAULT_* when set, as for any wlroots compositor, otherwise
// the system one that localectl keeps for X11.
xkb_rule_names keyboardLayout(std::string& layout, std::string& variant, std::string& options)
{
    if (environment("XKB_DEFAULT_LAYOUT").empty()) {
        std::ifstream file("/etc/X11/xorg.conf.d/00-keyboard.conf");
        const std::regex option(R"re(Option\s+"(XkbLayout|XkbVariant|XkbOptions)"\s+"([^"]*)")re");
        std::string line;
        std::smatch match;
        while (std::getline(file, line)) {
            if (!std::regex_search(line, match, option))
                continue;
            if (match[1] == "XkbLayout")
                layout = match[2];
            else if (match[1] == "XkbVariant")
                variant = match[2];
            else
                options = match[2];
        }
    }
    xkb_rule_names names {};
    if (!layout.empty()) {
        names.layout = layout.c_str();
        names.variant = variant.empty() ? nullptr : variant.c_str();
        names.options = options.empty() ? nullptr : options.c_str();
    }
    return names;
}

// Gives a keyboard of the machine the layout the system has.
void setSystemKeymap(wlr_keyboard* keyboard)
{
    std::string layout, variant, options;
    const xkb_rule_names names = keyboardLayout(layout, variant, options);
    xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_keymap* keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap)
        keymap = xkb_keymap_new_from_names(context, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
    wlr_keyboard_set_keymap(keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
}

bool isSuper(xkb_keysym_t sym)
{
    return sym == XKB_KEY_Super_L || sym == XKB_KEY_Super_R;
}

// Calls a method of a part of the shell over the session bus, or runs `otherwise` when that
// part is not there.
void callShell(std::string_view object, std::string_view call, std::string_view otherwise)
{
    Server::spawn(std::format("busctl --user call {} {} 2>/dev/null || {}", object, call, otherwise));
}

// Asks the bar to do `method`.
void hermes(std::string_view method, std::string_view otherwise)
{
    callShell("io.github.zskamljic.Hermes /io/github/zskamljic/Hermes",
        std::format("io.github.zskamljic.Hermes {}", method), otherwise);
}

// The window switcher of the overview that a modifier holds open: Flip 3D for Super, the
// switcher for Alt.
std::string_view pickerFor(uint32_t modifier)
{
    return modifier == WLR_MODIFIER_LOGO ? "Flip" : "Switcher";
}

// Asks the window switcher held open by `modifier` to do `call`.
void callPicker(uint32_t modifier, std::string_view call, std::string_view otherwise)
{
    const std::string_view picker = pickerFor(modifier);
    callShell(std::format("io.github.zskamljic.Argus /io/github/zskamljic/Argus/{}", picker),
        std::format("io.github.zskamljic.Argus.{} {}", picker, call), otherwise);
}

} // namespace

Keyboard::Keyboard(Server& server, wlr_keyboard* keyboard, bool virtualKeyboard)
    : keyboard(keyboard)
    , virtualKeyboard(virtualKeyboard)
    , m_server(server)
{
    m_key.connect<wlr_keyboard_key_event>(keyboard->events.key, [this](wlr_keyboard_key_event* event) {
        const bool pressed = event->state == WL_KEYBOARD_KEY_STATE_PRESSED;
        if (m_server.handleKey(*this, event->keycode, pressed))
            return;
        wlr_seat_set_keyboard(m_server.seat, this->keyboard);
        wlr_seat_keyboard_notify_key(m_server.seat, event->time_msec, event->keycode, event->state);
    });
    m_modifiers.connect(keyboard->events.modifiers, [this] {
        m_server.modifiersChanged(*this->keyboard);
        wlr_seat_set_keyboard(m_server.seat, this->keyboard);
        wlr_seat_keyboard_notify_modifiers(m_server.seat, &this->keyboard->modifiers);
    });
    m_destroy.connect(keyboard->base.events.destroy, [this] { m_server.keyboardDestroyed(*this); });
}

void Server::setUpInput()
{
    cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(cursor, outputLayout);
    // The size the session asks for, or the usual one when it asks for none it makes sense of.
    const std::string sizeText = environment("XCURSOR_SIZE");
    unsigned cursorSize = 0;
    if (std::from_chars(sizeText.data(), sizeText.data() + sizeText.size(), cursorSize).ec != std::errc {}
        || cursorSize == 0 || cursorSize > 256)
        cursorSize = 24;
    xcursor = wlr_xcursor_manager_create(std::getenv("XCURSOR_THEME"), cursorSize);
    wlr_cursor_set_xcursor(cursor, xcursor, "default");

    m_cursorMotion.connect<wlr_pointer_motion_event>(cursor->events.motion, [this](wlr_pointer_motion_event* event) {
        wlr_cursor_move(cursor, &event->pointer->base, event->delta_x, event->delta_y);
        cursorMotion(event->time_msec);
    });
    m_cursorMotionAbsolute.connect<wlr_pointer_motion_absolute_event>(
        cursor->events.motion_absolute, [this](wlr_pointer_motion_absolute_event* event) {
            wlr_cursor_warp_absolute(cursor, &event->pointer->base, event->x, event->y);
            cursorMotion(event->time_msec);
        });
    m_cursorButton.connect<wlr_pointer_button_event>(
        cursor->events.button, [this](wlr_pointer_button_event* event) { cursorButton(event); });
    m_cursorAxis.connect<wlr_pointer_axis_event>(cursor->events.axis, [this](wlr_pointer_axis_event* event) {
        wlr_idle_notifier_v1_notify_activity(idleNotifier, seat);
        wlr_seat_pointer_notify_axis(seat, event->time_msec, event->orientation, event->delta, event->delta_discrete,
            event->source, event->relative_direction);
    });
    m_cursorFrame.connect(cursor->events.frame, [this] { wlr_seat_pointer_notify_frame(seat); });

    seat = wlr_seat_create(display, "seat0");
    m_newInput.connect<wlr_input_device>(
        backend->events.new_input, [this](wlr_input_device* device) { newInput(device); });
    watchKeyboardLayout();

    m_requestCursor.connect<wlr_seat_pointer_request_set_cursor_event>(
        seat->events.request_set_cursor, [this](wlr_seat_pointer_request_set_cursor_event* event) {
            if (m_cursorMode == CursorMode::Passthrough && event->seat_client == seat->pointer_state.focused_client)
                wlr_cursor_set_surface(cursor, event->surface, event->hotspot_x, event->hotspot_y);
        });
    auto* shapes = wlr_cursor_shape_manager_v1_create(display, 1);
    m_requestCursorShape.connect<wlr_cursor_shape_manager_v1_request_set_shape_event>(
        shapes->events.request_set_shape, [this](wlr_cursor_shape_manager_v1_request_set_shape_event* event) {
            if (m_cursorMode == CursorMode::Passthrough && event->seat_client == seat->pointer_state.focused_client)
                wlr_cursor_set_xcursor(cursor, xcursor, wlr_cursor_shape_v1_name(event->shape));
        });
    m_requestSelection.connect<wlr_seat_request_set_selection_event>(
        seat->events.request_set_selection, [this](wlr_seat_request_set_selection_event* event) {
            wlr_seat_set_selection(seat, event->source, event->serial);
        });
    m_requestPrimarySelection.connect<wlr_seat_request_set_primary_selection_event>(
        seat->events.request_set_primary_selection, [this](wlr_seat_request_set_primary_selection_event* event) {
            wlr_seat_set_primary_selection(seat, event->source, event->serial);
        });

    // Drag and drop between windows; the dragged icon follows the pointer.
    m_requestStartDrag.connect<wlr_seat_request_start_drag_event>(
        seat->events.request_start_drag, [this](wlr_seat_request_start_drag_event* event) {
            if (wlr_seat_validate_pointer_grab_serial(seat, event->origin, event->serial))
                wlr_seat_start_pointer_drag(seat, event->drag, event->serial);
            else
                wlr_data_source_destroy(event->drag->source);
        });
    m_startDrag.connect<wlr_drag>(seat->events.start_drag, [this](wlr_drag* drag) {
        if (!drag->icon)
            return;
        m_dragIcon = wlr_scene_drag_icon_create(layers.feedback, drag->icon);
        wlr_scene_node_set_position(&m_dragIcon->node, int(cursor->x), int(cursor->y));
        m_dragIconDestroy.connect(drag->icon->events.destroy, [this] {
            m_dragIcon = nullptr;
            m_dragIconDestroy.disconnect();
        });
    });

    // Programs that type or point, such as wtype and on-screen keyboards.
    auto* virtualKeyboards = wlr_virtual_keyboard_manager_v1_create(display);
    m_newVirtualKeyboard.connect<wlr_virtual_keyboard_v1>(virtualKeyboards->events.new_virtual_keyboard,
        [this](wlr_virtual_keyboard_v1* keyboard) { newKeyboard(&keyboard->keyboard, true); });
    auto* virtualPointers = wlr_virtual_pointer_manager_v1_create(display);
    m_newVirtualPointer.connect<wlr_virtual_pointer_v1_new_pointer_event>(
        virtualPointers->events.new_virtual_pointer, [this](wlr_virtual_pointer_v1_new_pointer_event* event) {
            wlr_cursor_attach_input_device(cursor, &event->new_pointer->pointer.base);
        });
}

void Server::newInput(wlr_input_device* device)
{
    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        newKeyboard(wlr_keyboard_from_input_device(device), false);
        break;
    // Touch screens and tablets wait until their events are handled.
    case WLR_INPUT_DEVICE_POINTER:
        wlr_cursor_attach_input_device(cursor, device);
        break;
    default:
        break;
    }
    updateCapabilities();
}

void Server::newKeyboard(wlr_keyboard* keyboard, bool virtualKeyboard)
{
    // Virtual keyboards bring the keymap of the program behind them.
    if (!virtualKeyboard)
        setSystemKeymap(keyboard);
    wlr_keyboard_set_repeat_info(keyboard, 25, 600);
    m_keyboards.push_back(std::make_unique<Keyboard>(*this, keyboard, virtualKeyboard));
    wlr_seat_set_keyboard(seat, keyboard);
    updateCapabilities();
}

void Server::watchKeyboardLayout()
{
    // Set for the session, it stays as it is.
    if (!environment("XKB_DEFAULT_LAYOUT").empty())
        return;
    m_keyboardWatch = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_keyboardWatch < 0)
        return;
    // localectl writes the file anew; the folder may not be there before it first does.
    for (const char* folder : {"/etc/X11", "/etc/X11/xorg.conf.d"})
        inotify_add_watch(m_keyboardWatch, folder, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE);
    m_keyboardWatchSource = wl_event_loop_add_fd(
        eventLoop, m_keyboardWatch, WL_EVENT_READABLE,
        [](int fd, uint32_t, void* data) {
            alignas(inotify_event) char events[4096];
            while (read(fd, events, sizeof events) > 0) { }
            static_cast<Server*>(data)->keyboardLayoutChanged();
            return 0;
        },
        this);
}

void Server::keyboardLayoutChanged()
{
    // The folder made since starting is watched from now on.
    inotify_add_watch(m_keyboardWatch, "/etc/X11/xorg.conf.d", IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE);
    for (const auto& keyboard : m_keyboards) {
        if (!keyboard->virtualKeyboard)
            setSystemKeymap(keyboard->keyboard);
    }
}

void Server::keyboardDestroyed(Keyboard& keyboard)
{
    const bool wasSeats = wlr_seat_get_keyboard(seat) == keyboard.keyboard;
    std::erase_if(m_keyboards, [&](const auto& k) { return k.get() == &keyboard; });
    // The seat goes on with another keyboard, such as the real one after a virtual one is gone:
    // without one, programs that start later get no keymap to read their keys with.
    if (wasSeats && !m_keyboards.empty())
        wlr_seat_set_keyboard(seat, m_keyboards.front()->keyboard);
    updateCapabilities();
}

void Server::updateCapabilities()
{
    uint32_t capabilities = WL_SEAT_CAPABILITY_POINTER;
    if (!m_keyboards.empty())
        capabilities |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(seat, capabilities);
}

// Keyboard --------------------------------------------------------------------------------

bool Server::handleKey(Keyboard& keyboard, uint32_t keycode, bool pressed)
{
    wlr_idle_notifier_v1_notify_activity(idleNotifier, seat);

    // Bindings follow the keys as printed on the first layout, without Shift and the like.
    const xkb_keycode_t xkbKeycode = keycode + 8;
    xkb_state* state = keyboard.keyboard->xkb_state;
    const xkb_keysym_t* syms = nullptr;
    const int count = xkb_keymap_key_get_syms_by_level(
        keyboard.keyboard->keymap, xkbKeycode, xkb_state_key_get_layout(state, xkbKeycode), 0, &syms);
    const uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard.keyboard) & BindingModifiers;

    // Locked, the keys are the lock screen's; only switching to another terminal stays.
    if (m_locked) {
        m_superAlone = false;
        for (int i = 0; i < count && pressed; ++i) {
            if (modifiers == (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT) && syms[i] >= XKB_KEY_F1
                && syms[i] <= XKB_KEY_F12) {
                if (session)
                    wlr_session_change_vt(session, syms[i] - XKB_KEY_F1 + 1);
                s_consumedKeys.insert(keycode);
                return true;
            }
        }
        return !pressed && s_consumedKeys.erase(keycode) > 0;
    }

    if (!pressed) {
        for (int i = 0; i < count; ++i) {
            if (isSuper(syms[i]) && m_superAlone)
                spawn("tde-argus --toggle");
        }
        m_superAlone = false;
        return s_consumedKeys.erase(keycode) > 0;
    }

    m_superAlone = count > 0 && isSuper(syms[0]) && (modifiers & ~WLR_MODIFIER_LOGO) == 0;
    for (int i = 0; i < count; ++i) {
        if (runBinding(modifiers, syms[i], keycode)) {
            s_consumedKeys.insert(keycode);
            return true;
        }
    }
    return false;
}

void Server::modifiersChanged(wlr_keyboard& keyboard)
{
    // The modifier holding a window switcher open was let go: it switches. It is told from
    // here, as the key may have gone up before the switcher had the keyboard, or come as a
    // change of the modifiers alone.
    if (m_pickingWith != 0 && !(wlr_keyboard_get_modifiers(&keyboard) & m_pickingWith)) {
        callPicker(m_pickingWith, "Release", "true");
        m_pickingWith = 0;
    }
}

bool Server::runBinding(uint32_t modifiers, xkb_keysym_t sym, uint32_t keycode)
{
    constexpr uint32_t Super = WLR_MODIFIER_LOGO;
    constexpr uint32_t Alt = WLR_MODIFIER_ALT;
    constexpr uint32_t Ctrl = WLR_MODIFIER_CTRL;
    constexpr uint32_t Shift = WLR_MODIFIER_SHIFT;
    View* view = focusedView();
    const auto withView = [view](auto action) {
        if (view)
            action(*view);
        return true;
    };

    // Switching windows, drawn by the overview: Alt+Tab, Alt and the key above Tab for the
    // windows of one application, and Super+Tab flipping through them in 3D. Shift goes
    // backwards. Once the switcher shows, the keys are its own.
    const uint32_t base = modifiers & ~Shift;
    const bool tab = sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab;
    // The key above Tab, whatever it prints.
    const bool grave = keycode == KEY_GRAVE && base == Alt;
    if (((base == Alt || base == Super) && tab) || grave) {
        if (keyboardHeldByLayer())
            return false;
        m_pickingWith = base;
        callPicker(base, std::format("Show bb {} {}", bool(modifiers & Shift), grave),
            base == Super ? "exec tde-argus --flip" : "exec tde-argus --switch");
        return true;
    }

    if (modifiers == (Ctrl | Alt)) {
        if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12) {
            if (session)
                wlr_session_change_vt(session, sym - XKB_KEY_F1 + 1);
            return true;
        }
        switch (sym) {
        case XKB_KEY_t:
            spawn("tde-session --terminal");
            return true;
        case XKB_KEY_Delete:
            terminate();
            return true;
        default:
            break;
        }
    }

    if (modifiers == Alt) {
        switch (sym) {
        case XKB_KEY_F4:
            return withView([](View& v) { v.close(); });
        case XKB_KEY_F10:
            return withView([](View& v) { v.setTile(v.tile == Tile::Maximized ? Tile::None : Tile::Maximized, true); });
        default:
            break;
        }
    }

    if (modifiers == Super) {
        switch (sym) {
        case XKB_KEY_q:
            return withView([](View& v) { v.close(); });
        case XKB_KEY_h:
            return withView([](View& v) { v.setMinimized(true); });
        case XKB_KEY_Up:
            return withView([](View& v) { v.setTile(Tile::Maximized, true); });
        case XKB_KEY_Down:
            return withView([](View& v) {
                if (v.fullscreen)
                    v.setFullscreen(false);
                else
                    v.setTile(Tile::None, true);
            });
        case XKB_KEY_Left:
            return withView([](View& v) { v.setTile(v.tile == Tile::Right ? Tile::None : Tile::Left, true); });
        case XKB_KEY_Right:
            return withView([](View& v) { v.setTile(v.tile == Tile::Left ? Tile::None : Tile::Right, true); });
        case XKB_KEY_a:
            spawn("tde-argus --applications");
            return true;
        case XKB_KEY_e:
            spawn("ariadne");
            return true;
        case XKB_KEY_l:
            // Through the bar, which knows how the lock screen is wanted.
            callShell("io.github.zskamljic.Hermes /io/github/zskamljic/Hermes/Locking",
                "io.github.zskamljic.Hermes.Locking Lock", "exec tde-cerberus");
            return true;
        default:
            break;
        }
    }

    // Screenshots, taken by the overview: of what is picked, the screens, or the window in use.
    if (sym == XKB_KEY_Print) {
        const auto [method, option] = modifiers == Shift ? std::pair {"TakeScreen", "--screenshot-screen"}
            : modifiers == Alt                           ? std::pair {"TakeWindow", "--screenshot-window"}
                                                         : std::pair {"Show", "--screenshot"};
        callShell("io.github.zskamljic.Argus /io/github/zskamljic/Argus/Screenshot",
            std::format("io.github.zskamljic.Argus.Screenshot {}", method), std::format("exec tde-argus {}", option));
        return true;
    }

    switch (sym) {
    // The bar changes them and shows it; without it, they still change.
    case XKB_KEY_XF86AudioLowerVolume:
        hermes("LowerVolume", "wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-");
        return true;
    case XKB_KEY_XF86AudioRaiseVolume:
        hermes("RaiseVolume", "wpctl set-volume -l 1 @DEFAULT_AUDIO_SINK@ 5%+");
        return true;
    case XKB_KEY_XF86AudioMute:
        hermes("ToggleMute", "wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle");
        return true;
    case XKB_KEY_XF86MonBrightnessUp:
        hermes("RaiseBrightness", "brightnessctl set +5%");
        return true;
    case XKB_KEY_XF86MonBrightnessDown:
        hermes("LowerBrightness", "brightnessctl set 5%-");
        return true;
    default:
        return false;
    }
}

// Pointer ---------------------------------------------------------------------------------

NodeOwner* Server::ownerAt(double x, double y, wlr_surface** surface, double* sx, double* sy) const
{
    wlr_scene_node* node = wlr_scene_node_at(&scene->tree.node, x, y, sx, sy);
    if (!node)
        return nullptr;
    // A program's surface, or else a title bar or the margin around a window.
    *surface = nullptr;
    if (node->type == WLR_SCENE_NODE_BUFFER) {
        if (wlr_scene_surface* sceneSurface = wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node)))
            *surface = sceneSurface->surface;
    }
    for (wlr_scene_tree* tree = node->parent; tree; tree = tree->node.parent) {
        if (tree->node.data)
            return static_cast<NodeOwner*>(tree->node.data);
    }
    return nullptr;
}

void Server::cursorMotion(uint32_t timeMsec)
{
    wlr_idle_notifier_v1_notify_activity(idleNotifier, seat);
    if (m_dragIcon)
        wlr_scene_node_set_position(&m_dragIcon->node, int(cursor->x), int(cursor->y));

    if (m_cursorMode == CursorMode::Move) {
        updateMove();
        return;
    }
    if (m_cursorMode == CursorMode::Resize) {
        updateResize();
        return;
    }
    // Locked: the pointer is for the lock screen alone.
    if (m_locked) {
        wlr_surface* surface = nullptr;
        double sx = 0;
        double sy = 0;
        ownerAt(cursor->x, cursor->y, &surface, &sx, &sy);
        if (m_lock && m_lock->owns(surface)) {
            wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
            wlr_seat_pointer_notify_motion(seat, timeMsec, sx, sy);
        } else {
            wlr_cursor_set_xcursor(cursor, xcursor, "default");
            wlr_seat_pointer_clear_focus(seat);
        }
        return;
    }
    // A title held down and dragged: the window follows from now on.
    if (m_titlePress && std::hypot(cursor->x - m_titlePressX, cursor->y - m_titlePressY) > 4) {
        View* view = std::exchange(m_titlePress, nullptr);
        beginMove(*view);
        return;
    }

    wlr_surface* surface = nullptr;
    double sx = 0;
    double sy = 0;
    NodeOwner* owner = ownerAt(cursor->x, cursor->y, &surface, &sx, &sy);
    View* decorated = nullptr;
    Decoration::Part part = Decoration::Part::None;
    uint32_t edges = WLR_EDGE_NONE;
    if (!surface && owner && owner->kind == NodeOwner::Kind::View) {
        auto* view = static_cast<View*>(owner);
        if (view->decoration()) {
            decorated = view;
            part = view->decoration()->partAt(cursor->x, cursor->y, &edges);
        }
    }
    setDecorationHover(decorated, part);

    if (surface) {
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, timeMsec, sx, sy);
    } else {
        wlr_cursor_set_xcursor(cursor, xcursor,
            part == Decoration::Part::Edge ? wlr_xcursor_get_resize_name(static_cast<wlr_edges>(edges)) : "default");
        wlr_seat_pointer_clear_focus(seat);
    }
}

void Server::setDecorationHover(View* view, Decoration::Part part)
{
    if (m_decorationHover && m_decorationHover != view && m_decorationHover->decoration())
        m_decorationHover->decoration()->setHovered(Decoration::Part::None);
    m_decorationHover = view;
    if (view)
        view->decoration()->setHovered(part);
}

// A press on a title bar or around a window; true when it was one.
bool Server::decorationButton(View& view, wlr_pointer_button_event* event)
{
    uint32_t edges = WLR_EDGE_NONE;
    const Decoration::Part part = view.decoration()->partAt(cursor->x, cursor->y, &edges);
    if (event->button != BTN_LEFT)
        return part != Decoration::Part::None;
    switch (part) {
    case Decoration::Part::None:
        return false;
    case Decoration::Part::Edge:
        beginResize(view, edges);
        return true;
    case Decoration::Part::Title:
        // Twice in a row maximizes or restores; held and dragged moves.
        if (m_titleClickView == &view && event->time_msec - m_titleClickTime < 400) {
            m_titleClickView = nullptr;
            view.setTile(view.tile == Tile::Maximized ? Tile::None : Tile::Maximized, true);
            return true;
        }
        m_titleClickView = &view;
        m_titleClickTime = event->time_msec;
        m_titlePress = &view;
        m_titlePressX = cursor->x;
        m_titlePressY = cursor->y;
        return true;
    case Decoration::Part::Minimize:
    case Decoration::Part::Maximize:
    case Decoration::Part::Close:
        m_decorationPress = &view;
        view.decoration()->setPressed(part);
        return true;
    }
    return false;
}

// The button of a title bar does its thing when let go over it.
void Server::decorationReleased(View& view)
{
    Decoration* decoration = view.decoration();
    if (!decoration)
        return;
    const Decoration::Part pressed = decoration->pressed();
    decoration->setPressed(Decoration::Part::None);
    uint32_t edges = WLR_EDGE_NONE;
    if (decoration->partAt(cursor->x, cursor->y, &edges) != pressed)
        return;
    switch (pressed) {
    case Decoration::Part::Close:
        view.close();
        break;
    case Decoration::Part::Maximize:
        view.setTile(view.tile == Tile::Maximized ? Tile::None : Tile::Maximized, true);
        break;
    case Decoration::Part::Minimize:
        view.setMinimized(true);
        break;
    default:
        break;
    }
}

void Server::cursorButton(wlr_pointer_button_event* event)
{
    wlr_idle_notifier_v1_notify_activity(idleNotifier, seat);
    static bool s_swallowRelease = false;
    if (m_locked) {
        // Only the lock screen can have the pointer now.
        wlr_seat_pointer_notify_button(seat, event->time_msec, event->button, event->state);
        return;
    }

    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        m_titlePress = nullptr;
        if (View* view = std::exchange(m_decorationPress, nullptr))
            decorationReleased(*view);
        if (m_cursorMode != CursorMode::Passthrough)
            finishGrab();
        if (s_swallowRelease) {
            s_swallowRelease = false;
            return;
        }
        wlr_seat_pointer_notify_button(seat, event->time_msec, event->button, event->state);
        return;
    }

    m_superAlone = false;
    wlr_surface* surface = nullptr;
    double sx = 0;
    double sy = 0;
    NodeOwner* owner = ownerAt(cursor->x, cursor->y, &surface, &sx, &sy);
    if (owner && owner->kind == NodeOwner::Kind::View) {
        auto* view = static_cast<View*>(owner);
        focus(view);
        // Super and a drag moves the window from anywhere in it, as in GNOME.
        wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
        if (keyboard && (wlr_keyboard_get_modifiers(keyboard) & WLR_MODIFIER_LOGO) && event->button == BTN_LEFT) {
            beginMove(*view);
            s_swallowRelease = true;
            return;
        }
        if (!surface && view->decoration() && decorationButton(*view, event)) {
            s_swallowRelease = true;
            return;
        }
    } else if (owner && owner->kind == NodeOwner::Kind::Layer) {
        auto* layer = static_cast<LayerSurface*>(owner);
        if (layer->surface->current.keyboard_interactive == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND)
            focusSurface(layer->surface->surface);
    }
    wlr_seat_pointer_notify_button(seat, event->time_msec, event->button, event->state);
}

void Server::beginMove(View& view)
{
    if (view.fullscreen || m_cursorMode != CursorMode::Passthrough)
        return;
    view.handled = true;
    if (view.isTiled()) {
        // Leaving a tile restores the size the window had, under the same spot of the pointer.
        const wlr_box tiled = view.geometry();
        const double ratio = tiled.width > 0 ? (cursor->x - tiled.x) / tiled.width : 0.5;
        const wlr_box restore = view.restore;
        view.setTile(Tile::None);
        view.setGeometry({int(cursor->x - ratio * restore.width), tiled.y, restore.width, restore.height});
    }
    const wlr_box geometry = view.geometry();
    m_grabbed = &view;
    m_cursorMode = CursorMode::Move;
    m_grabX = cursor->x - geometry.x;
    m_grabY = cursor->y - geometry.y;
    m_snapTarget = Tile::None;
    wlr_cursor_set_xcursor(cursor, xcursor, "grabbing");
}

void Server::beginResize(View& view, uint32_t edges)
{
    if (view.fullscreen || view.isTiled() || m_cursorMode != CursorMode::Passthrough)
        return;
    view.handled = true;
    const wlr_box geometry = view.geometry();
    m_grabbed = &view;
    m_cursorMode = CursorMode::Resize;
    m_resizeEdges = edges;
    m_grabBox = geometry;
    const double edgeX = geometry.x + ((edges & WLR_EDGE_RIGHT) ? geometry.width : 0);
    const double edgeY = geometry.y + ((edges & WLR_EDGE_BOTTOM) ? geometry.height : 0);
    m_grabX = cursor->x - edgeX;
    m_grabY = cursor->y - edgeY;
    wlr_cursor_set_xcursor(cursor, xcursor, wlr_xcursor_get_resize_name(static_cast<wlr_edges>(edges)));
}

void Server::updateMove()
{
    m_grabbed->moveTo(int(cursor->x - m_grabX), int(cursor->y - m_grabY));

    // Near the top of the screen the window will fill it, near a side it will fill that half.
    const Output* output = outputAt(cursor->x, cursor->y);
    if (!output)
        return;
    const wlr_box full = output->box();
    Tile target = Tile::None;
    if (cursor->y <= full.y + SnapEdge)
        target = Tile::Maximized;
    else if (cursor->x <= full.x + SnapEdge)
        target = Tile::Left;
    else if (cursor->x >= full.x + full.width - 1 - SnapEdge)
        target = Tile::Right;
    if (target != m_snapTarget) {
        m_snapTarget = target;
        showSnapPreview(target, usableArea(cursor->x, cursor->y));
    }
}

void Server::updateResize()
{
    const double edgeX = cursor->x - m_grabX;
    const double edgeY = cursor->y - m_grabY;
    int left = m_grabBox.x;
    int right = m_grabBox.x + m_grabBox.width;
    int top = m_grabBox.y;
    int bottom = m_grabBox.y + m_grabBox.height;
    const int minWidth = m_grabbed->minimumWidth();
    const int minHeight = m_grabbed->minimumHeight() + m_grabbed->decorationHeight();

    if (m_resizeEdges & WLR_EDGE_TOP)
        top = std::min(int(edgeY), bottom - minHeight);
    else if (m_resizeEdges & WLR_EDGE_BOTTOM)
        bottom = std::max(int(edgeY), top + minHeight);
    if (m_resizeEdges & WLR_EDGE_LEFT)
        left = std::min(int(edgeX), right - minWidth);
    else if (m_resizeEdges & WLR_EDGE_RIGHT)
        right = std::max(int(edgeX), left + minWidth);

    m_grabbed->setGeometry({left, top, right - left, bottom - top});
}

void Server::finishGrab()
{
    if (m_cursorMode == CursorMode::Move && m_grabbed && m_snapTarget != Tile::None)
        m_grabbed->setTile(m_snapTarget, true);
    showSnapPreview(Tile::None, {});
    m_snapTarget = Tile::None;
    m_cursorMode = CursorMode::Passthrough;
    m_grabbed = nullptr;
    wlr_cursor_set_xcursor(cursor, xcursor, "default");
}

void Server::showSnapPreview(Tile tile, const wlr_box& area)
{
    if (tile == Tile::None) {
        if (m_snapPreview)
            wlr_scene_node_set_enabled(&m_snapPreview->node, false);
        if (m_snapPreviewTimer) {
            wl_event_source_remove(m_snapPreviewTimer);
            m_snapPreviewTimer = nullptr;
        }
        return;
    }
    wlr_box box = area;
    if (tile == Tile::Left || tile == Tile::Right) {
        box.width = area.width / 2;
        if (tile == Tile::Right)
            box.x += area.width - box.width;
    }
    // The accent colour of the theme, see through; colours are premultiplied.
    constexpr float alpha = 0.3f;
    const float color[4] = {0x52 / 255.0f * alpha, 0x94 / 255.0f * alpha, 0xe2 / 255.0f * alpha, alpha};
    if (!m_snapPreview)
        m_snapPreview = wlr_scene_rect_create(layers.feedback, box.width, box.height, color);
    wlr_scene_rect_set_color(m_snapPreview, color);
    wlr_scene_node_set_enabled(&m_snapPreview->node, true);

    // It grows out of the window being dragged.
    m_snapPreviewFrom = m_grabbed ? m_grabbed->geometry() : box;
    m_snapPreviewTo = box;
    clock_gettime(CLOCK_MONOTONIC, &m_snapPreviewStart);
    if (!m_snapPreviewTimer) {
        m_snapPreviewTimer = wl_event_loop_add_timer(
            eventLoop,
            [](void* data) {
                static_cast<Server*>(data)->stepSnapPreview();
                return 0;
            },
            this);
    }
    stepSnapPreview();
}

void Server::stepSnapPreview()
{
    timespec now {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    const double elapsed = double(now.tv_sec - m_snapPreviewStart.tv_sec) * 1000
        + double(now.tv_nsec - m_snapPreviewStart.tv_nsec) / 1e6;
    const double t = windowAnimationTime > 0 ? std::min(1.0, elapsed / windowAnimationTime) : 1.0;
    const double eased = 1 - std::pow(1 - t, 3);
    const auto between = [eased](int a, int b) { return int(std::lround(a + (b - a) * eased)); };
    wlr_scene_rect_set_size(m_snapPreview, std::max(1, between(m_snapPreviewFrom.width, m_snapPreviewTo.width)),
        std::max(1, between(m_snapPreviewFrom.height, m_snapPreviewTo.height)));
    wlr_scene_node_set_position(&m_snapPreview->node, between(m_snapPreviewFrom.x, m_snapPreviewTo.x),
        between(m_snapPreviewFrom.y, m_snapPreviewTo.y));
    if (t < 1) {
        wl_event_source_timer_update(m_snapPreviewTimer, 8);
    } else {
        wl_event_source_remove(m_snapPreviewTimer);
        m_snapPreviewTimer = nullptr;
    }
}

} // namespace atlas
