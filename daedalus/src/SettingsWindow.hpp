#pragma once

#include "Pages.hpp"

#include <QTimer>
#include <QWidget>

class QListWidget;
class QStackedWidget;

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

    Settings m_settings;
    QTimer m_saving;
    QListWidget* m_pages = nullptr;
    QStackedWidget* m_stack = nullptr;
    QStringList m_names; // of the pages, in order
};

} // namespace daedalus
