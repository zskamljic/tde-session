#include "Locker.hpp"

#include "Auth.hpp"
#include "Face.hpp"
#include "Fingerprint.hpp"
#include "Owned.hpp"
#include "Password.hpp"

#include "ext-session-lock-v1-client-protocol.h"

#include <cairo.h>
#include <langinfo.h>
#include <poll.h>
#include <pwd.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

namespace cerberus {
namespace {

struct XkbDeleter {
    void operator()(xkb_context* context) const { xkb_context_unref(context); }
    void operator()(xkb_keymap* keymap) const { xkb_keymap_unref(keymap); }
    void operator()(xkb_state* state) const { xkb_state_unref(state); }
};

std::string formatted(const char* format)
{
    char text[128];
    const std::time_t now = std::time(nullptr);
    std::tm local {};
    localtime_r(&now, &local);
    return std::strftime(text, sizeof(text), format, &local) ? text : "";
}

// The clock as the locale writes it: with AM and PM where that is usual.
std::string timeText(bool seconds)
{
    const std::string_view format = nl_langinfo(T_FMT);
    const bool twelveHours = format.find("%I") != std::string_view::npos || format.find("%r") != std::string_view::npos;
    if (twelveHours)
        return formatted(seconds ? "%-I:%M:%S %p" : "%-I:%M %p");
    return formatted(seconds ? "%H:%M:%S" : "%H:%M");
}

std::string fullName(const passwd* user)
{
    // The first field of GECOS holds the name, when there is one.
    std::string gecos = user->pw_gecos ? user->pw_gecos : "";
    gecos = gecos.substr(0, gecos.find(','));
    return gecos.empty() ? user->pw_name : gecos;
}

class Locker;

struct Output {
    Locker* owner = nullptr;
    uint32_t name = 0;
    Owned<wl_output, wl_output_destroy> output;
    Owned<wl_surface, wl_surface_destroy> surface;
    Owned<ext_session_lock_surface_v1, ext_session_lock_surface_v1_destroy> lockSurface;
    int scale = 1;
    int width = 0;
    int height = 0;
};

// One picture handed to the compositor; it goes once the compositor is done with it.
struct Buffer {
    Locker* owner = nullptr;
    Owned<wl_buffer, wl_buffer_destroy> buffer;
    void* data = nullptr;
    std::size_t size = 0;

    ~Buffer()
    {
        if (data)
            munmap(data, size);
    }
};

class Locker {
public:
    explicit Locker(bool seconds)
        : m_seconds(seconds)
    {
    }
    Locker(const Locker&) = delete;
    Locker& operator=(const Locker&) = delete;

    int run();

private:
    static void global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
    static void globalRemove(void* data, wl_registry* registry, uint32_t name);

    void addOutput(wl_output* output, uint32_t name);
    void lockOutput(Output& output);
    void draw(Output& output);
    void drawAll();
    void released(Buffer& buffer);

    void setUpSeat(uint32_t capabilities);
    void keymap(int fd, uint32_t size);
    void key(uint32_t key, bool pressed);
    void press(xkb_keysym_t sym, uint32_t key);
    void startRepeat(uint32_t key);
    void stopRepeat();
    void tickClock();
    void authenticated(bool accepted);
    void fingerprintChanged();

    wl_display* m_display = nullptr;
    Owned<wl_registry, wl_registry_destroy> m_registry;
    Owned<wl_compositor, wl_compositor_destroy> m_compositor;
    Owned<wl_shm, wl_shm_destroy> m_shm;
    Owned<wl_seat, wl_seat_destroy> m_seat;
    Owned<wl_keyboard, wl_keyboard_destroy> m_keyboard;
    Owned<wl_pointer, wl_pointer_destroy> m_pointer;
    Owned<ext_session_lock_manager_v1, ext_session_lock_manager_v1_destroy> m_manager;
    ext_session_lock_v1* m_lock = nullptr; // destroyed by unlocking, or with "finished"
    std::vector<std::unique_ptr<Output>> m_outputs;
    std::vector<std::unique_ptr<Buffer>> m_buffers;

    std::unique_ptr<xkb_context, XkbDeleter> m_xkb {xkb_context_new(XKB_CONTEXT_NO_FLAGS)};
    std::unique_ptr<xkb_keymap, XkbDeleter> m_keymap;
    std::unique_ptr<xkb_state, XkbDeleter> m_state;
    int m_repeatRate = 25;
    int m_repeatDelay = 600;
    uint32_t m_repeatKey = 0;
    FileDescriptor m_repeatTimer;
    FileDescriptor m_clockTimer;

