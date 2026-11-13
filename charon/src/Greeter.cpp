#include "Greeter.hpp"

#include "Greetd.hpp"
#include "Lift.hpp"
#include "Power.hpp"
#include "Screen.hpp"
#include "Sessions.hpp"
#include "Users.hpp"

#include <Clock.hpp>
#include <Owned.hpp>
#include <Password.hpp>

#include "cursor-shape-v1-client-protocol.h"
#include "ext-idle-notify-v1-client-protocol.h"
// The layer shell's header names an argument "namespace", which C++ keeps for itself.
#pragma GCC diagnostic push
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wkeyword-macro"
#endif
#define namespace layer_namespace
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace
#pragma GCC diagnostic pop
#include "wlr-output-power-management-unstable-v1-client-protocol.h"

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace charon {
namespace {

using cerberus::FileDescriptor;
using cerberus::Owned;

constexpr int BlankAfter = 5 * 60 * 1000; // ms without a key or the pointer, until the screens go dark
constexpr int SuspendAfter = 15 * 60 * 1000; // and until a computer on its battery sleeps, as GDM's does

struct XkbDeleter {
    void operator()(xkb_context* context) const { xkb_context_unref(context); }
    void operator()(xkb_keymap* keymap) const { xkb_keymap_unref(keymap); }
    void operator()(xkb_state* state) const { xkb_state_unref(state); }
};

using Picture = Owned<cairo_surface_t, cairo_surface_destroy>;

// A picture file as a surface cairo can draw; nothing when it cannot be read.
Picture loadPicture(const std::string& path)
{
    if (path.empty())
        return Picture(nullptr);
    // Never larger than drawn, at twice the size for screens that are scaled.
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file_at_scale(path.c_str(), 192, 192, TRUE, nullptr);
    if (!pixbuf)
        return Picture(nullptr);
    const int width = gdk_pixbuf_get_width(pixbuf);
    const int height = gdk_pixbuf_get_height(pixbuf);
    const int channels = gdk_pixbuf_get_n_channels(pixbuf);
    const int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    const guchar* pixels = gdk_pixbuf_get_pixels(pixbuf);
    Picture surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height));
    cairo_surface_flush(surface.get());
    unsigned char* data = cairo_image_surface_get_data(surface.get());
    const int stride = cairo_image_surface_get_stride(surface.get());
    // From RGB(A) bytes to native-endian ARGB words, the colours multiplied by the alpha.
    for (int y = 0; y < height; ++y) {
        const guchar* from = pixels + y * rowstride;
        auto* to = reinterpret_cast<uint32_t*>(data + y * stride);
        for (int x = 0; x < width; ++x, from += channels) {
            const unsigned alpha = channels == 4 ? from[3] : 255;
            const auto premultiplied = [alpha](unsigned c) { return (c * alpha + 127) / 255; };
            to[x] = (alpha << 24) | (premultiplied(from[0]) << 16) | (premultiplied(from[1]) << 8)
                | premultiplied(from[2]);
        }
    }
    cairo_surface_mark_dirty(surface.get());
    g_object_unref(pixbuf);
    return surface;
}

// PAM's question as a placeholder: "Password: " as "Password".
std::string questionText(std::string text)
{
    while (!text.empty() && (text.back() == ' ' || text.back() == ':' || text.back() == '\n'))
        text.pop_back();
    return text.empty() ? std::string("Password") : text;
}

// Whether logging in through greetd unlocks the keyring, as it does through GDM: only when its
// PAM service has the keyring's module, or oo7's. Without a keyring module installed, there is
// nothing to unlock.
bool keyringLeftLocked()
{
    if (access("/usr/lib/security/pam_gnome_keyring.so", F_OK) != 0)
        return false;
    std::ifstream service("/etc/pam.d/greetd");
    std::string line;
    while (std::getline(service, line)) {
        const auto start = line.find_first_not_of(" \t-");
        if (start == std::string::npos || line[start] == '#')
            continue;
        if (line.contains("pam_gnome_keyring.so") || line.contains("pam_oo7.so"))
            return false;
    }
    return true;
}

class Greeter;

struct Output {
    Greeter* owner = nullptr;
    uint32_t name = 0;
    Owned<wl_output, wl_output_destroy> output;
    Owned<wl_surface, wl_surface_destroy> surface;
    Owned<zwlr_layer_surface_v1, zwlr_layer_surface_v1_destroy> layer;
    Owned<zwlr_output_power_v1, zwlr_output_power_v1_destroy> power;
    int scale = 1;
    int width = 0;
    int height = 0;
};

struct Buffer {
    Greeter* owner = nullptr;
    Owned<wl_buffer, wl_buffer_destroy> buffer;
    void* data = nullptr;
    std::size_t size = 0;
    int width = 0;
    int height = 0;
    bool busy = false; // with the compositor, to be drawn on again once it lets go

    ~Buffer()
    {
        if (data)
            munmap(data, size);
    }
};

class Greeter {
public:
    Greeter() = default;
    Greeter(const Greeter&) = delete;
    Greeter& operator=(const Greeter&) = delete;
    ~Greeter();

    int run();

private:
    static void global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
    static void globalRemove(void* data, wl_registry* registry, uint32_t name);

    void addOutput(wl_output* output, uint32_t name);
    void showOn(Output& output);
    void draw(Output& output);
    void drawAll();
    Output* mainOutput() const;
    void released(Buffer& buffer);

    void setUpSeat(uint32_t capabilities);
    void setUpIdle();
    void keymap(int fd, uint32_t size);
    void key(uint32_t key, bool pressed);
    void press(xkb_keysym_t sym, uint32_t key);
    void startRepeat(uint32_t key);
    void stopRepeat();
    void updateCaret(bool restart);
    void tickClock();
    // Shows what is under the curtain, or the curtain again.
    void setCurtain(bool down);
    void pointerAt(wl_surface* surface, double x, double y);
    void click();
    void setScreensOn(bool on);

