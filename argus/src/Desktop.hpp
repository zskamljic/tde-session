#pragma once

#include "Background.hpp"
#include "Overview.hpp"

#include <Displays.hpp>

#include <QObject>
#include <QPointer>
#include <QScreen>

#include <memory>
#include <vector>

namespace argus {

// What Argus shows on every screen: the background, and the overview with the windows on
// that screen, searching and the applications on the primary one. Its scriptable slots are
// the D-Bus methods for the overview.
class Desktop : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.zskamljic.Argus")

public:
    Desktop(Toplevels& toplevels, const Wallpaper& wallpaper, QObject* parent = nullptr);
    ~Desktop() override;

    // The screen of the bar, where the window switchers show too.
    QScreen* primaryScreen() const { return m_primary; }

    void setSettings(const shell::SessionConfig& config);

public slots:
    Q_SCRIPTABLE void Toggle();
    Q_SCRIPTABLE void Show();
    Q_SCRIPTABLE void Hide();
    // Shows all applications, or hides the overview when they are shown.
    Q_SCRIPTABLE void ToggleApplications();

signals:
    void primaryScreenChanged(QScreen* screen);

private:
    struct Screen {
        QScreen* screen = nullptr;
        std::unique_ptr<Background> background {};
        std::unique_ptr<Overview> overview {};
    };

    // Screens came or went, or another one is primary: each has its parts, on the right one.
    void sync();
    Overview* primaryOverview() const;

    Toplevels& m_toplevels;
    const Wallpaper& m_wallpaper;
    shell::Displays m_displays;
    shell::SessionConfig::Displays m_displaySettings;
    int m_animationTime = 250;
    QPointer<QScreen> m_primary;
    std::vector<Screen> m_screens;
};

} // namespace argus