    std::unique_ptr<Authenticator> m_auth;
    std::unique_ptr<Fingerprint> m_fingerprint;
    int m_ticks = 0; // of the clock, to try the fingerprint reader again now and then
    Password m_password;
    Face m_face;
    bool m_seconds = false; // shown in the clock
    bool m_locked = false;
    bool m_finished = false;
    bool m_unlocked = false;
};

void Locker::global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
{
    auto* self = static_cast<Locker*>(data);
    const auto bind = [&]<typename T>(const wl_interface& wanted, uint32_t most) {
        return static_cast<T*>(wl_registry_bind(registry, name, &wanted, std::min(version, most)));
    };
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        self->m_compositor.reset(bind.operator()<wl_compositor>(wl_compositor_interface, 4));
    } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        self->m_shm.reset(bind.operator()<wl_shm>(wl_shm_interface, 1));
    } else if (std::strcmp(interface, ext_session_lock_manager_v1_interface.name) == 0) {
        self->m_manager.reset(bind.operator()<ext_session_lock_manager_v1>(ext_session_lock_manager_v1_interface, 1));
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0 && !self->m_seat) {
        static const wl_seat_listener listener {
            .capabilities
            = [](void* data, wl_seat*, uint32_t capabilities) { static_cast<Locker*>(data)->setUpSeat(capabilities); },
            .name = [](void*, wl_seat*, const char*) { },
        };
        self->m_seat.reset(bind.operator()<wl_seat>(wl_seat_interface, 5));
        wl_seat_add_listener(self->m_seat.get(), &listener, self);
    } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
        self->addOutput(bind.operator()<wl_output>(wl_output_interface, 3), name);
    }
}

void Locker::globalRemove(void* data, wl_registry*, uint32_t name)
{
    // An output unplugged takes its lock surface along.
    auto* self = static_cast<Locker*>(data);
    std::erase_if(self->m_outputs, [name](const auto& output) { return output->name == name; });
}

void Locker::addOutput(wl_output* wlOutput, uint32_t name)
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
                if (output->lockSurface && output->width > 0)
                    output->owner->draw(*output);
            },
        .scale
        = [](void* data, wl_output*, int32_t factor) { static_cast<Output*>(data)->scale = std::max(factor, 1); },
        .name = [](void*, wl_output*, const char*) { },
        .description = [](void*, wl_output*, const char*) { },
    };
    wl_output_add_listener(wlOutput, &listener, output.get());
    Output& added = *m_outputs.emplace_back(std::move(output));
    // Outputs plugged in while locked are covered too.
    if (m_lock)
        lockOutput(added);
}

void Locker::lockOutput(Output& output)
{
    output.surface.reset(wl_compositor_create_surface(m_compositor.get()));
    output.lockSurface.reset(ext_session_lock_v1_get_lock_surface(m_lock, output.surface.get(), output.output.get()));
    static const ext_session_lock_surface_v1_listener listener {
        .configure =
            [](void* data, ext_session_lock_surface_v1* lockSurface, uint32_t serial, uint32_t width, uint32_t height) {
                auto* output = static_cast<Output*>(data);
                ext_session_lock_surface_v1_ack_configure(lockSurface, serial);
                output->width = int(width);
                output->height = int(height);
                output->owner->draw(*output);
            },
    };
    ext_session_lock_surface_v1_add_listener(output.lockSurface.get(), &listener, &output);
}

