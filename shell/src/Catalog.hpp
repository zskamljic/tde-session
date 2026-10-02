#pragma once

#include "Applications.hpp"

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

#include <vector>

namespace shell {

// The installed applications for this desktop, read again a moment after some were
// installed or removed, once the package manager is done writing them.
class Catalog : public QObject {
    Q_OBJECT

public:
    explicit Catalog(QObject* parent = nullptr);

    const std::vector<Application>& applications() const { return m_applications; }

signals:
    // The applications were read again: pointers to the old ones are no longer valid.
    void changed();

private:
    void reload();
    void watch();
    bool appeared() const;

    std::vector<Application> m_applications;
    QFileSystemWatcher m_folders;
    QStringList m_waitingIn; // where folders of applications not there yet will be made
    QTimer m_delay;
};

} // namespace shell
