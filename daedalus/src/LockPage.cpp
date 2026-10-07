#include "LockPage.hpp"

#include <tde/DesktopConfig.hpp>

#include <QComboBox>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <vector>

using namespace Qt::StringLiterals;

namespace daedalus {

bool setLockAfter(const QString& path, int minutes)
{
    tde::createDesktopConfig(path);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    QString text = QString::fromUtf8(file.readAll());
    file.close();

    const QString value = QString::number(minutes);
    // The lock table holds no tables of its own, so it ends at the first closing brace.
    static const QRegularExpression table(uR"(\block\s*=\s*\{[^}]*\})"_s);
    static const QRegularExpression after(uR"((\bafter\s*=\s*)-?\d+)"_s);
    if (const auto lock = table.match(text); lock.hasMatch()) {
        QString block = lock.captured();
        if (after.match(block).hasMatch())
            block.replace(after, u"\\1"_s + value);
        else
            block.insert(block.indexOf(u'{') + 1, u" after = %1,"_s.arg(value));
        text.replace(lock.capturedStart(), lock.capturedLength(), block);
    } else {
        // Into the table the file returns, at its end.
        const qsizetype end = text.lastIndexOf(u'}');
        if (end < 0)
            return false;
        text.insert(end, u"    lock = { after = %1 },\n"_s.arg(value));
    }

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    out.write(text.toUtf8());
    return out.commit();
}

LockPage::LockPage(QWidget* parent)
    : Page(parent)
{
    const int after = tde::loadDesktopConfig().lock.after;

    auto* enabled = new Switch(this);
    enabled->setChecked(after > 0);
    // As GNOME offers them, and whatever the config says besides.
    auto* delay = new QComboBox(this);
    std::vector<int> choices {1, 2, 3, 5, 10, 15, 30, 60};
    if (after > 0 && !std::ranges::contains(choices, after))
        choices.insert(std::ranges::upper_bound(choices, after), after);
    for (const int minutes : choices) {
        delay->addItem(minutes == 60 ? u"1 hour"_s
                : minutes == 1       ? u"1 minute"_s
                                     : u"%1 minutes"_s.arg(minutes),
            minutes);
    }
    delay->setCurrentIndex(std::max(0, delay->findData(after > 0 ? after : 5)));
    delay->setEnabled(after > 0);

    const auto save = [enabled, delay] {
        delay->setEnabled(enabled->isChecked());
        const int minutes = enabled->isChecked() ? delay->currentData().toInt() : 0;
        if (!setLockAfter(tde::desktopConfigPath(), minutes))
            qWarning("tde-daedalus: cannot write %s", qPrintable(tde::desktopConfigPath()));
    };
    connect(enabled, &Switch::toggled, this, save);
    connect(delay, &QComboBox::activated, this, save);

    Group* automatic = addGroup(u"Automatic Screen Lock"_s);
    automatic->addRow(u"Lock when not used"_s, u"Locks the screen after a while without input"_s, enabled);
    automatic->addRow(u"Delay"_s, u"How long without input before it locks"_s, delay);
    automatic->addNote(u"Super+L locks it at any time, and it locks before the computer sleeps. Programs such as "
                       "video players keep it from locking while they play."_s);
}

} // namespace daedalus
