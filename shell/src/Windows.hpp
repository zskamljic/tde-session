#pragma once

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "xdg-activation-v1-client-protocol.h"

#include <Proxy.hpp>

#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

SHELL_PROXY(zwlr_foreign_toplevel_manager_v1, zwlr_foreign_toplevel_manager_v1_destroy);
SHELL_PROXY(zwlr_foreign_toplevel_handle_v1, zwlr_foreign_toplevel_handle_v1_destroy);
SHELL_PROXY(xdg_activation_v1, xdg_activation_v1_destroy);
SHELL_PROXY(xdg_activation_token_v1, xdg_activation_token_v1_destroy);

namespace shell {

class Windows;

// A window of another program, as the compositor announces it.
struct Window {
    Windows* owner = nullptr;
    quint64 id = 0;
    QString title;
    QString appId;
    bool activated = false;
    bool minimized = false;
    Proxy<zwlr_foreign_toplevel_handle_v1> handle;

    // What arrived since the last "done".
    QString pendingTitle;
    QString pendingAppId;
    bool pendingActivated = false;
    bool pendingMinimized = false;
    bool announced = false;
};

// The open windows, through wlr-foreign-toplevel-management, which can also switch to,
// minimize and close them.
class Windows : public QObject {
    Q_OBJECT

public:
    explicit Windows(QObject* parent = nullptr);
    ~Windows() override;

    bool isSupported() const { return m_manager != nullptr; }

    // Announced windows, in the order they were opened.
    std::vector<const Window*> windows() const;
    const Window* find(quint64 id) const;
    // Ids of windows, the one active most recently first.
    const std::vector<quint64>& recent() const { return m_recent; }

    void activate(quint64 id);
    void minimize(quint64 id);
    void close(quint64 id);

    // Asks for a token another program can bring its window forward with, on the strength of
    // the click the user just made. `done` gets it, or nothing when the compositor gives none.
    void requestActivationToken(std::function<void(const QString&)> done);

signals:
    void changed();

private:
    static void global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
    static void globalRemove(void* data, wl_registry* registry, uint32_t name);

    void add(zwlr_foreign_toplevel_handle_v1* handle);
    void done(Window& window);
    void closed(Window& window);
    Window* byId(quint64 id);
    void flush();

    wl_display* m_display = nullptr;
    wl_seat* m_seat = nullptr;
    Proxy<wl_registry> m_registry;
    Proxy<zwlr_foreign_toplevel_manager_v1> m_manager;
    Proxy<xdg_activation_v1> m_activation;
    std::vector<std::unique_ptr<Window>> m_windows;

    struct TokenRequest {
        Windows* owner;
        Proxy<xdg_activation_token_v1> token;
        std::function<void(const QString&)> done;
    };
    std::vector<std::unique_ptr<TokenRequest>> m_tokenRequests;
    std::vector<quint64> m_recent;
    quint64 m_nextId = 1;
};

} // namespace shell
