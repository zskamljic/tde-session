#pragma once

#include "ImageCopy.hpp"

#include "ext-foreign-toplevel-list-v1-client-protocol.h"
#include "tde-window-info-v1-client-protocol.h"

#include <Catalog.hpp>
#include <Windows.hpp>

#include <QIcon>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QScreen>
#include <QString>
#include <QTimer>
#include <QWidget>

class QPainter;
class QPainterPath;

#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <vector>

SHELL_PROXY(ext_foreign_toplevel_list_v1, ext_foreign_toplevel_list_v1_destroy);
SHELL_PROXY(ext_foreign_toplevel_handle_v1, ext_foreign_toplevel_handle_v1_destroy);
SHELL_PROXY(
    ext_foreign_toplevel_image_capture_source_manager_v1, ext_foreign_toplevel_image_capture_source_manager_v1_destroy);
SHELL_PROXY(tde_window_info_manager_v1, tde_window_info_manager_v1_destroy);
SHELL_PROXY(tde_window_info_v1, tde_window_info_v1_destroy);

namespace argus {

class Toplevels;
struct ListEntry;

// A window as the overview shows it. The compositor announces windows twice: in the window
// list, which can activate and close them, and through ext-foreign-toplevel-list, which
// screen capture works with. Both arrive in the order windows open, so the two halves of a
// window are paired as the first ones that agree on application and title.
struct Toplevel {
    Toplevel();
    ~Toplevel();

    QString displayName() const { return title.isEmpty() ? appId : title; }
    // How ext-foreign-toplevel-list names it, as screen sharing picks windows by; empty until paired.
    QString identifier() const;
    // How far down the stack it is: 0 for the window used last, those not known below all.
    int depth() const { return recency < 0 ? std::numeric_limits<int>::max() : recency; }

    quint64 id = 0; // as the window list knows it
    QString title;
    QString appId;
    QImage preview;
    // Where it is on the screen, in the coordinates of the output layout; empty when not known.
    QRect frame;
    int recency = -1; // 0 for the window used last; -1 when not known
    bool minimized = false;

    ListEntry* entry = nullptr; // its half from ext-foreign-toplevel-list, once paired
    std::unique_ptr<ImageCopy> capture; // while copying what it shows
    std::unique_ptr<ImageCopy> live; // while its picture follows what it shows
    Proxy<tde_window_info_v1> info; // while asking where it is
};

// Where `window` is on the screen, in the coordinates of `widget` covering it; empty when it is
// not shown or not known.
QRect placeOf(const Toplevel& window, const QWidget& widget);

// What stands for a window not pictured yet: its outline filled, with its application's icon.
void paintPlaceholder(QPainter& painter, const QPainterPath& shape, const QIcon& icon);

// A window as ext-foreign-toplevel-list announces it.
struct ListEntry {
    Toplevels* owner = nullptr;
    Proxy<ext_foreign_toplevel_handle_v1> handle;
    QString title;
    QString appId;
    QString pendingTitle;
    QString pendingAppId;
    QString identifier;
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
    // Puts the window on `screen`; where it is then comes with the next refresh().
    void moveToScreen(quint64 id, QScreen* screen);
    // Whether windows can be moved to another screen.
    bool canMoveWindows() const;

    // While live, the pictures of the windows follow what they show, as often as they change
    // and at most every so often; previewChanged tells. Every setLive(true) is undone by a
    // setLive(false).
    void setLive(bool live);

    // The installed applications, which the windows belong to.
    const shell::Catalog& catalog() const { return m_catalog; }
    QIcon iconOf(const Toplevel& window) const;

    Toplevel* find(quint64 id) const;

    // Captures what every window shows and asks where each one is; previewChanged follows for
    // each picture taken. `ready` is called once all are answered, or after a moment without.
    // `context` going first, `ready` is not called.
    void refresh(QObject* context, std::function<void()> ready);

signals:
    void windowsChanged();
    void previewChanged(quint64 id);

private:
    static void global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
    static void globalRemove(void* data, wl_registry* registry, uint32_t name);

    void sync();
    void addListEntry(ext_foreign_toplevel_handle_v1* handle);
    void listEntryDone(ListEntry& entry);
    void listEntryClosed(ListEntry& entry);
    void pair();
    void captured(Toplevel& window, QImage image);
    void startLive();
    void liveFrame(Toplevel& window, QImage image);
    void located(Toplevel& window, const QRect& frame, int recency, bool minimized);
    bool isRefreshing() const;
    void checkRefreshed(bool waitedLongEnough = false);
    void flush();

    shell::Windows m_windows;
    shell::Catalog m_catalog;
    mutable std::map<QString, QIcon> m_icons; // by application id, until the catalog changes
    wl_display* m_display = nullptr;
    Proxy<wl_registry> m_registry;
    Proxy<wl_shm> m_shm;
    Proxy<ext_foreign_toplevel_list_v1> m_list;
    Proxy<ext_foreign_toplevel_image_capture_source_manager_v1> m_sources;
    Proxy<ext_image_copy_capture_manager_v1> m_copier;
    Proxy<tde_window_info_manager_v1> m_info;

    std::vector<std::unique_ptr<Toplevel>> m_shown;
    std::vector<std::unique_ptr<ListEntry>> m_entries;
    std::vector<std::pair<QPointer<QObject>, std::function<void()>>> m_waiting; // for refresh() to be done
    QTimer m_patience;
    int m_live = 0; // how many asked for live pictures
};

} // namespace argus