void Locker::draw(Output& output)
{
    if (output.width <= 0 || output.height <= 0 || !m_shm)
        return;
    const int width = output.width * output.scale;
    const int height = output.height * output.scale;
    const int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    const std::size_t size = std::size_t(stride) * std::size_t(height);

    const FileDescriptor fd(memfd_create("tde-cerberus", MFD_CLOEXEC));
    if (!fd || ftruncate(fd.get(), off_t(size)) < 0)
        return;
    auto buffer = std::make_unique<Buffer>();
    buffer->owner = this;
    buffer->size = size;
    buffer->data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
    if (buffer->data == MAP_FAILED) {
        buffer->data = nullptr;
        return;
    }
    const Owned<wl_shm_pool, wl_shm_pool_destroy> pool(wl_shm_create_pool(m_shm.get(), fd.get(), int(size)));
    buffer->buffer.reset(wl_shm_pool_create_buffer(pool.get(), 0, width, height, stride, WL_SHM_FORMAT_ARGB8888));

    {
        const Owned<cairo_surface_t, cairo_surface_destroy> surface(cairo_image_surface_create_for_data(
            static_cast<unsigned char*>(buffer->data), CAIRO_FORMAT_ARGB32, width, height, stride));
        const Owned<cairo_t, cairo_destroy> cr(cairo_create(surface.get()));
        cairo_scale(cr.get(), output.scale, output.scale);
        cerberus::draw(cr.get(), output.width, output.height, m_face);
    }

    static const wl_buffer_listener listener {
        .release =
            [](void* data, wl_buffer*) {
                auto* buffer = static_cast<Buffer*>(data);
                buffer->owner->released(*buffer);
            },
    };
    wl_buffer_add_listener(buffer->buffer.get(), &listener, buffer.get());
    wl_surface_set_buffer_scale(output.surface.get(), output.scale);
    wl_surface_attach(output.surface.get(), buffer->buffer.get(), 0, 0);
    wl_surface_damage_buffer(output.surface.get(), 0, 0, width, height);
    wl_surface_commit(output.surface.get());
    m_buffers.push_back(std::move(buffer));
}

void Locker::drawAll()
{
    for (const auto& output : m_outputs) {
        if (output->lockSurface)
            draw(*output);
    }
}

void Locker::released(Buffer& buffer)
{
    std::erase_if(m_buffers, [&](const auto& b) { return b.get() == &buffer; });
}

void Locker::setUpSeat(uint32_t capabilities)
{
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !m_keyboard) {
        static const wl_keyboard_listener listener {
            .keymap =
                [](void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
                    const FileDescriptor keymap(fd);
                    if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
                        static_cast<Locker*>(data)->keymap(keymap.get(), size);
                },
            .enter = [](void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) { },
            .leave = [](void* data, wl_keyboard*, uint32_t, wl_surface*) { static_cast<Locker*>(data)->stopRepeat(); },
            .key
            = [](void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t key,
                  uint32_t state) { static_cast<Locker*>(data)->key(key, state == WL_KEYBOARD_KEY_STATE_PRESSED); },
            .modifiers =
                [](void* data, wl_keyboard*, uint32_t, uint32_t depressed, uint32_t latched, uint32_t locked,
                    uint32_t group) {
                    auto* self = static_cast<Locker*>(data);
                    if (!self->m_state)
                        return;
                    xkb_state_update_mask(self->m_state.get(), depressed, latched, locked, 0, 0, group);
                    const bool capsLock
                        = xkb_state_mod_name_is_active(self->m_state.get(), XKB_MOD_NAME_CAPS, XKB_STATE_MODS_LOCKED)
                        > 0;
                    if (capsLock != self->m_face.capsLock) {
                        self->m_face.capsLock = capsLock;
                        self->drawAll();
                    }
                },
            .repeat_info =
                [](void* data, wl_keyboard*, int32_t rate, int32_t delay) {
                    auto* self = static_cast<Locker*>(data);
                    self->m_repeatRate = rate;
                    self->m_repeatDelay = delay;
                },
        };
        m_keyboard.reset(wl_seat_get_keyboard(m_seat.get()));
        wl_keyboard_add_listener(m_keyboard.get(), &listener, this);
    }
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !m_pointer) {
        // No pointer over the lock screen: there is nothing to point at. The seat is bound at
        // version 5, so the events of later versions, which newer headers add, never come.
        static const wl_pointer_listener listener = [] {
            wl_pointer_listener events {};
            events.enter = [](void*, wl_pointer* pointer, uint32_t serial, wl_surface*, wl_fixed_t, wl_fixed_t) {
                wl_pointer_set_cursor(pointer, serial, nullptr, 0, 0);
            };
            events.leave = [](void*, wl_pointer*, uint32_t, wl_surface*) { };
            events.motion = [](void*, wl_pointer*, uint32_t, wl_fixed_t, wl_fixed_t) { };
            events.button = [](void*, wl_pointer*, uint32_t, uint32_t, uint32_t, uint32_t) { };
            events.axis = [](void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t) { };
            events.frame = [](void*, wl_pointer*) { };
            events.axis_source = [](void*, wl_pointer*, uint32_t) { };
            events.axis_stop = [](void*, wl_pointer*, uint32_t, uint32_t) { };
            events.axis_discrete = [](void*, wl_pointer*, uint32_t, int32_t) { };
            return events;
        }();
        m_pointer.reset(wl_seat_get_pointer(m_seat.get()));
        wl_pointer_add_listener(m_pointer.get(), &listener, this);
    }
}

