#include "Toplevels.hpp"

#include <QGuiApplication>
#include <QLoggingCategory>

#include <algorithm>
#include <cstring>
#include <optional>

#include <sys/mman.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcToplevels, "tde.argus.toplevels")

namespace argus {
namespace {

QString fromUtf8(const char* text)
{
    return QString::fromUtf8(text ? text : "");
}

// Memory shared with the compositor, which copies a window into it.
class SharedMemory {
public:
    explicit SharedMemory(size_t size)
        : m_size(size)
    {
        m_fd = memfd_create("tde-argus", MFD_CLOEXEC);
        if (m_fd < 0 || ftruncate(m_fd, static_cast<off_t>(size)) < 0)
            return;
        void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
        if (data != MAP_FAILED)
            m_data = data;
    }
    ~SharedMemory()
    {
        if (m_data)
            munmap(m_data, m_size);
        if (m_fd >= 0)
            ::close(m_fd);
    }
    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    bool isValid() const { return m_data != nullptr; }
    int fd() const { return m_fd; }
    const uchar* data() const { return static_cast<const uchar*>(m_data); }

private:
    size_t m_size = 0;
    int m_fd = -1;
    void* m_data = nullptr;
};

// The QImage format with the same layout in memory, for the 32-bit formats the compositor
// may copy windows in.
std::optional<QImage::Format> imageFormat(uint32_t format)
{
    switch (format) {
    case WL_SHM_FORMAT_XRGB8888:
        return QImage::Format_RGB32;
    case WL_SHM_FORMAT_ARGB8888:
        return QImage::Format_ARGB32_Premultiplied;
    case WL_SHM_FORMAT_XBGR8888:
        return QImage::Format_RGBX8888;
    case WL_SHM_FORMAT_ABGR8888:
        return QImage::Format_RGBA8888_Premultiplied;
    default:
        return std::nullopt;
    }
}

} // namespace

// One copy of what a window shows, through ext-image-copy-capture. The session first says
// which buffers it takes, then a frame is copied into one; either way it ends in
// Toplevels::captured, which destroys it.
class Capture {
public:
    Capture(Toplevels& owner, Toplevel& window, ext_image_capture_source_v1* source)
        : m_owner(owner)
        , m_window(window)
        , m_source(source)
        , m_session(ext_image_copy_capture_manager_v1_create_session(owner.m_copier.get(), source, 0))
    {
        static const ext_image_copy_capture_session_v1_listener listener {
            .buffer_size
            = [](void* data, ext_image_copy_capture_session_v1*, uint32_t width,
                  uint32_t height) { static_cast<Capture*>(data)->m_size = QSize(int(width), int(height)); },
            .shm_format =
                [](void* data, ext_image_copy_capture_session_v1*, uint32_t format) {
                    auto* self = static_cast<Capture*>(data);
                    const auto image = imageFormat(format);
                    // Formats with alpha keep rounded corners and the like see-through.
                    if (image && (!self->m_format || QImage(1, 1, *image).hasAlphaChannel()))
                        self->m_format = format;
                },
            .dmabuf_device = [](void*, ext_image_copy_capture_session_v1*, wl_array*) { },
            .dmabuf_format = [](void*, ext_image_copy_capture_session_v1*, uint32_t, wl_array*) { },
            .done = [](void* data, ext_image_copy_capture_session_v1*) { static_cast<Capture*>(data)->copy(); },
            .stopped = [](void* data, ext_image_copy_capture_session_v1*) { static_cast<Capture*>(data)->finish({}); },
        };
        ext_image_copy_capture_session_v1_add_listener(m_session.get(), &listener, this);
    }

private:
    void copy()
    {
        if (m_frame)
            return; // the buffer constraints changed while copying; the copy still fits or fails
        if (!m_format || m_size.isEmpty()) {
            finish({});
            return;
        }
        const int stride = m_size.width() * 4;
        m_memory = std::make_unique<SharedMemory>(size_t(stride) * size_t(m_size.height()));
        if (!m_memory->isValid()) {
            finish({});
            return;
        }
        Proxy<wl_shm_pool> pool(wl_shm_create_pool(m_owner.m_shm.get(), m_memory->fd(), stride * m_size.height()));
        m_buffer.reset(wl_shm_pool_create_buffer(pool.get(), 0, m_size.width(), m_size.height(), stride, *m_format));

        static const ext_image_copy_capture_frame_v1_listener listener {
            .transform = [](void*, ext_image_copy_capture_frame_v1*, uint32_t) { },
            .damage = [](void*, ext_image_copy_capture_frame_v1*, int32_t, int32_t, int32_t, int32_t) { },
            .presentation_time = [](void*, ext_image_copy_capture_frame_v1*, uint32_t, uint32_t, uint32_t) { },
            .ready = [](void* data, ext_image_copy_capture_frame_v1*) { static_cast<Capture*>(data)->ready(); },
            .failed =
                [](void* data, ext_image_copy_capture_frame_v1*, uint32_t reason) {
                    qCDebug(lcToplevels) << "capture failed, reason" << reason;
                    static_cast<Capture*>(data)->finish({});
                },
        };
        m_frame.reset(ext_image_copy_capture_session_v1_create_frame(m_session.get()));
        ext_image_copy_capture_frame_v1_add_listener(m_frame.get(), &listener, this);
        ext_image_copy_capture_frame_v1_attach_buffer(m_frame.get(), m_buffer.get());
        ext_image_copy_capture_frame_v1_damage_buffer(m_frame.get(), 0, 0, m_size.width(), m_size.height());
        ext_image_copy_capture_frame_v1_capture(m_frame.get());
        m_owner.flush();
    }

