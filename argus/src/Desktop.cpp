#include "Desktop.hpp"

#include <QGuiApplication>
#include <QScreen>

#include <algorithm>

namespace argus {

Desktop::Desktop(Toplevels& toplevels, const Wallpaper& wallpaper, QObject* parent)
    : QObject(parent)
    , m_toplevels(toplevels)
    , m_wallpaper(wallpaper)
{
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &Desktop::sync);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &Desktop::sync);
    connect(&m_displays, &shell::Displays::changed, this, &Desktop::sync);
    sync();
}

Desktop::~Desktop() = default;

void Desktop::setSettings(const shell::SessionConfig& config)
{
    m_animationTime = config.animations.overview;
    for (const Screen& screen : m_screens)
        screen.overview->setAnimationTime(m_animationTime);
    if (config.displays != m_displaySettings) {
        m_displaySettings = config.displays;
        sync();
    }
}

void Desktop::sync()
{
    const QList<QScreen*> screens = QGuiApplication::screens();
    QScreen* primary = shell::primaryScreen(m_displaySettings, m_displays.displays());

    // Screens that went take their parts along; new ones get theirs.
    std::erase_if(m_screens, [&](const Screen& entry) { return !screens.contains(entry.screen); });
    for (QScreen* screen : screens) {
        if (std::ranges::contains(m_screens, screen, &Screen::screen))
            continue;
        Screen& entry = m_screens.emplace_back(Screen {.screen = screen});
        entry.background = std::make_unique<Background>(m_wallpaper, screen);
        entry.background->show();
        entry.overview = std::make_unique<Overview>(m_toplevels, m_wallpaper, screen, screen == primary);
        entry.overview->setAnimationTime(m_animationTime);
        connect(entry.overview.get(), &Overview::closeRequested, this, [this](quint64 chosen) {
            for (const Screen& other : m_screens)
                other.overview->closeOnto(chosen);
        });
        // Windows dragged from one screen's overview to another's go to that screen.
        connect(entry.overview.get(), &Overview::windowDragged, this, [this, screen](quint64 id, QPoint global) {
            for (const Screen& other : m_screens) {
                const bool over = other.screen != screen && other.screen->geometry().contains(global);
                other.overview->showDrop(
                    id, over ? std::optional(global - other.screen->geometry().topLeft()) : std::nullopt);
            }
        });
        connect(entry.overview.get(), &Overview::windowDropped, this, [this, screen](quint64 id, QPoint global) {
            for (const Screen& other : m_screens)
                other.overview->showDrop(id, std::nullopt);
            QScreen* target = QGuiApplication::screenAt(global);
            if (!target || target == screen)
                return;
            m_toplevels.moveToScreen(id, target);
            // Laid out again where the windows are now.
            m_toplevels.refresh(this, [this] {
                for (const Screen& other : m_screens)
                    other.overview->windowsMoved();
            });
        });
    }
    for (const Screen& entry : m_screens)
        entry.overview->setPrimary(entry.screen == primary);

    if (primary != m_primary) {
        m_primary = primary;
        emit primaryScreenChanged(primary);
    }
}

Overview* Desktop::primaryOverview() const
{
    const auto it = std::ranges::find(m_screens, m_primary.data(), &Screen::screen);
    return it == m_screens.end() ? nullptr : it->overview.get();
}

void Desktop::Toggle()
{
    if (std::ranges::any_of(m_screens, [](const Screen& screen) { return screen.overview->isOpen(); }))
        Hide();
    else
        Show();
}

void Desktop::Show()
{
    for (const Screen& screen : m_screens)
        screen.overview->Show();
}

void Desktop::Hide()
{
    for (const Screen& screen : m_screens)
        screen.overview->closeOnto(0);
}

void Desktop::ToggleApplications()
{
    Overview* primary = primaryOverview();
    if (!primary || primary->showsAllApplications()) {
        Hide();
        return;
    }
    Show();
    primary->showApplications();
}

} // namespace argus
