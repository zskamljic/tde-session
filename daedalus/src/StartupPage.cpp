#include "StartupPage.hpp"

#include <Icons.hpp>

#include <tde/Dialog.hpp>
#include <tde/Theme.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSaveFile>
#include <QToolButton>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace daedalus {
namespace {

QStringList currentDesktops()
{
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(u':', Qt::SkipEmptyParts);
}

QString readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(file.readAll()) : QString();
}

bool writeFile(const QString& path, const QString& text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(text.toUtf8()) < 0)
        return false;
    return file.commit();
}

} // namespace

QString withAutostartEnabled(const QString& text, bool enabled)
{
    const QStringList keys {u"Hidden"_s, u"X-GNOME-Autostart-enabled"_s};
    QStringList lines = text.split(u'\n');
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    qsizetype group = -1;
    bool inEntry = false;
    for (qsizetype i = 0; i < lines.size();) {
        const QString line = lines[i].trimmed();
        if (line.startsWith(u'[')) {
            inEntry = line == u"[Desktop Entry]";
            if (inEntry)
                group = i;
            ++i;
            continue;
        }
        const QString key = line.section(u'=', 0, 0).trimmed();
        if (inEntry && line.contains(u'=') && keys.contains(key))
            lines.removeAt(i);
        else
            ++i;
    }
    if (group < 0) {
        lines.prepend(u"[Desktop Entry]"_s);
        group = 0;
    }
    lines.insert(group + 1, u"Hidden=%1"_s.arg(enabled ? u"false"_s : u"true"_s));
    lines.insert(group + 2, u"X-GNOME-Autostart-enabled=%1"_s.arg(enabled ? u"true"_s : u"false"_s));
    return lines.join(u'\n') + u'\n';
}

bool setAutostartEnabled(const shell::AutostartEntry& entry, bool enabled, const QString& userFolder)
{
    // The system's own stays as it is; the user's copy, made when there is none, says otherwise.
    const QString text = readFile(entry.path);
    if (text.isEmpty())
        return false;
    return writeFile(userFolder + u'/' + entry.id, withAutostartEnabled(text, enabled));
}

bool addAutostart(const QString& id, const QString& userFolder)
{
    const QString path = shell::applicationFile(id);
    const QString text = path.isEmpty() ? QString() : readFile(path);
    if (text.isEmpty())
        return false;
    // Named by its id, as the autostart folders go by the file's name.
    return writeFile(userFolder + u'/' + id, withAutostartEnabled(text, true));
}

StartupPage::StartupPage(QWidget* parent)
    : Page(parent)
{
    fill();
}

void StartupPage::fill()
{
    if (m_group) {
        m_group->hide();
        m_group->deleteLater();
    }
    m_group = addGroup(u"Startup Applications"_s);
    auto* add = new QPushButton(u"Add…"_s, this);
    connect(add, &QPushButton::clicked, this, &StartupPage::add);
    m_group->setHeaderWidget(add);

    const QStringList folders = shell::autostartFolders();
    const QString userFolder = folders.first();
    const auto entries = shell::autostartEntries(folders, currentDesktops());
    for (const shell::AutostartEntry& entry : entries) {
        auto* controls = new QWidget(this);
        auto* layout = new QHBoxLayout(controls);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        // Those the user added can go again; those of the system can only be turned off.
        if (!entry.system) {
            auto* remove = new QToolButton(controls);
            remove->setIcon(tde::theme::symbolicIcon(u"user-trash"_s));
            remove->setToolTip(u"Remove"_s);
            remove->setAutoRaise(true);
            remove->setCursor(Qt::PointingHandCursor);
            connect(remove, &QToolButton::clicked, this, [this, path = entry.path] {
                if (!QFile::remove(path))
                    qWarning("tde-daedalus: cannot remove %s", qPrintable(path));
                QMetaObject::invokeMethod(this, &StartupPage::fill, Qt::QueuedConnection);
            });
            layout->addWidget(remove);
        }
        auto* enabled = new Switch(controls);
        enabled->setChecked(entry.enabled);
        connect(enabled, &Switch::toggled, this, [entry, userFolder](bool on) {
            if (!setAutostartEnabled(entry, on, userFolder))
                qWarning("tde-daedalus: cannot write %s/%s", qPrintable(userFolder), qPrintable(entry.id));
        });
        layout->addWidget(enabled);
        m_group->addRow(entry.name, entry.comment, controls);
    }
    if (entries.empty())
        m_group->addRow(u"Nothing starts at login"_s, u"Applications added here start when you log in"_s);
    m_group->addNote(u"Changes take effect the next time you log in."_s);
}

void StartupPage::add()
{
    tde::Dialog dialog(u"Add a Startup Application"_s, window());
    dialog.resize(420, 520);
    auto* search = new QLineEdit(&dialog);
    search->setPlaceholderText(u"Search applications"_s);
    auto* choices = new QListWidget(&dialog);
    choices->setIconSize(QSize(24, 24));
    const auto apps = shell::loadApplications(currentDesktops());
    for (const shell::Application& app : apps) {
        if (!app.inMenus)
            continue;
        auto* item = new QListWidgetItem(shell::applicationIcon(&app), app.name, choices);
        item->setData(Qt::UserRole, app.id);
        item->setToolTip(app.comment);
    }
    connect(search, &QLineEdit::textChanged, choices, [choices](const QString& text) {
        for (int i = 0; i < choices->count(); ++i)
            choices->item(i)->setHidden(!choices->item(i)->text().contains(text, Qt::CaseInsensitive));
    });
    auto* buttons = new QHBoxLayout;
    auto* cancel = new QPushButton(u"Cancel"_s, &dialog);
    auto* accept = new QPushButton(u"Add"_s, &dialog);
    accept->setEnabled(false);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(accept);
    dialog.contentLayout()->addWidget(search);
    dialog.contentLayout()->addWidget(choices, 1);
    dialog.contentLayout()->addLayout(buttons);
    dialog.setDefaultButton(accept);
    connect(choices, &QListWidget::currentItemChanged, accept,
        [accept](QListWidgetItem* item) { accept->setEnabled(item != nullptr); });
    connect(choices, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(accept, &QPushButton::clicked, &dialog, &QDialog::accept);
    search->setFocus();
    if (dialog.run() != QDialog::Accepted || !choices->currentItem())
        return;

    const QString id = choices->currentItem()->data(Qt::UserRole).toString();
    if (!addAutostart(id, shell::autostartFolders().first()))
        qWarning("tde-daedalus: cannot add %s to the startup applications", qPrintable(id));
    fill();
}

} // namespace daedalus