    void ready()
    {
        const QImage view(
            m_memory->data(), m_size.width(), m_size.height(), m_size.width() * 4, *imageFormat(*m_format));
        finish(view.copy());
    }

    // Hands the image over; this capture is destroyed on return.
    void finish(QImage image) { m_owner.captured(m_window, std::move(image)); }

    Toplevels& m_owner;
    Toplevel& m_window;
    Proxy<ext_image_capture_source_v1> m_source;
    Proxy<ext_image_copy_capture_session_v1> m_session;
    Proxy<ext_image_copy_capture_frame_v1> m_frame;
    Proxy<wl_buffer> m_buffer;
    std::unique_ptr<SharedMemory> m_memory;
    QSize m_size;
    std::optional<uint32_t> m_format;
};

Toplevel::Toplevel() = default;
Toplevel::~Toplevel() = default;

Toplevels::Toplevels(QObject* parent)
    : QObject(parent)
{
    auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wayland) {
        qCWarning(lcToplevels) << "not running on Wayland, no windows to show";
        return;
    }
    m_display = wayland->display();

    static const wl_registry_listener listener {.global = global, .global_remove = globalRemove};
    m_registry.reset(wl_display_get_registry(m_display));
    wl_registry_add_listener(m_registry.get(), &listener, this);
    wl_display_roundtrip(m_display);

    connect(&m_windows, &shell::Windows::changed, this, &Toplevels::sync);
    sync();
}

Toplevels::~Toplevels()
{
    // Captures and handles go before the managers they came from.
    m_shown.clear();
    m_entries.clear();
}

void Toplevels::global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
{
    auto* self = static_cast<Toplevels*>(data);
    const auto bind = [&]<typename T>(const wl_interface& wanted, uint32_t maxVersion) {
        return static_cast<T*>(wl_registry_bind(registry, name, &wanted, std::min(version, maxVersion)));
    };

    if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        self->m_shm.reset(bind.operator()<wl_shm>(wl_shm_interface, 1));
    } else if (std::strcmp(interface, ext_foreign_toplevel_list_v1_interface.name) == 0) {
        static const ext_foreign_toplevel_list_v1_listener listener {
            .toplevel
            = [](void* data, ext_foreign_toplevel_list_v1*,
                  ext_foreign_toplevel_handle_v1* handle) { static_cast<Toplevels*>(data)->addListEntry(handle); },
            .finished = [](void*, ext_foreign_toplevel_list_v1*) { },
        };
        self->m_list.reset(bind.operator()<ext_foreign_toplevel_list_v1>(ext_foreign_toplevel_list_v1_interface, 1));
        ext_foreign_toplevel_list_v1_add_listener(self->m_list.get(), &listener, self);
    } else if (std::strcmp(interface, ext_foreign_toplevel_image_capture_source_manager_v1_interface.name) == 0) {
        self->m_sources.reset(bind.operator()<ext_foreign_toplevel_image_capture_source_manager_v1>(
            ext_foreign_toplevel_image_capture_source_manager_v1_interface, 1));
    } else if (std::strcmp(interface, ext_image_copy_capture_manager_v1_interface.name) == 0) {
        self->m_copier.reset(
            bind.operator()<ext_image_copy_capture_manager_v1>(ext_image_copy_capture_manager_v1_interface, 1));
    } else if (std::strcmp(interface, tde_window_info_manager_v1_interface.name) == 0) {
        self->m_info.reset(bind.operator()<tde_window_info_manager_v1>(tde_window_info_manager_v1_interface, 1));
    }
}

void Toplevels::globalRemove(void*, wl_registry*, uint32_t) { }

// Follows the window list: windows new to it are added, those gone from it go, with their
// pictures and pairing.
void Toplevels::sync()
{
    const auto listed = m_windows.windows();
    std::erase_if(m_shown, [&](const auto& window) {
        const bool gone
            = std::ranges::none_of(listed, [&](const shell::Window* other) { return other->id == window->id; });
        if (gone && window->entry)
            window->entry->window = nullptr;
        return gone;
    });
    for (const shell::Window* listedWindow : listed) {
        Toplevel* window = byId(listedWindow->id);
        if (!window) {
            window = m_shown.emplace_back(std::make_unique<Toplevel>()).get();
            window->id = listedWindow->id;
        }
        window->title = listedWindow->title;
        window->appId = listedWindow->appId;
    }
    pair();
    emit windowsChanged();
    checkRefreshed();
}

