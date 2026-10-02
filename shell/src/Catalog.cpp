#include "Catalog.hpp"

#include <QDirListing>
#include <QFileInfo>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace shell {

Catalog::Catalog(QObject* parent)
    : QObject(parent)
{
    m_delay.setSingleShot(true);
    m_delay.setInterval(1000);
    connect(&m_folders, &QFileSystemWatcher::directoryChanged, this, [this](const QString& path) {
        // A folder watched only until an applications folder is made in it: anything else
        // happening there, and plenty does in ~/.local/share, is of no interest.
        // Only part of the way made: the watch moves down to where it was made.
        if (m_waitingIn.contains(path) && !appeared()) {
            watch();
            return;
        }
        m_delay.start();
    });
    connect(&m_delay, &QTimer::timeout, this, [this] {
        reload();
        emit changed();
    });
    reload();
}

void Catalog::reload()
{
    m_applications = loadApplications(qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(u':', Qt::SkipEmptyParts));
    watch();
}

// Whether a folder of applications that was not there before is now.
bool Catalog::appeared() const
{
    return std::ranges::any_of(applicationFolders(), [&](const QString& folder) {
        return QFileInfo(folder).isDir() && !m_folders.directories().contains(QFileInfo(folder).absoluteFilePath());
    });
}

// The folders of applications and those inside them; for a folder not there yet, such as
// ~/.local/share/applications before anything was installed for the user alone, the folder
// it will be made in.
void Catalog::watch()
{
    QStringList wanted;
    m_waitingIn.clear();
    for (const QString& folder : applicationFolders()) {
        QFileInfo existing(folder);
        while (!existing.isDir() && !existing.isRoot())
            existing.setFile(existing.path());
        wanted << existing.absoluteFilePath();
        if (existing.absoluteFilePath() != QFileInfo(folder).absoluteFilePath()) {
            m_waitingIn << existing.absoluteFilePath();
            continue;
        }
        for (const auto& entry :
            QDirListing(folder, QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::DirsOnly))
            wanted << entry.absoluteFilePath();
    }
    wanted.removeDuplicates();

    const QStringList watched = m_folders.directories();
    QStringList stale = watched;
    stale.removeIf([&](const QString& path) { return wanted.contains(path); });
    if (!stale.isEmpty())
        m_folders.removePaths(stale);
    QStringList added = wanted;
    added.removeIf([&](const QString& path) { return watched.contains(path); });
    if (!added.isEmpty())
        m_folders.addPaths(added);
}

} // namespace shell