void Locker::keymap(int fd, uint32_t size)
{
    void* text = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (text == MAP_FAILED)
        return;
    m_keymap.reset(xkb_keymap_new_from_string(
        m_xkb.get(), static_cast<const char*>(text), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS));
    munmap(text, size);
    m_state.reset(m_keymap ? xkb_state_new(m_keymap.get()) : nullptr);
}

void Locker::key(uint32_t key, bool pressed)
{
    if (!m_state)
        return;
    if (!pressed) {
        if (key == m_repeatKey)
            stopRepeat();
        return;
    }
    press(xkb_state_key_get_one_sym(m_state.get(), key + 8), key);
    if (xkb_keymap_key_repeats(m_keymap.get(), key + 8))
        startRepeat(key);
}

void Locker::press(xkb_keysym_t sym, uint32_t key)
{
    // While the password is checked, it stays as it is.
    if (m_auth->busy())
        return;
    if (m_face.status == Face::Status::Wrong)
        m_face.status = Face::Status::Ready;

    switch (sym) {
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        if (!m_password.empty()) {
            m_face.status = Face::Status::Checking;
            m_auth->check(m_password);
            m_password.clear();
            stopRepeat();
        }
        break;
    case XKB_KEY_BackSpace:
        m_password.removeLast();
        break;
    case XKB_KEY_Escape:
        m_password.clear();
        break;
    default:
        if (xkb_state_mod_name_is_active(m_state.get(), XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0) {
            if (sym == XKB_KEY_u)
                m_password.clear();
            break;
        }
        char text[64];
        const int length = xkb_state_key_get_utf8(m_state.get(), key + 8, text, sizeof(text));
        // Printable characters only; control characters type nothing.
        if (length > 0 && static_cast<unsigned char>(text[0]) >= 0x20 && text[0] != 0x7f)
            m_password.append(std::string_view(text, std::size_t(length)));
        explicit_bzero(text, sizeof(text));
        break;
    }
    m_face.typed = m_password.length();
    drawAll();
}

void Locker::startRepeat(uint32_t key)
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

void Locker::stopRepeat()
{
    m_repeatKey = 0;
    const itimerspec off {};
    timerfd_settime(m_repeatTimer.get(), 0, &off, nullptr);
}

void Locker::fingerprintChanged()
{
    using State = Fingerprint::State;
    switch (m_fingerprint->state()) {
    case State::Matched:
        authenticated(true);
        return;
    case State::Unavailable:
        m_face.finger = Face::Finger::None;
        break;
    case State::Waiting:
        m_face.finger = Face::Finger::Ready;
        break;
    case State::NoMatch:
        m_face.finger = Face::Finger::NoMatch;
        break;
    case State::Retry:
        m_face.finger = Face::Finger::Retry;
        break;
    }
    drawAll();
}

void Locker::tickClock()
{
    // A reader that went away, as over sleeping, may be back.
    if (++m_ticks % 10 == 0 && m_fingerprint && m_fingerprint->state() == Fingerprint::State::Unavailable) {
        m_fingerprint->restart();
        if (m_fingerprint->state() != Fingerprint::State::Unavailable)
            fingerprintChanged();
    }
    std::string time = timeText(m_seconds);
    std::string date = formatted("%A, %-d %B");
    if (time != m_face.time || date != m_face.date) {
        m_face.time = std::move(time);
        m_face.date = std::move(date);
        drawAll();
    }
}

void Locker::authenticated(bool accepted)
{
    if (accepted) {
        ext_session_lock_v1_unlock_and_destroy(m_lock);
        m_lock = nullptr;
        m_unlocked = true;
        return;
    }
    m_face.status = Face::Status::Wrong;
    drawAll();
}

int Locker::run()
{
    const passwd* user = getpwuid(getuid());
    if (!user) {
        std::fprintf(stderr, "tde-cerberus: who is this?\n");
        return 1;
    }
    m_face.user = fullName(user);
    m_auth = std::make_unique<Authenticator>(user->pw_name);
    m_fingerprint = std::make_unique<Fingerprint>(user->pw_name);
    if (m_fingerprint->state() == Fingerprint::State::Waiting)
        m_face.finger = Face::Finger::Ready;

    m_display = wl_display_connect(nullptr);
    if (!m_display) {
        std::fprintf(stderr, "tde-cerberus: no Wayland compositor to lock\n");
        return 1;
    }
    static const wl_registry_listener registryListener {.global = global, .global_remove = globalRemove};
    m_registry.reset(wl_display_get_registry(m_display));
    wl_registry_add_listener(m_registry.get(), &registryListener, this);
    wl_display_roundtrip(m_display);
    if (!m_manager || !m_compositor || !m_shm) {
        std::fprintf(stderr, "tde-cerberus: the compositor cannot lock the session\n");
        return 1;
    }
    // The outputs' scales and the keymap.
    wl_display_roundtrip(m_display);

    m_repeatTimer = FileDescriptor(timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK));
    m_clockTimer = FileDescriptor(timerfd_create(CLOCK_REALTIME, TFD_CLOEXEC | TFD_NONBLOCK));
    itimerspec everySecond {};
    everySecond.it_value.tv_sec = 1;
    everySecond.it_interval.tv_sec = 1;
    timerfd_settime(m_clockTimer.get(), 0, &everySecond, nullptr);
    m_face.time = timeText(m_seconds);
    m_face.date = formatted("%A, %-d %B");

    m_lock = ext_session_lock_manager_v1_lock(m_manager.get());
    static const ext_session_lock_v1_listener lockListener {
        .locked = [](void* data, ext_session_lock_v1*) { static_cast<Locker*>(data)->m_locked = true; },
        .finished = [](void* data, ext_session_lock_v1*) { static_cast<Locker*>(data)->m_finished = true; },
    };
    ext_session_lock_v1_add_listener(m_lock, &lockListener, this);
    for (const auto& output : m_outputs)
        lockOutput(*output);
    bool announced = false;

    while (!m_finished && !m_unlocked) {
        while (wl_display_prepare_read(m_display) != 0)
            wl_display_dispatch_pending(m_display);
        wl_display_flush(m_display);
        pollfd fds[] {
            {wl_display_get_fd(m_display), POLLIN, 0},
            {m_auth->fd(), POLLIN, 0},
            {m_repeatTimer.get(), POLLIN, 0},
            {m_clockTimer.get(), POLLIN, 0},
            {m_fingerprint->fd(), m_fingerprint->events(), 0},
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

        uint64_t expirations = 0;
        if ((fds[2].revents & POLLIN) && read(m_repeatTimer.get(), &expirations, sizeof(expirations)) > 0
            && m_repeatKey) {
            for (uint64_t i = 0; i < expirations; ++i)
                press(xkb_state_key_get_one_sym(m_state.get(), m_repeatKey + 8), m_repeatKey);
        }
        if ((fds[3].revents & POLLIN) && read(m_clockTimer.get(), &expirations, sizeof(expirations)) > 0)
            tickClock();
        if (fds[1].revents & POLLIN) {
            if (const auto accepted = m_auth->result())
                authenticated(*accepted);
        }
        if (fds[4].revents && m_fingerprint->process() && !m_unlocked)
            fingerprintChanged();
        // Whoever started it may wait for the session to be locked, as before sleeping.
        if (m_locked && !announced) {
            std::puts("locked");
            std::fflush(stdout);
            announced = true;
        }
    }

    if (m_unlocked) {
        wl_display_roundtrip(m_display);
        // Everything of the connection goes before it does.
        m_outputs.clear();
        m_buffers.clear();
        m_pointer.reset();
        m_keyboard.reset();
        m_seat.reset();
        m_manager.reset();
        m_shm.reset();
        m_compositor.reset();
        m_registry.reset();
        wl_display_disconnect(m_display);
        m_display = nullptr;
        return 0;
    }
    // Refused, as when another lock screen holds the session already.
    if (!m_locked)
        std::fprintf(stderr, "tde-cerberus: the session could not be locked\n");
    return m_locked ? 0 : 2;
}

} // namespace

int lockSession(bool seconds)
{
    Locker locker(seconds);
    return locker.run();
}

} // namespace cerberus
