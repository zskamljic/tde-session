#pragma once

#include <Displays.hpp>

#include <QObject>
#include <QPointer>
#include <QScreen>
#include <QTimer>

namespace hermes {

// Arranges the displays as the session's settings keep them for the ones plugged in, at the
// start and whenever displays come or go, and tells which one has the bar.
class DisplayLayouts : public QObject {
    Q_OBJECT

public:
    explicit DisplayLayouts(QObject* parent = nullptr);

    // The screen with the bar, the notifications and the like.
    QScreen* primaryScreen() const { return m_primary; }

    void setSettings(const shell::SessionConfig::Displays& settings);

signals:
    void primaryScreenChanged(QScreen* screen);

private:
    void arrange();
    void findPrimary();

    shell::Displays m_displays;
    shell::SessionConfig::Displays m_settings;
    QStringList m_plugged; // the displays arranged last
    QPointer<QScreen> m_primary;
    QTimer m_retry; // after arranging them failed
    int m_failures = 0; // in a row
};

} // namespace hermes
