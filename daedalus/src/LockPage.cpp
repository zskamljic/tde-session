#include "LockPage.hpp"

#include <DesktopSettings.hpp>

#include <tde/DesktopConfig.hpp>

#include <QComboBox>

#include <algorithm>
#include <vector>

using namespace Qt::StringLiterals;

namespace daedalus {

bool setLockAfter(const QString& path, int minutes)
{
    return shell::setDesktopSetting(path, u"lock"_s, u"after"_s, QString::number(minutes));
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
