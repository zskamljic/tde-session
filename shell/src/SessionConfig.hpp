#pragma once

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QStringList>

#include <vector>

namespace shell {

// Settings of the session, from ~/.config/tde/session/config.lua; the settings app writes it.
struct SessionConfig {
    struct Background {
        // How the picture covers the screen.
        enum class Mode { Fill, Fit, Stretch, Center, Tile };

        QString image; // none shows the colour alone
        Mode mode = Mode::Fill;
        QColor color {0x26, 0x2a, 0x33}; // around a picture that leaves room, or alone

        bool operator==(const Background&) const = default;
    } background;

    struct Switcher {
        // What Alt+Tab shows of every window.
        enum class Style { Previews, Icons };

        Style style = Style::Previews;

        bool operator==(const Switcher&) const = default;
    } switcher;

    // How long animations take, in milliseconds; 0 leaves them out.
    struct Animations {
        int overview = 250; // opening and closing the overview
        int flip = 300; // opening and closing Flip 3D
        int flipStep = 220; // turning to the next window in it
        int windows = 200; // windows moving into a tile, as when maximized

        bool operator==(const Animations&) const = default;
    } animations;

    // How displays are arranged, for each set of them plugged in together, and which one has
    // the bar.
    struct Displays {
        struct Placement {
            QString display; // its identity, as shell::Display tells it
            bool enabled = true;
            QSize size {}; // of its mode, in pixels
            int refresh = 0; // mHz
            QPoint position {};
            int scale = 100; // percent
            int transform = 0; // as wl_output has it

            bool operator==(const Placement&) const = default;
        };
        using Layout = std::vector<Placement>;

        QString primary; // empty for whichever comes first
        std::vector<Layout> layouts;

        // The layout for exactly these displays, if one was kept.
        const Layout* find(const QStringList& displays) const;
        // Keeps `layout`, in place of one for the same displays.
        void keep(Layout layout);

        bool operator==(const Displays&) const = default;
    } displays;

    struct Clock {
        bool seconds = false; // in the bar and on the lock screen

        bool operator==(const Clock&) const = default;
    } clock;

    // What happens after a while without input, in minutes; 0 never does it.
    struct Power {
        int blank = 5; // the screens turn off
        int suspend = 0; // the computer sleeps, plugged in
        int suspendOnBattery = 15; // and on battery

        bool operator==(const Power&) const = default;
    } power;

    // Mice and touchpads, which the compositor sets up.
    struct Input {
        struct Pointer {
            int speed = 0; // -100 to 100, percent around the usual
            int scrollSpeed = 100; // percent of the usual distance
            bool naturalScroll = false; // the content moves the way the fingers or the wheel do
            bool tapToClick = true; // touchpads only
            bool disableWhileTyping = true; // touchpads only

            bool operator==(const Pointer&) const = default;
        };

        Pointer mouse;
        Pointer touchpad {.naturalScroll = true};

        bool operator==(const Input&) const = default;
    } input;

    bool operator==(const SessionConfig&) const = default;
};

QString sessionConfigPath();
// Problems are reported on stderr; what is missing or wrong keeps its default.
SessionConfig loadSessionConfig(const QString& path = sessionConfigPath());
bool saveSessionConfig(const SessionConfig& config, const QString& path = sessionConfigPath());

// The background's picture, the way up the camera meant it, at `scale` of its size when that
// is below 1; null when it cannot be read, with the reason in `error`.
QImage loadBackgroundPicture(const QString& path, double scale = 1, QString* error = nullptr);
// "*.png" and the like, for every kind of picture there is a reader for.
QStringList pictureFilePatterns();
// The background as `background` has it, with `picture` (which may be null), at `size`.
QImage drawBackground(const SessionConfig::Background& background, const QImage& picture, QSize size);

} // namespace shell