    // The login itself.
    void pickUser(const User& user);
    void askName();
    void back();
    void submit();
    void replied(const Reply& reply);
    void startChosenSession();
    void clearAnswer();
    void choose(Target target);
    int sessionFor(const User& user) const;

    wl_display* m_display = nullptr;
    Owned<wl_registry, wl_registry_destroy> m_registry;
    Owned<wl_compositor, wl_compositor_destroy> m_compositor;
    Owned<wl_shm, wl_shm_destroy> m_shm;
    Owned<wl_seat, wl_seat_destroy> m_seat;
    Owned<wl_keyboard, wl_keyboard_release> m_keyboard;
    Owned<wl_pointer, wl_pointer_release> m_pointer;
    Owned<zwlr_layer_shell_v1, zwlr_layer_shell_v1_destroy> m_layerShell;
    Owned<wp_cursor_shape_manager_v1, wp_cursor_shape_manager_v1_destroy> m_cursorShapes;
    Owned<wp_cursor_shape_device_v1, wp_cursor_shape_device_v1_destroy> m_cursorShape;
    Owned<ext_idle_notifier_v1, ext_idle_notifier_v1_destroy> m_idleNotifier;
    Owned<ext_idle_notification_v1, ext_idle_notification_v1_destroy> m_blankIdle;
    Owned<ext_idle_notification_v1, ext_idle_notification_v1_destroy> m_suspendIdle;
    Owned<zwlr_output_power_manager_v1, zwlr_output_power_manager_v1_destroy> m_powerManager;
    std::vector<std::unique_ptr<Output>> m_outputs;
    std::vector<std::unique_ptr<Buffer>> m_buffers;
    Output* m_main = nullptr; // where the pointer last came
    wl_surface* m_pointerSurface = nullptr;
    double m_pointerX = 0;
    double m_pointerY = 0;
    std::vector<Target> m_targets; // of the main output

    std::unique_ptr<xkb_context, XkbDeleter> m_xkb {xkb_context_new(XKB_CONTEXT_NO_FLAGS)};
    std::unique_ptr<xkb_keymap, XkbDeleter> m_keymap;
    std::unique_ptr<xkb_state, XkbDeleter> m_state;
    int m_repeatRate = 25;
    int m_repeatDelay = 600;
    uint32_t m_repeatKey = 0;
    FileDescriptor m_repeatTimer;
    FileDescriptor m_clockTimer;
    FileDescriptor m_caretTimer;
    bool m_blinking = false;
    bool m_pulsing = false;
    cerberus::Lift m_lift; // of the curtain, as it goes up and down
    int m_idle = 0; // seconds with nothing typed while the curtain is up, until it falls

    Greetd m_greetd;
    // What was asked of greetd, for what its reply means.
    enum class Request { None, Login, Start, Cancel };
    Request m_request = Request::None;
    bool m_backWanted = false; // asked to go back while greetd was busy
    bool m_retry = false; // ask again for the same user once the session is cancelled
    std::vector<User> m_users;
    std::vector<Picture> m_pictures; // of m_users, by index
    User m_user; // being logged in
    std::vector<Session> m_sessions;
    std::vector<PowerAction> m_power;
    Memory m_memory {TDE_GREETER_DIR "/state/charon"};
    cerberus::Password m_secret;
    Screen m_screen;
    bool m_started = false;
    bool m_blank = false;
};

Greeter::~Greeter()
{
    // Everything of the connection goes before it does.
    m_outputs.clear();
    m_buffers.clear();
    m_blankIdle.reset();
    m_suspendIdle.reset();
    m_idleNotifier.reset();
    m_powerManager.reset();
    m_cursorShape.reset();
    m_cursorShapes.reset();
    m_pointer.reset();
    m_keyboard.reset();
    m_seat.reset();
    m_layerShell.reset();
    m_shm.reset();
    m_compositor.reset();
    m_registry.reset();
    if (m_display)
        wl_display_disconnect(m_display);
}

void Greeter::global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
{
    auto* self = static_cast<Greeter*>(data);
    const auto bind = [&]<typename T>(const wl_interface& wanted, uint32_t most) {
        return static_cast<T*>(wl_registry_bind(registry, name, &wanted, std::min(version, most)));
    };
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        self->m_compositor.reset(bind.operator()<wl_compositor>(wl_compositor_interface, 4));
    } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        self->m_shm.reset(bind.operator()<wl_shm>(wl_shm_interface, 1));
    } else if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        self->m_layerShell.reset(bind.operator()<zwlr_layer_shell_v1>(zwlr_layer_shell_v1_interface, 3));
    } else if (std::strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0) {
        self->m_cursorShapes.reset(
            bind.operator()<wp_cursor_shape_manager_v1>(wp_cursor_shape_manager_v1_interface, 1));
    } else if (std::strcmp(interface, ext_idle_notifier_v1_interface.name) == 0) {
        self->m_idleNotifier.reset(bind.operator()<ext_idle_notifier_v1>(ext_idle_notifier_v1_interface, 1));
    } else if (std::strcmp(interface, zwlr_output_power_manager_v1_interface.name) == 0) {
        self->m_powerManager.reset(
            bind.operator()<zwlr_output_power_manager_v1>(zwlr_output_power_manager_v1_interface, 1));
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0 && !self->m_seat) {
        static const wl_seat_listener listener {
            .capabilities
            = [](void* data, wl_seat*, uint32_t capabilities) { static_cast<Greeter*>(data)->setUpSeat(capabilities); },
            .name = [](void*, wl_seat*, const char*) { },
        };
        self->m_seat.reset(bind.operator()<wl_seat>(wl_seat_interface, 5));
        wl_seat_add_listener(self->m_seat.get(), &listener, self);
    } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
        self->addOutput(bind.operator()<wl_output>(wl_output_interface, 3), name);
    }
}

