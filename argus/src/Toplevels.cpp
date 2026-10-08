#include "Toplevels.hpp"

#include <Applications.hpp>
#include <Icons.hpp>

#include <tde/Theme.hpp>

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>

#include <algorithm>
#include <cstring>

Q_LOGGING_CATEGORY(lcToplevels, "tde.argus.toplevels")

namespace argus {
namespace {

QString fromUtf8(const char* text)
{
    return QString::fromUtf8(text ? text : "");
}

} // namespace

Toplevel::Toplevel() = default;
Toplevel::~Toplevel() = default;

QString Toplevel::identifier() const
{
    return entry ? entry->identifier : QString();
}

QRect placeOf(const Toplevel& window, const QWidget& widget)
{
    if (window.frame.isEmpty() || window.minimized)
        return {};
    return window.frame.translated(-(widget.screen() ? widget.screen()->geometry().topLeft() : QPoint()));
}

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
    connect(&m_catalog, &shell::Catalog::changed, this, [this] { m_icons.clear(); });
    sync();

    // Pictures that take longer than this are shown once they come.
    m_patience.setSingleShot(true);
    m_patience.setInterval(150);
    connect(&m_patience, &QTimer::timeout, this, [this] { checkRefreshed(true); });
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
        Toplevel* window = find(listedWindow->id);
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
        .identifier = [](void* data, ext_foreign_toplevel_handle_v1*,
                          const char* identifier) { static_cast<ListEntry*>(data)->identifier = fromUtf8(identifier); },
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

Toplevel* Toplevels::find(quint64 id) const
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

QIcon Toplevels::iconOf(const Toplevel& window) const
{
    // Finding the application goes through all of them, too much to do for every frame.
    auto [it, added] = m_icons.try_emplace(window.appId);
    if (added) {
        it->second = shell::applicationIcon(
            shell::findApplication(m_catalog.applications(), window.appId), window.appId.toLower());
    }
    return it->second;
}

void paintPlaceholder(QPainter& painter, const QPainterPath& shape, const QIcon& icon)
{
    const QRectF area = shape.boundingRect();
    painter.fillPath(shape, tde::theme::colors().window);
    const double size = std::min({96.0, area.width() / 2, area.height() / 2});
    QRectF place(0, 0, size, size);
    place.moveCenter(area.center());
    icon.paint(&painter, place.toRect());
}

void Toplevels::refresh(QObject* context, std::function<void()> ready)
{
    m_waiting.emplace_back(context, std::move(ready));
    if (!m_patience.isActive())
        m_patience.start();

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
            window->capture = std::make_unique<ImageCopy>(m_shm.get(), m_copier.get(), source,
                [this, shown = window.get()](QImage image) { captured(*shown, std::move(image)); });
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

void Toplevels::checkRefreshed(bool waitedLongEnough)
{
    if (m_waiting.empty() || (isRefreshing() && !waitedLongEnough))
        return;
    m_patience.stop();
    // The callbacks may ask again.
    for (const auto& [context, ready] : std::exchange(m_waiting, {})) {
        if (context)
            ready();
    }
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
