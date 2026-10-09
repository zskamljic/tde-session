#include "DisplayLayouts.hpp"

#include <QGuiApplication>
#include <QScreen>

namespace hermes {

DisplayLayouts::DisplayLayouts(QObject* parent)
    : QObject(parent)
{
    m_retry.setSingleShot(true);
    m_retry.setInterval(1500);
    connect(&m_retry, &QTimer::timeout, this, &DisplayLayouts::arrange);
    connect(&m_displays, &shell::Displays::changed, this, [this] {
        arrange();
        findPrimary();
    });
    // Screens come and go after the displays were set, so the bar finds its screen then.
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &DisplayLayouts::findPrimary);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &DisplayLayouts::findPrimary);
    arrange();
    findPrimary();
}

void DisplayLayouts::setSettings(const shell::SessionConfig::Displays& settings)
{
    if (settings == m_settings)
        return;
    // Kept layouts may be new, or come in only now that the displays were seen already.
    m_settings = settings;
    m_plugged.clear();
    arrange();
    findPrimary();
}

void DisplayLayouts::arrange()
{
    // Only when other displays are plugged in: changes to the ones there are someone's doing,
    // such as Settings trying them out.
    const auto& displays = m_displays.displays();
    QStringList plugged = shell::identitiesOf(displays);
    plugged.sort();
    if (plugged == m_plugged)
        return;
    m_plugged = plugged;
    const shell::DisplayLayout* kept = m_settings.find(plugged);
    if (!kept)
        return;
    const std::vector<shell::DisplaySetting> wanted = shell::settingsOf(*kept, displays);
    if (!shell::sameSettings(wanted, m_displays.settings())) {
        m_displays.apply(wanted, [this](bool succeeded) {
            // Displays that changed meanwhile, as a dock's do one by one, are arranged again
            // with the next change, or a little later when none comes.
            if (succeeded) {
                m_failures = 0;
                return;
            }
            qWarning("tde-hermes: the displays could not be arranged as they were kept");
            m_plugged.clear();
            if (++m_failures <= 3)
                m_retry.start();
        });
    }
}

void DisplayLayouts::findPrimary()
{
    QScreen* found = shell::primaryScreen(m_settings, m_displays.displays());
    if (found != m_primary) {
        m_primary = found;
        emit primaryScreenChanged(found);
    }
}

} // namespace hermes
