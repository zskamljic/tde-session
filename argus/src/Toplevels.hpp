#pragma once

#include "ext-foreign-toplevel-list-v1-client-protocol.h"
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"

#include <Windows.hpp>

#include <QImage>
#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>
#include <vector>

SHELL_PROXY(wl_shm, wl_shm_destroy);
SHELL_PROXY(wl_shm_pool, wl_shm_pool_destroy);
SHELL_PROXY(wl_buffer, wl_buffer_destroy);
SHELL_PROXY(ext_foreign_toplevel_list_v1, ext_foreign_toplevel_list_v1_destroy);
SHELL_PROXY(ext_foreign_toplevel_handle_v1, ext_foreign_toplevel_handle_v1_destroy);
SHELL_PROXY(
    ext_foreign_toplevel_image_capture_source_manager_v1, ext_foreign_toplevel_image_capture_source_manager_v1_destroy);
SHELL_PROXY(ext_image_capture_source_v1, ext_image_capture_source_v1_destroy);
SHELL_PROXY(ext_image_copy_capture_manager_v1, ext_image_copy_capture_manager_v1_destroy);
SHELL_PROXY(ext_image_copy_capture_session_v1, ext_image_copy_capture_session_v1_destroy);
SHELL_PROXY(ext_image_copy_capture_frame_v1, ext_image_copy_capture_frame_v1_destroy);

namespace argus {

using shell::Proxy;

class Capture;
class Toplevels;
struct ListEntry;

// A window as the overview shows it. The compositor announces windows twice: in the window
// list, which can activate and close them, and through ext-foreign-toplevel-list, which
// screen capture works with. Both arrive in the order windows open, so the two halves of a
// window are paired as the first ones that agree on application and title.
struct Toplevel {
    Toplevel();
    ~Toplevel();

    quint64 id = 0; // as the window list knows it
    QString title;
    QString appId;
    QImage preview;

    ListEntry* entry = nullptr; // its half from ext-foreign-toplevel-list, once paired
    std::unique_ptr<Capture> capture;
};

// A window as ext-foreign-toplevel-list announces it.
struct ListEntry {
    Toplevels* owner = nullptr;
    Proxy<ext_foreign_toplevel_handle_v1> handle;
    QString title;
    QString appId;
    QString pendingTitle;
    QString pendingAppId;
    bool announced = false;
    Toplevel* window = nullptr; // once paired
};

// Windows of the other programs on the compositor Qt is connected to.
class Toplevels : public QObject {
    Q_OBJECT

public:
    explicit Toplevels(QObject* parent = nullptr);
    ~Toplevels() override;

    // False when the compositor lacks the protocols to list or control windows.
    bool isSupported() const { return m_windows.isSupported(); }

    // In the order they were opened.
    const std::vector<std::unique_ptr<Toplevel>>& windows() const { return m_shown; }

    void activate(quint64 id);
    void close(quint64 id);

    // Captures what every window shows; previewChanged follows for each one that succeeds.
    void capturePreviews();

signals:
    void windowsChanged();
    void previewChanged(quint64 id);

private:
    friend class Capture;

    static void global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
    static void globalRemove(void* data, wl_registry* registry, uint32_t name);

    void sync();
    void addListEntry(ext_foreign_toplevel_handle_v1* handle);
    void listEntryDone(ListEntry& entry);
    void listEntryClosed(ListEntry& entry);
    void pair();
    void captured(Toplevel& window, QImage image);
    void flush();

    Toplevel* byId(quint64 id);

    shell::Windows m_windows;
    wl_display* m_display = nullptr;
    Proxy<wl_registry> m_registry;
    Proxy<wl_shm> m_shm;
    Proxy<ext_foreign_toplevel_list_v1> m_list;
    Proxy<ext_foreign_toplevel_image_capture_source_manager_v1> m_sources;
    Proxy<ext_image_copy_capture_manager_v1> m_copier;

    std::vector<std::unique_ptr<Toplevel>> m_shown;
    std::vector<std::unique_ptr<ListEntry>> m_entries;
};

} // namespace argus
