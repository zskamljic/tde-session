#include "Windows.hpp"

#include <QGuiApplication>

#include <algorithm>
#include <cstring>

namespace shell {
namespace {

QString fromUtf8(const char* text)
{
    return QString::fromUtf8(text ? text : "");
}

} // namespace

Windows::Windows(QObject* parent)
    : QObject(parent)
{
    auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wayland) {
        qWarning("not running on Wayland, no windows to list");
        return;
    }
    m_display = wayland->display();
    m_seat = wayland->seat();

    static const wl_registry_listener listener {.global = global, .global_remove = globalRemove};
    m_registry.reset(wl_display_get_registry(m_display));
    wl_registry_add_listener(m_registry.get(), &listener, this);
    wl_display_roundtrip(m_display);
}

Windows::~Windows()
{
    // Handles go before the manager they came from.
    m_windows.clear();
    m_tokenRequests.clear();
}

void Windows::global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
{
    auto* self = static_cast<Windows*>(data);
    if (std::strcmp(interface, xdg_activation_v1_interface.name) == 0) {
        self->m_activation.reset(
            static_cast<xdg_activation_v1*>(wl_registry_bind(registry, name, &xdg_activation_v1_interface, 1)));
        return;
    }
    if (std::strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) != 0)
        return;
    static const zwlr_foreign_toplevel_manager_v1_listener listener {
        .toplevel = [](void* data, zwlr_foreign_toplevel_manager_v1*,
                        zwlr_foreign_toplevel_handle_v1* handle) { static_cast<Windows*>(data)->add(handle); },
        .finished = [](void*, zwlr_foreign_toplevel_manager_v1*) { },
    };
    self->m_manager.reset(static_cast<zwlr_foreign_toplevel_manager_v1*>(
        wl_registry_bind(registry, name, &zwlr_foreign_toplevel_manager_v1_interface, std::min(version, 3u))));
    zwlr_foreign_toplevel_manager_v1_add_listener(self->m_manager.get(), &listener, self);
}

void Windows::globalRemove(void*, wl_registry*, uint32_t) { }

void Windows::add(zwlr_foreign_toplevel_handle_v1* handle)
{
    auto window = std::make_unique<Window>();
    window->owner = this;
    window->id = m_nextId++;
    window->handle.reset(handle);

    static const zwlr_foreign_toplevel_handle_v1_listener listener {
        .title = [](void* data, zwlr_foreign_toplevel_handle_v1*,
                     const char* title) { static_cast<Window*>(data)->pendingTitle = fromUtf8(title); },
        .app_id = [](void* data, zwlr_foreign_toplevel_handle_v1*,
                      const char* appId) { static_cast<Window*>(data)->pendingAppId = fromUtf8(appId); },
        .output_enter = [](void*, zwlr_foreign_toplevel_handle_v1*, wl_output*) { },
        .output_leave = [](void*, zwlr_foreign_toplevel_handle_v1*, wl_output*) { },
        .state =
            [](void* data, zwlr_foreign_toplevel_handle_v1*, wl_array* states) {
                auto* window = static_cast<Window*>(data);
                window->pendingActivated = false;
                window->pendingMinimized = false;
                const auto* state = static_cast<const uint32_t*>(states->data);
                for (size_t i = 0; i < states->size / sizeof(uint32_t); ++i) {
                    if (state[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED)
                        window->pendingActivated = true;
                    else if (state[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED)
                        window->pendingMinimized = true;
                }
            },
        .done =
            [](void* data, zwlr_foreign_toplevel_handle_v1*) {
                auto* window = static_cast<Window*>(data);
                window->owner->done(*window);
            },
        .closed =
            [](void* data, zwlr_foreign_toplevel_handle_v1*) {
                auto* window = static_cast<Window*>(data);
                window->owner->closed(*window);
            },
        .parent = [](void*, zwlr_foreign_toplevel_handle_v1*, zwlr_foreign_toplevel_handle_v1*) { },
    };
    zwlr_foreign_toplevel_handle_v1_add_listener(handle, &listener, window.get());
    m_windows.push_back(std::move(window));
}

void Windows::done(Window& window)
{
    window.title = window.pendingTitle;
    window.appId = window.pendingAppId;
    window.activated = window.pendingActivated;
    window.minimized = window.pendingMinimized;
    if (!window.announced || window.activated) {
        std::erase(m_recent, window.id);
        // A new window that is not active goes behind the active ones.
        m_recent.insert(window.activated ? m_recent.begin() : m_recent.end(), window.id);
    }
    window.announced = true;
    emit changed();
}

void Windows::closed(Window& window)
{
    std::erase(m_recent, window.id);
    std::erase_if(m_windows, [&](const auto& w) { return w.get() == &window; });
    emit changed();
}

std::vector<const Window*> Windows::windows() const
{
    std::vector<const Window*> result;
    for (const auto& window : m_windows) {
        if (window->announced)
            result.push_back(window.get());
    }
    return result;
}

const Window* Windows::find(quint64 id) const
{
    const auto it = std::ranges::find(m_windows, id, [](const auto& window) { return window->id; });
    return it == m_windows.end() ? nullptr : it->get();
}

Window* Windows::byId(quint64 id)
{
    return const_cast<Window*>(std::as_const(*this).find(id));
}

void Windows::activate(quint64 id)
{
    Window* window = byId(id);
    if (!window || !m_seat)
        return;
    if (window->minimized)
        zwlr_foreign_toplevel_handle_v1_unset_minimized(window->handle.get());
    zwlr_foreign_toplevel_handle_v1_activate(window->handle.get(), m_seat);
    flush();
}

void Windows::minimize(quint64 id)
{
    if (Window* window = byId(id)) {
        zwlr_foreign_toplevel_handle_v1_set_minimized(window->handle.get());
        flush();
    }
}

void Windows::close(quint64 id)
{
    if (Window* window = byId(id)) {
        zwlr_foreign_toplevel_handle_v1_close(window->handle.get());
        flush();
    }
}

void Windows::requestActivationToken(std::function<void(const QString&)> done)
{
    auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!m_activation || !wayland) {
        done({});
        return;
    }
    auto request = std::make_unique<TokenRequest>(TokenRequest {this,
        Proxy<xdg_activation_token_v1>(xdg_activation_v1_get_activation_token(m_activation.get())), std::move(done)});
    static const xdg_activation_token_v1_listener listener {
        .done =
            [](void* data, xdg_activation_token_v1*, const char* token) {
                auto* request = static_cast<TokenRequest*>(data);
                const auto callback = std::move(request->done);
                Windows* owner = request->owner;
                std::erase_if(owner->m_tokenRequests, [&](const auto& r) { return r.get() == request; });
                callback(QString::fromUtf8(token));
            },
    };
    xdg_activation_token_v1* token = request->token.get();
    xdg_activation_token_v1_add_listener(token, &listener, request.get());
    // The click that asks for it vouches for it.
    if (wl_seat* seat = wayland->lastInputSeat())
        xdg_activation_token_v1_set_serial(token, wayland->lastInputSerial(), seat);
    xdg_activation_token_v1_commit(token);
    m_tokenRequests.push_back(std::move(request));
    flush();
}

void Windows::flush()
{
    if (m_display)
        wl_display_flush(m_display);
}

} // namespace shell