void Greeter::globalRemove(void* data, wl_registry*, uint32_t name)
{
    auto* self = static_cast<Greeter*>(data);
    const bool wasMain = self->m_main && self->m_main->name == name;
    if (wasMain)
        self->m_main = nullptr;
    const auto gone = std::ranges::find(self->m_outputs, name, [](const auto& output) { return output->name; });
    if (gone != self->m_outputs.end() && (*gone)->surface.get() == self->m_pointerSurface)
        self->m_pointerSurface = nullptr;
    std::erase_if(self->m_outputs, [name](const auto& output) { return output->name == name; });
    // The login moves to a screen still there.
    if (wasMain)
        self->drawAll();
}

void Greeter::addOutput(wl_output* wlOutput, uint32_t name)
{
    auto output = std::make_unique<Output>();
    output->owner = this;
    output->name = name;
    output->output.reset(wlOutput);
    static const wl_output_listener listener {
        .geometry
        = [](void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) { },
        .mode = [](void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) { },
        .done =
            [](void* data, wl_output*) {
                auto* output = static_cast<Output*>(data);
                if (output->layer && output->width > 0)
                    output->owner->draw(*output);
            },
        .scale
        = [](void* data, wl_output*, int32_t factor) { static_cast<Output*>(data)->scale = std::max(factor, 1); },
        .name = [](void*, wl_output*, const char*) { },
        .description = [](void*, wl_output*, const char*) { },
    };
    wl_output_add_listener(wlOutput, &listener, output.get());
    Output& added = *m_outputs.emplace_back(std::move(output));
    if (m_layerShell)
        showOn(added);
}

