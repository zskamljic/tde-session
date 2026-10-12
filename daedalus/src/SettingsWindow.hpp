#pragma once

#include "Pages.hpp"

#include <tde/ConfigWatcher.hpp>
#include <tde/DesktopConfig.hpp>

#include <QTimer>
#include <QWidget>

class QListWidget;
class QStackedWidget;

namespace tde {
class HeaderBar;
}

namespace daedalus {

// The settings of the session, a page for each part, chosen on the left. Changes are saved as
// they are made, and the session follows them at once.
class SettingsWindow : public QWidget {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.zskamljic.Daedalus")

public:
    explicit SettingsWindow(QWidget* parent = nullptr);
    ~SettingsWindow() override;

    // The page called `name` ("bluetooth", "sound" and the like); false when there is none.
    bool showPage(const QString& name);

public slots:
    // Over the session bus, from a second start: shows the window on the page asked for.
    Q_SCRIPTABLE void ShowPage(const QString& name);

private:
    void save();
    // Makes the pages, again when the settings were changed elsewhere, as in an editor.
    void buildPages();
    void reload(const QString& path);
    // The window buttons at the end of the header bars the desktop config says.
    void placeWindowButtons();

    Settings m_settings;
    QTimer m_saving;
    QListWidget* m_pages = nullptr;
    QStackedWidget* m_stack = nullptr;
    QStringList m_names; // of the pages, in order
    tde::HeaderBar* m_sidebarHeader = nullptr;
    tde::HeaderBar* m_contentHeader = nullptr;
    QWidget* m_windowButtons = nullptr;
    tde::DesktopConfig::WindowButtons m_placedButtons;
    tde::ConfigWatcher m_watcher; // of the session's config and the desktop's
};

} // namespace daedalus
