#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace shell {

// The power mode, through power-profiles-daemon when it runs: "performance", "balanced" or
// "power-saver".
class PowerProfiles : public QObject {
    Q_OBJECT

public:
    explicit PowerProfiles(QObject* parent = nullptr);

    bool isAvailable() const { return !m_service.isEmpty(); }
    QString active() const { return m_active; }
    // Those the machine has, the fastest first.
    QStringList offered() const { return m_offered; }
    void setActive(const QString& profile);

signals:
    void changed();

private slots:
    void refresh();

private:
    QString m_service;
    QString m_path;
    QString m_interface;
    QString m_active;
    QStringList m_offered;
};

// "Performance", "Balanced" or "Power Saver".
QString powerProfileName(const QString& profile);
// The symbolic icon of a profile.
QString powerProfileIcon(const QString& profile);

} // namespace shell