void Greeter::showOn(Output& output)
{
    output.surface.reset(wl_compositor_create_surface(m_compositor.get()));
    output.layer.reset(zwlr_layer_shell_v1_get_layer_surface(m_layerShell.get(), output.surface.get(),
        output.output.get(), ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "tde-charon"));
    static const zwlr_layer_surface_v1_listener listener {
        .configure =
            [](void* data, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t width, uint32_t height) {
                auto* output = static_cast<Output*>(data);
                zwlr_layer_surface_v1_ack_configure(layer, serial);
                output->width = int(width);
                output->height = int(height);
                output->owner->draw(*output);
            },
        .closed =
            [](void* data, zwlr_layer_surface_v1*) {
                // Its screen is going; the output's removal takes the rest.
                auto* output = static_cast<Output*>(data);
                output->width = 0;
            },
    };
    zwlr_layer_surface_v1_add_listener(output.layer.get(), &listener, &output);
    zwlr_layer_surface_v1_set_anchor(output.layer.get(),
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
            | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(output.layer.get(), -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        output.layer.get(), ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    wl_surface_commit(output.surface.get());
    if (m_powerManager)
        output.power.reset(zwlr_output_power_manager_v1_get_output_power(m_powerManager.get(), output.output.get()));
}

void Greeter::draw(Output& output)
{
    if (output.width <= 0 || output.height <= 0 || !m_shm)
        return;
    const int width = output.width * output.scale;
    const int height = output.height * output.scale;
    const int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    const std::size_t size = std::size_t(stride) * std::size_t(height);

    // A picture the compositor is done with is drawn on again: one made anew for each frame,
    // in memory fresh from the kernel, and the compositor taking each in as new, made the
    // curtain stutter as it went up.
    const auto spare = std::ranges::find_if(
        m_buffers, [&](const auto& b) { return !b->busy && b->width == width && b->height == height; });
    Buffer* buffer = spare != m_buffers.end() ? spare->get() : nullptr;
    if (!buffer) {
        const FileDescriptor fd(memfd_create("tde-charon", MFD_CLOEXEC));
        if (!fd || ftruncate(fd.get(), off_t(size)) < 0)
            return;
        auto made = std::make_unique<Buffer>();
        made->owner = this;
        made->size = size;
        made->width = width;
        made->height = height;
        made->data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
        if (made->data == MAP_FAILED) {
            made->data = nullptr;
            return;
        }
        const Owned<wl_shm_pool, wl_shm_pool_destroy> pool(wl_shm_create_pool(m_shm.get(), fd.get(), int(size)));
        made->buffer.reset(wl_shm_pool_create_buffer(pool.get(), 0, width, height, stride, WL_SHM_FORMAT_ARGB8888));
        static const wl_buffer_listener listener {
            .release =
                [](void* data, wl_buffer*) {
                    auto* buffer = static_cast<Buffer*>(data);
                    buffer->owner->released(*buffer);
                },
        };
        wl_buffer_add_listener(made->buffer.get(), &listener, made.get());
        buffer = made.get();
        m_buffers.push_back(std::move(made));
    }
    buffer->busy = true;
    {
        const Owned<cairo_surface_t, cairo_surface_destroy> surface(cairo_image_surface_create_for_data(
            static_cast<unsigned char*>(buffer->data), CAIRO_FORMAT_ARGB32, width, height, stride));
        const Owned<cairo_t, cairo_destroy> cr(cairo_create(surface.get()));
        cairo_scale(cr.get(), output.scale, output.scale);
        const bool main = &output == mainOutput();
        if (main)
            m_targets.clear();
        m_screen.lift = m_lift.value();
        charon::draw(cr.get(), output.width, output.height, m_screen, main, main ? &m_targets : nullptr);
    }
    wl_surface_set_buffer_scale(output.surface.get(), output.scale);
    wl_surface_attach(output.surface.get(), buffer->buffer.get(), 0, 0);
    wl_surface_damage_buffer(output.surface.get(), 0, 0, width, height);
    wl_surface_commit(output.surface.get());
}

void Greeter::drawAll()
{
    for (const auto& output : m_outputs) {
        if (output->layer)
            draw(*output);
    }
}

Output* Greeter::mainOutput() const
{
    if (m_main)
        return m_main;
    for (const auto& output : m_outputs) {
        if (output->layer)
            return output.get();
    }
    return nullptr;
}

void Greeter::released(Buffer& buffer)
{
    buffer.busy = false;
    // Kept while some screen is still of its size, for the next picture there.
    const bool needed = std::ranges::any_of(m_outputs, [&](const auto& output) {
        return output->width * output->scale == buffer.width && output->height * output->scale == buffer.height;
    });
    if (!needed)
        std::erase_if(m_buffers, [&](const auto& b) { return b.get() == &buffer; });
}

void Greeter::setUpSeat(uint32_t capabilities)
{
    if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && m_keyboard) {
        m_keyboard.reset();
        stopRepeat();
    }
    if (!(capabilities & WL_SEAT_CAPABILITY_POINTER)) {
        m_cursorShape.reset();
        m_pointer.reset();
    }
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !m_keyboard) {
        static const wl_keyboard_listener listener {
            .keymap =
                [](void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
                    const FileDescriptor keymap(fd);
                    if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
                        static_cast<Greeter*>(data)->keymap(keymap.get(), size);
                },
            .enter =
                [](void* data, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {
                    auto* self = static_cast<Greeter*>(data);
                    self->m_screen.focused = true;
                    self->updateCaret(true);
                    self->drawAll();
                },
            .leave =
                [](void* data, wl_keyboard*, uint32_t, wl_surface*) {
                    auto* self = static_cast<Greeter*>(data);
                    self->stopRepeat();
                    self->m_screen.focused = false;
                    self->updateCaret(true);
                    self->drawAll();
                },
            .key
            = [](void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t key,
                  uint32_t state) { static_cast<Greeter*>(data)->key(key, state == WL_KEYBOARD_KEY_STATE_PRESSED); },
            .modifiers =
                [](void* data, wl_keyboard*, uint32_t, uint32_t depressed, uint32_t latched, uint32_t locked,
                    uint32_t group) {
                    auto* self = static_cast<Greeter*>(data);
                    if (!self->m_state)
                        return;
                    xkb_state_update_mask(self->m_state.get(), depressed, latched, locked, 0, 0, group);
                    const bool capsLock
                        = xkb_state_mod_name_is_active(self->m_state.get(), XKB_MOD_NAME_CAPS, XKB_STATE_MODS_LOCKED)
                        > 0;
                    // The layout, when there are several to switch between.
                    std::string layout;
                    if (xkb_keymap_num_layouts(self->m_keymap.get()) > 1) {
                        const char* name = xkb_keymap_layout_get_name(self->m_keymap.get(), group);
                        layout = name ? name : "";
                    }
                    if (capsLock != self->m_screen.capsLock || layout != self->m_screen.keyboardLayout) {
                        self->m_screen.capsLock = capsLock;
                        self->m_screen.keyboardLayout = layout;
                        self->drawAll();
                    }
                },
            .repeat_info =
                [](void* data, wl_keyboard*, int32_t rate, int32_t delay) {
                    auto* self = static_cast<Greeter*>(data);
                    self->m_repeatRate = rate;
                    self->m_repeatDelay = delay;
                },
        };
        m_keyboard.reset(wl_seat_get_keyboard(m_seat.get()));
        wl_keyboard_add_listener(m_keyboard.get(), &listener, this);
    }
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !m_pointer) {
        // The seat is bound at version 5, so the events of later versions never come.
        static const wl_pointer_listener listener = [] {
            wl_pointer_listener events {};
            events.enter
                = [](void* data, wl_pointer*, uint32_t serial, wl_surface* surface, wl_fixed_t x, wl_fixed_t y) {
                      auto* self = static_cast<Greeter*>(data);
                      if (self->m_cursorShape)
                          wp_cursor_shape_device_v1_set_shape(
                              self->m_cursorShape.get(), serial, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
                      self->pointerAt(surface, wl_fixed_to_double(x), wl_fixed_to_double(y));
                  };
            events.leave = [](void*, wl_pointer*, uint32_t, wl_surface*) { };
            events.motion = [](void* data, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) {
                auto* self = static_cast<Greeter*>(data);
                self->pointerAt(self->m_pointerSurface, wl_fixed_to_double(x), wl_fixed_to_double(y));
            };
            events.button = [](void* data, wl_pointer*, uint32_t, uint32_t, uint32_t, uint32_t state) {
                if (state == WL_POINTER_BUTTON_STATE_PRESSED)
                    static_cast<Greeter*>(data)->click();
            };
            events.axis = [](void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t) { };
            events.frame = [](void*, wl_pointer*) { };
            events.axis_source = [](void*, wl_pointer*, uint32_t) { };
            events.axis_stop = [](void*, wl_pointer*, uint32_t, uint32_t) { };
            events.axis_discrete = [](void*, wl_pointer*, uint32_t, int32_t) { };
            return events;
        }();
        m_pointer.reset(wl_seat_get_pointer(m_seat.get()));
        wl_pointer_add_listener(m_pointer.get(), &listener, this);
        if (m_cursorShapes)
            m_cursorShape.reset(wp_cursor_shape_manager_v1_get_pointer(m_cursorShapes.get(), m_pointer.get()));
    }
}

void Greeter::setUpIdle()
{
    if (!m_idleNotifier || !m_seat)
        return;
    // Left alone, the screens go dark, as GDM's do, and come back with the first key or move.
    static const ext_idle_notification_v1_listener blank {
        .idled = [](void* data, ext_idle_notification_v1*) { static_cast<Greeter*>(data)->setScreensOn(false); },
        .resumed = [](void* data, ext_idle_notification_v1*) { static_cast<Greeter*>(data)->setScreensOn(true); },
    };
    m_blankIdle.reset(ext_idle_notifier_v1_get_idle_notification(m_idleNotifier.get(), BlankAfter, m_seat.get()));
    ext_idle_notification_v1_add_listener(m_blankIdle.get(), &blank, this);
    // Left longer on its battery, the computer sleeps.
    static const ext_idle_notification_v1_listener sleep {
        .idled =
            [](void* data, ext_idle_notification_v1*) {
                auto* self = static_cast<Greeter*>(data);
                if (std::ranges::contains(self->m_power, PowerAction::Suspend) && onBattery())
                    runPowerAction(PowerAction::Suspend);
            },
        .resumed = [](void*, ext_idle_notification_v1*) { },
    };
    m_suspendIdle.reset(ext_idle_notifier_v1_get_idle_notification(m_idleNotifier.get(), SuspendAfter, m_seat.get()));
    ext_idle_notification_v1_add_listener(m_suspendIdle.get(), &sleep, this);
}

void Greeter::setScreensOn(bool on)
{
    m_blank = !on;
    for (const auto& output : m_outputs) {
        if (output->power)
            zwlr_output_power_v1_set_mode(
                output->power.get(), on ? ZWLR_OUTPUT_POWER_V1_MODE_ON : ZWLR_OUTPUT_POWER_V1_MODE_OFF);
    }
}

void Greeter::keymap(int fd, uint32_t size)
{
    void* text = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (text == MAP_FAILED)
        return;
    m_keymap.reset(xkb_keymap_new_from_string(
        m_xkb.get(), static_cast<const char*>(text), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS));
    munmap(text, size);
    m_state.reset(m_keymap ? xkb_state_new(m_keymap.get()) : nullptr);
}

void Greeter::key(uint32_t key, bool pressed)
{
    if (!m_state)
        return;
    if (!pressed) {
        if (key == m_repeatKey)
            stopRepeat();
        return;
    }
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(m_state.get(), key + 8);
    press(sym, key);
    if (sym != XKB_KEY_Escape && sym != XKB_KEY_Return && xkb_keymap_key_repeats(m_keymap.get(), key + 8))
        startRepeat(key);
}

void Greeter::press(xkb_keysym_t sym, uint32_t key)
{
    if (m_started)
        return;
    const bool enter = sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter;
    const bool up = sym == XKB_KEY_Up || sym == XKB_KEY_KP_Up;
    const bool down = sym == XKB_KEY_Down || sym == XKB_KEY_KP_Down;
    m_idle = 0;
    // The first key lifts the curtain; one that types something types it into the answer too.
    if (m_screen.curtain) {
        setCurtain(false);
        if (enter || up || down || sym == XKB_KEY_BackSpace || sym == XKB_KEY_Escape)
            return;
    }

    // An open menu has the keys.
    if (m_screen.powerOpen || m_screen.sessionsOpen) {
        const int count = int(m_screen.powerOpen ? m_screen.powerActions.size() : m_screen.sessions.size());
        if (sym == XKB_KEY_Escape) {
            m_screen.powerOpen = m_screen.sessionsOpen = false;
        } else if (up || down) {
            const int from = m_screen.menuSelected < 0 ? (up ? count : -1) : m_screen.menuSelected;
            m_screen.menuSelected = std::clamp(from + (up ? -1 : 1), 0, count - 1);
        } else if (enter && m_screen.menuSelected >= 0) {
            choose({m_screen.powerOpen ? Target::Kind::PowerItem : Target::Kind::SessionItem, m_screen.menuSelected});
            return;
        }
        drawAll();
        return;
    }

    if (m_screen.stage == Screen::Stage::Users) {
        const int count = int(m_users.size());
        if (sym == XKB_KEY_Escape) {
            setCurtain(true);
            return;
        }
        if (up || down) {
            m_screen.selected = std::clamp(m_screen.selected + (up ? -1 : 1), 0, std::max(0, count - 1));
        } else if (enter && m_screen.selected < count) {
            pickUser(m_users[std::size_t(m_screen.selected)]);
            return;
        }
        drawAll();
        return;
    }

    // While greetd has the answer, keys wait; Escape goes back once it replies.
    if (m_screen.checking) {
        if (sym == XKB_KEY_Escape)
            back();
        return;
    }
    const bool control = xkb_state_mod_name_is_active(m_state.get(), XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
    // All of the answer, selected: what is typed next takes its place, and Backspace or Delete
    // takes it away.
    if (std::exchange(m_screen.answerSelected, false) && !control) {
        if (sym == XKB_KEY_BackSpace || sym == XKB_KEY_Delete || sym == XKB_KEY_KP_Delete) {
            clearAnswer();
            sym = XKB_KEY_NoSymbol;
        } else if (!enter && sym != XKB_KEY_Escape) {
            clearAnswer();
        }
    }
    switch (sym) {
    case XKB_KEY_NoSymbol:
        break;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        submit();
        return;
    case XKB_KEY_BackSpace:
        if (m_screen.secret) {
            m_secret.removeLast();
        } else if (!m_screen.text.empty()) {
            // The last character, which may be several bytes.
            std::size_t end = m_screen.text.size() - 1;
            while (end > 0 && (static_cast<unsigned char>(m_screen.text[end]) & 0xc0) == 0x80)
                --end;
            m_screen.text.erase(end);
        }
        break;
    case XKB_KEY_Escape:
        // What was typed goes first; with nothing typed, back to the list.
        if (m_secret.empty() && m_screen.text.empty()) {
            back();
            return;
        }
        clearAnswer();
        break;
    default: {
        if (control) {
            if (sym == XKB_KEY_u)
                clearAnswer();
            else if (sym == XKB_KEY_a)
                m_screen.answerSelected = !m_secret.empty() || !m_screen.text.empty();
            break;
        }
        char text[64];
        const int length = xkb_state_key_get_utf8(m_state.get(), key + 8, text, sizeof(text));
        if (length > 0 && static_cast<unsigned char>(text[0]) >= 0x20 && text[0] != 0x7f) {
            if (m_screen.secret)
                m_secret.append(std::string_view(text, std::size_t(length)));
            else if (m_screen.text.size() < 512)
                m_screen.text.append(text, std::size_t(length));
        }
        explicit_bzero(text, sizeof(text));
        break;
    }
    }
    // Typing again, what went wrong goes.
    m_screen.wrong = false;
    std::erase_if(m_screen.messages, [](const Screen::Line& line) { return line.error; });
    m_screen.typed = m_secret.length();
    updateCaret(true);
    drawAll();
}

void Greeter::clearAnswer()
{
    m_screen.answerSelected = false;
    m_secret.clear();
    explicit_bzero(m_screen.text.data(), m_screen.text.size());
    m_screen.text.clear();
    m_screen.typed = 0;
}

void Greeter::startRepeat(uint32_t key)
{
    if (m_repeatRate <= 0)
        return;
    m_repeatKey = key;
    itimerspec timer {};
    timer.it_value.tv_sec = m_repeatDelay / 1000;
    timer.it_value.tv_nsec = (m_repeatDelay % 1000) * 1'000'000L;
    timer.it_interval.tv_nsec = 1'000'000'000L / m_repeatRate;
    timerfd_settime(m_repeatTimer.get(), 0, &timer, nullptr);
}

void Greeter::stopRepeat()
{
    m_repeatKey = 0;
    const itimerspec off {};
    timerfd_settime(m_repeatTimer.get(), 0, &off, nullptr);
}

void Greeter::updateCaret(bool restart)
{
    const bool checking = m_screen.checking;
    const bool typing = m_screen.stage != Screen::Stage::Users;
    const bool wanted = typing && !m_screen.curtain && (m_screen.focused || checking);
    if (restart)
        m_screen.caret = true;
    if (wanted == m_blinking && checking == m_pulsing && !restart)
        return;
    m_blinking = wanted;
    m_pulsing = checking;
    itimerspec timer {};
    if (wanted) {
        timer.it_value.tv_nsec = checking ? 200'000'000L : 530'000'000L;
        timer.it_interval.tv_nsec = timer.it_value.tv_nsec;
    }
    timerfd_settime(m_caretTimer.get(), 0, &timer, nullptr);
}

void Greeter::tickClock()
{
    // Left alone with nothing typed, all goes behind the curtain again.
    const bool menus = m_screen.powerOpen || m_screen.sessionsOpen;
    if (!m_screen.curtain && m_secret.empty() && m_screen.text.empty() && !m_screen.checking && !menus
        && ++m_idle >= 30)
        setCurtain(true);
    std::string clock = cerberus::formatted("%a %-d %b") + "  " + cerberus::timeText(false);
    std::string time = cerberus::timeText(false);
    std::string date = cerberus::formatted("%A, %-d %B");
    if (clock != m_screen.clock || time != m_screen.time || date != m_screen.date) {
        m_screen.clock = std::move(clock);
        m_screen.time = std::move(time);
        m_screen.date = std::move(date);
        drawAll();
    }
}

void Greeter::setCurtain(bool down)
{
    if (m_screen.curtain == down)
        return;
    m_screen.curtain = down;
    m_screen.answerSelected = false;
    m_screen.powerOpen = m_screen.sessionsOpen = false;
    m_screen.hover = {};
    m_idle = 0;
    m_lift.set(!down);
    updateCaret(true);
    drawAll();
}

void Greeter::pointerAt(wl_surface* surface, double x, double y)
{
    m_pointerSurface = surface;
    m_pointerX = x;
    m_pointerY = y;
    // The login goes to the screen the pointer comes to.
    for (const auto& output : m_outputs) {
        if (output->surface.get() == surface && output.get() != mainOutput()) {
            m_main = output.get();
            drawAll();
            break;
        }
    }
    Target hover;
    if (mainOutput() && surface == mainOutput()->surface.get()) {
        for (auto it = m_targets.rbegin(); it != m_targets.rend(); ++it) {
            if (it->contains(x, y)) {
                hover = *it;
                break;
            }
        }
    }
    if (!hover.same(m_screen.hover)) {
        m_screen.hover = hover;
        drawAll();
    }
}

void Greeter::click()
{
    m_idle = 0;
    // A click lifts the curtain, and does nothing more.
    if (m_screen.curtain) {
        setCurtain(false);
        return;
    }
    const Target target = m_screen.hover;
    // A click anywhere else closes a menu.
    const bool inMenu = target.kind == Target::Kind::PowerItem || target.kind == Target::Kind::SessionItem;
    const bool opener = target.kind == Target::Kind::Power || target.kind == Target::Kind::Session;
    if ((m_screen.powerOpen || m_screen.sessionsOpen) && !inMenu && !opener) {
        m_screen.powerOpen = m_screen.sessionsOpen = false;
        drawAll();
        return;
    }
    choose(target);
}

void Greeter::choose(Target target)
{
    if (m_started)
        return;
    switch (target.kind) {
    case Target::Kind::None:
        return;
    case Target::Kind::User:
        if (target.index < int(m_users.size()) && !m_screen.checking) {
            m_screen.selected = target.index;
            pickUser(m_users[std::size_t(target.index)]);
        }
        return;
    case Target::Kind::NotListed:
        askName();
        return;
    case Target::Kind::Back:
        back();
        return;
    case Target::Kind::Session:
        m_screen.sessionsOpen = !m_screen.sessionsOpen;
        m_screen.powerOpen = false;
        m_screen.menuSelected = m_screen.session;
        break;
    case Target::Kind::SessionItem:
        m_screen.session = target.index;
        m_screen.sessionsOpen = false;
        break;
    case Target::Kind::Power:
        m_screen.powerOpen = !m_screen.powerOpen;
        m_screen.sessionsOpen = false;
        m_screen.menuSelected = -1;
        break;
    case Target::Kind::PowerItem:
        m_screen.powerOpen = false;
        drawAll();
        if (target.index < int(m_power.size()))
            runPowerAction(m_power[std::size_t(target.index)]);
        return;
    }
    m_screen.hover = {};
    drawAll();
}

int Greeter::sessionFor(const User& user) const
{
    // The one they chose last here, or else the one GDM remembers for them, or else TDE.
    for (const std::string& id : {m_memory.sessionOf(user.name), user.session, std::string("tde")}) {
        const auto it = std::ranges::find(m_sessions, id, &Session::id);
        if (!id.empty() && it != m_sessions.end())
            return int(it - m_sessions.begin());
    }
    return 0;
}

void Greeter::pickUser(const User& user)
{
    m_user = user;
    const auto listed = std::ranges::find(m_users, user.name, &User::name);
    m_screen.stage = Screen::Stage::Prompt;
    m_screen.person.name = user.realName;
    m_screen.person.picture
        = listed == m_users.end() ? nullptr : m_pictures[std::size_t(listed - m_users.begin())].get();
    m_screen.session = sessionFor(user);
    m_screen.question.clear();
    m_screen.messages.clear();
    m_screen.wrong = false;
    m_screen.hover = {};
    clearAnswer();
    m_screen.checking = true;
    if (m_greetd.waiting()) {
        // Still cancelling the last one: this one is asked for once that is done.
        m_retry = true;
    } else {
        m_request = Request::Login;
        if (!m_greetd.createSession(user.name))
            m_screen.messages.push_back({"The login service cannot be reached", true});
    }
    updateCaret(true);
    drawAll();
}

void Greeter::askName()
{
    m_screen.stage = Screen::Stage::Username;
    m_screen.question = "Username";
    m_screen.secret = false;
    m_screen.messages.clear();
    m_screen.wrong = false;
    m_screen.checking = false;
    m_screen.hover = {};
    clearAnswer();
    updateCaret(true);
    drawAll();
}

void Greeter::back()
{
    if (m_screen.stage == Screen::Stage::Users)
        return;
    if (m_screen.stage == Screen::Stage::Prompt) {
        // What PAM asks is given up; greetd is told once it has replied to what it has.
        m_retry = false;
        if (m_greetd.waiting()) {
            m_backWanted = true;
        } else if (m_request != Request::Cancel) {
            m_request = Request::Cancel;
            m_greetd.cancelSession();
        }
    }
    clearAnswer();
    m_screen.stage = Screen::Stage::Users;
    m_screen.checking = false;
    m_screen.messages.clear();
    m_screen.wrong = false;
    m_screen.sessionsOpen = false;
    m_screen.hover = {};
    updateCaret(true);
    drawAll();
}

void Greeter::submit()
{
    if (m_screen.stage == Screen::Stage::Username) {
        // Leading and trailing spaces are no part of a name.
        std::string name = m_screen.text;
        name.erase(0, name.find_first_not_of(' '));
        name.erase(name.find_last_not_of(' ') + 1);
        if (!name.empty())
            pickUser(findUser(name));
        return;
    }
    if (m_screen.question.empty() || m_greetd.waiting())
        return;
    std::string answer = m_screen.secret ? std::string(m_secret.data()) : m_screen.text;
    clearAnswer();
    m_screen.checking = true;
    m_screen.pulse = 0;
    m_request = Request::Login;
    m_greetd.answer(&answer);
    stopRepeat();
    updateCaret(true);
    drawAll();
}

void Greeter::startChosenSession()
{
    if (m_sessions.empty()) {
        m_screen.checking = false;
        m_screen.messages = {{"There is no session to start", true}};
        return;
    }
    const Session& session = m_sessions[std::size_t(std::clamp(m_screen.session, 0, int(m_sessions.size()) - 1))];
    m_memory.loggedIn(m_user.name, session.id);
    m_request = Request::Start;
    m_greetd.startSession(sessionCommand(session, m_user.shell), sessionEnvironment(session));
}

void Greeter::replied(const Reply& reply)
{
    const Request request = std::exchange(m_request, Request::None);
    // Gone back meanwhile: what it replied to is given up.
    if (m_backWanted) {
        m_backWanted = false;
        m_request = Request::Cancel;
        m_greetd.cancelSession();
        return;
    }
    if (request == Request::Cancel) {
        // Another user was picked while it cancelled, or the same one is to be asked again.
        if (m_retry) {
            m_retry = false;
            m_request = Request::Login;
            m_greetd.createSession(m_user.name);
        }
        return;
    }
    if (request == Request::Start) {
        if (reply.kind == Reply::Kind::Success) {
            // Greetd starts the session once the login screen is gone.
            m_started = true;
            return;
        }
        m_screen.checking = false;
        m_screen.messages = {{reply.text.empty() ? "The session cannot be started" : reply.text, true}};
        m_retry = true;
        m_request = Request::Cancel;
        m_greetd.cancelSession();
        drawAll();
        return;
    }

    switch (reply.kind) {
    case Reply::Kind::Question:
        m_screen.checking = false;
        m_screen.question = questionText(reply.text);
        m_screen.secret = reply.type == "secret";
        clearAnswer();
        break;
    case Reply::Kind::Message:
        // Said, and acknowledged at once; what comes after it follows.
        m_screen.messages.push_back({reply.text, reply.type == "error"});
        m_request = Request::Login;
        m_greetd.answer(nullptr);
        break;
    case Reply::Kind::Success:
        m_screen.messages.clear();
        startChosenSession();
        break;
    case Reply::Kind::Error:
        m_screen.checking = false;
        m_screen.wrong = true;
        m_screen.messages = {{failureText(reply), true}};
        // Asked again from the start, for the same user.
        m_retry = true;
        m_request = Request::Cancel;
        m_greetd.cancelSession();
        break;
    }
    updateCaret(true);
    drawAll();
}

int Greeter::run()
{
    if (!m_greetd.connect()) {
        std::fprintf(stderr, "tde-charon: greetd is not there to log in with (GREETD_SOCK is not set)\n");
        return 1;
    }
    m_users = findUsers();
    for (const User& user : m_users) {
        m_pictures.push_back(loadPicture(user.icon));
        m_screen.people.push_back({user.realName, m_pictures.back().get()});
    }
    m_sessions = findSessions(access("/usr/bin/startx", X_OK) == 0);
    for (const Session& session : m_sessions)
        m_screen.sessions.push_back(session.name);
    m_power = availablePowerActions();
    for (const PowerAction action : m_power)
        m_screen.powerActions.push_back(powerActionName(action));
    if (keyringLeftLocked())
        m_screen.notice = "Logging in leaves the keyring locked: /etc/pam.d/greetd has no pam_gnome_keyring.";

    m_display = wl_display_connect(nullptr);
    if (!m_display) {
        std::fprintf(stderr, "tde-charon: no Wayland compositor to show the login screen on\n");
        return 1;
    }
    static const wl_registry_listener registryListener {.global = global, .global_remove = globalRemove};
    m_registry.reset(wl_display_get_registry(m_display));
    wl_registry_add_listener(m_registry.get(), &registryListener, this);
    wl_display_roundtrip(m_display);
    if (!m_layerShell || !m_compositor || !m_shm) {
        std::fprintf(stderr, "tde-charon: the compositor cannot show the login screen over everything\n");
        return 1;
    }
    for (const auto& output : m_outputs) {
        if (!output->layer)
            showOn(*output);
    }
    if (m_seat && m_cursorShapes && m_pointer && !m_cursorShape)
        m_cursorShape.reset(wp_cursor_shape_manager_v1_get_pointer(m_cursorShapes.get(), m_pointer.get()));
    setUpIdle();
    wl_display_roundtrip(m_display);

    m_repeatTimer = FileDescriptor(timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK));
    m_clockTimer = FileDescriptor(timerfd_create(CLOCK_REALTIME, TFD_CLOEXEC | TFD_NONBLOCK));
    m_caretTimer = FileDescriptor(timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK));
    itimerspec everySecond {};
    everySecond.it_value.tv_sec = 1;
    everySecond.it_interval.tv_sec = 1;
    timerfd_settime(m_clockTimer.get(), 0, &everySecond, nullptr);
    tickClock();

    // The one who logged in last is asked for at once, as there is often only one; the others
    // are a step back. Nobody to list, a name is asked for.
    // One who was not listed, but logged in by name, is asked for all the same.
    const auto last = std::ranges::find(m_users, m_memory.lastUser(), &User::name);
    if (last != m_users.end()) {
        m_screen.selected = int(last - m_users.begin());
        pickUser(*last);
    } else if (!m_memory.lastUser().empty()) {
        pickUser(findUser(m_memory.lastUser()));
    } else if (m_users.empty()) {
        askName();
    }
    drawAll();

    while (!m_started) {
        while (wl_display_prepare_read(m_display) != 0)
            wl_display_dispatch_pending(m_display);
        wl_display_flush(m_display);
        pollfd fds[] {
            {wl_display_get_fd(m_display), POLLIN, 0},
            {m_greetd.waiting() ? m_greetd.fd() : -1, POLLIN, 0},
            {m_repeatTimer.get(), POLLIN, 0},
            {m_clockTimer.get(), POLLIN, 0},
            {m_caretTimer.get(), POLLIN, 0},
            {m_lift.fd(), POLLIN, 0},
        };
        if (poll(fds, std::size(fds), -1) < 0) {
            wl_display_cancel_read(m_display);
            continue;
        }
        if (fds[0].revents & POLLIN) {
            if (wl_display_read_events(m_display) < 0)
                break;
        } else {
            wl_display_cancel_read(m_display);
        }
        if (wl_display_dispatch_pending(m_display) < 0)
            break;

        if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            const auto reply = m_greetd.read();
            if (!reply) {
                std::fprintf(stderr, "tde-charon: greetd went away\n");
                return 1;
            }
            replied(*reply);
        }
        uint64_t expirations = 0;
        if ((fds[2].revents & POLLIN) && read(m_repeatTimer.get(), &expirations, sizeof(expirations)) > 0
            && m_repeatKey) {
            for (uint64_t i = 0; i < expirations; ++i)
                press(xkb_state_key_get_one_sym(m_state.get(), m_repeatKey + 8), m_repeatKey);
        }
        if ((fds[3].revents & POLLIN) && read(m_clockTimer.get(), &expirations, sizeof(expirations)) > 0)
            tickClock();
        if (fds[5].revents & POLLIN) {
            m_lift.step();
            // The other screens show none of it.
            if (Output* main = mainOutput())
                draw(*main);
        }
        if ((fds[4].revents & POLLIN) && read(m_caretTimer.get(), &expirations, sizeof(expirations)) > 0
            && m_blinking) {
            if (m_pulsing)
                ++m_screen.pulse;
            else
                m_screen.caret = !m_screen.caret;
            drawAll();
        }
    }
    if (m_blank)
        setScreensOn(true);
    wl_display_roundtrip(m_display);
    return m_started ? 0 : 1;
}

} // namespace

int greet()
{
    Greeter greeter;
    return greeter.run();
}

} // namespace charon
