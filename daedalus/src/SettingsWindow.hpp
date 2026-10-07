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

public:
    explicit SettingsWindow(QWidget* parent = nullptr);
    ~SettingsWindow() override;

private:
    void save();

    Settings m_settings;
    QTimer m_saving;
    QListWidget* m_pages = nullptr;
    QStackedWidget* m_stack = nullptr;
};

} // namespace daedalus