void Toplevels::addListEntry(ext_foreign_toplevel_handle_v1* handle)
{
    auto entry = std::make_unique<ListEntry>();
    entry->owner = this;
    entry->handle.reset(handle);

    static const ext_foreign_toplevel_handle_v1_listener listener {
        .closed =
            [](void* data, ext_foreign_toplevel_handle_v1*) {
                auto* entry = static_cast<ListEntry*>(data);
                entry->owner->listEntryClosed(*entry);
            },
        .done =
            [](void* data, ext_foreign_toplevel_handle_v1*) {
                auto* entry = static_cast<ListEntry*>(data);
                entry->owner->listEntryDone(*entry);
            },
        .title = [](void* data, ext_foreign_toplevel_handle_v1*,
                     const char* title) { static_cast<ListEntry*>(data)->pendingTitle = fromUtf8(title); },
        .app_id = [](void* data, ext_foreign_toplevel_handle_v1*,
                      const char* appId) { static_cast<ListEntry*>(data)->pendingAppId = fromUtf8(appId); },
        .identifier = [](void*, ext_foreign_toplevel_handle_v1*, const char*) { },
    };
    ext_foreign_toplevel_handle_v1_add_listener(handle, &listener, entry.get());
    m_entries.push_back(std::move(entry));
}

void Toplevels::listEntryDone(ListEntry& entry)
{
    entry.title = entry.pendingTitle;
    entry.appId = entry.pendingAppId;
    entry.announced = true;
    pair();
}

void Toplevels::listEntryClosed(ListEntry& entry)
{
    if (entry.window) {
        entry.window->capture.reset();
        entry.window->info.reset();
        entry.window->entry = nullptr;
    }
    std::erase_if(m_entries, [&](const auto& e) { return e.get() == &entry; });
    checkRefreshed();
}

void Toplevels::pair()
{
    for (const auto& window : m_shown) {
        if (window->entry)
            continue;
        const auto match = std::ranges::find_if(m_entries, [&](const auto& entry) {
            return !entry->window && entry->announced && entry->appId == window->appId && entry->title == window->title;
        });
        if (match != m_entries.end()) {
            window->entry = match->get();
            (*match)->window = window.get();
        }
    }
}

Toplevel* Toplevels::byId(quint64 id)
{
    const auto it = std::ranges::find_if(m_shown, [id](const auto& window) { return window->id == id; });
    return it == m_shown.end() ? nullptr : it->get();
}

void Toplevels::activate(quint64 id)
{
    m_windows.activate(id);
}

void Toplevels::close(quint64 id)
{
    m_windows.close(id);
}

void Toplevels::refresh()
{
    static const tde_window_info_v1_listener listener {
        .info =
            [](void* data, tde_window_info_v1*, int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t recency,
                uint32_t flags) {
                auto* window = static_cast<Toplevel*>(data);
                window->entry->owner->located(*window, QRect(x, y, int(width), int(height)), int(recency),
                    flags & TDE_WINDOW_INFO_V1_FLAGS_MINIMIZED);
            },
    };
    for (const auto& window : m_shown) {
        if (!window->entry)
            continue;
        if (!window->capture && m_sources && m_copier && m_shm) {
            auto* source = ext_foreign_toplevel_image_capture_source_manager_v1_create_source(
                m_sources.get(), window->entry->handle.get());
            window->capture = std::make_unique<Capture>(*this, *window, source);
        }
        if (!window->info && m_info) {
            window->info.reset(tde_window_info_manager_v1_get_info(m_info.get(), window->entry->handle.get()));
            tde_window_info_v1_add_listener(window->info.get(), &listener, window.get());
        }
    }
    flush();
    checkRefreshed();
}

bool Toplevels::isRefreshing() const
{
    return std::ranges::any_of(m_shown, [](const auto& window) { return window->capture || window->info; });
}

void Toplevels::checkRefreshed()
{
    if (!isRefreshing())
        emit refreshed();
}

void Toplevels::captured(Toplevel& window, QImage image)
{
    // Destroys the capture that called; nothing of it may run after this.
    window.capture.reset();
    if (!image.isNull()) {
        window.preview = std::move(image);
        emit previewChanged(window.id);
    }
    checkRefreshed();
}

void Toplevels::located(Toplevel& window, const QRect& frame, int recency, bool minimized)
{
    // The compositor is done with the object; this lets go of it too.
    window.info.reset();
    window.frame = frame;
    window.recency = frame.isEmpty() ? -1 : recency;
    window.minimized = minimized;
    checkRefreshed();
}

void Toplevels::flush()
{
    if (m_display)
        wl_display_flush(m_display);
}

} // namespace argus
