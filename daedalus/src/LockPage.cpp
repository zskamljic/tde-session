#include "LockPage.hpp"

#include "Fingerprints.hpp"

#include <DesktopSettings.hpp>

#include <tde/DesktopConfig.hpp>

#include <QComboBox>
#include <QSignalBlocker>

#include <algorithm>
#include <memory>
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
    const auto lock = tde::loadDesktopConfig().lock;
    const int after = lock.after;

    m_enabled = new Switch(this);
    m_enabled->setChecked(after > 0);
    // As GNOME offers them, and whatever the config says besides.
    m_delay = new QComboBox(this);
    std::vector<int> choices {1, 2, 3, 5, 10, 15, 30, 60};
    if (after > 0 && !std::ranges::contains(choices, after))
        choices.insert(std::ranges::upper_bound(choices, after), after);
    for (const int minutes : choices) {
        m_delay->addItem(minutes == 60 ? u"1 hour"_s
                : minutes == 1         ? u"1 minute"_s
                                       : u"%1 minutes"_s.arg(minutes),
            minutes);
    }
    m_delay->setCurrentIndex(std::max(0, m_delay->findData(after > 0 ? after : 5)));
    m_delay->setEnabled(after > 0);

    const auto save = [this] {
        m_delay->setEnabled(m_enabled->isChecked());
        const int minutes = m_enabled->isChecked() ? m_delay->currentData().toInt() : 0;
        if (!setLockAfter(tde::desktopConfigPath(), minutes))
            qWarning("tde-daedalus: cannot write %s", qPrintable(tde::desktopConfigPath()));
    };
    connect(m_enabled, &Switch::toggled, this, save);
    connect(m_delay, &QComboBox::activated, this, save);

    m_beforeSleep = new Switch(this);
    m_beforeSleep->setChecked(lock.beforeSleep);
    connect(m_beforeSleep, &Switch::toggled, this, [](bool on) {
        if (!shell::setDesktopSetting(
                tde::desktopConfigPath(), u"lock"_s, u"before_sleep"_s, on ? u"true"_s : u"false"_s))
            qWarning("tde-daedalus: cannot write %s", qPrintable(tde::desktopConfigPath()));
    });

    Group* automatic = addGroup(u"Automatic Screen Lock"_s);
    automatic->addRow(u"Lock when not used"_s, u"Locks the screen after a while without input"_s, m_enabled);
    automatic->addRow(u"Delay"_s, u"How long without input before it locks"_s, m_delay);
    automatic->addRow(
        u"Lock before sleeping"_s, u"So the screen is locked when the computer wakes up"_s, m_beforeSleep);
    automatic->addNote(u"Super+L locks it at any time. Programs such as video players keep it from locking while "
                       "they play."_s);

    // With a fingerprint reader, fingers to unlock with.
    if (auto fingerprints = std::make_unique<FingerprintGroup>(); fingerprints->isAvailable())
        addWidget(std::move(fingerprints));
}

void LockPage::desktopChanged(const tde::DesktopConfig& config)
{
    const QSignalBlocker enabled(m_enabled);
    const QSignalBlocker beforeSleep(m_beforeSleep);
    m_enabled->setChecked(config.lock.after > 0);
    m_delay->setEnabled(config.lock.after > 0);
    if (const int index = m_delay->findData(config.lock.after); index >= 0)
        m_delay->setCurrentIndex(index);
    m_beforeSleep->setChecked(config.lock.beforeSleep);
}

} // namespace daedalus
