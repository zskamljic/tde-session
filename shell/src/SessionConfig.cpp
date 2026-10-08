#include "SessionConfig.hpp"

#include <tde/LuaConfig.hpp>

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace shell {
namespace {

using Mode = SessionConfig::Background::Mode;
using Style = SessionConfig::Switcher::Style;

constexpr int MaxDuration = 2000; // ms
constexpr int MaxPixels = 100'000;

constexpr std::pair<QStringView, Mode> modes[] = {
    {u"fill", Mode::Fill},
    {u"fit", Mode::Fit},
    {u"stretch", Mode::Stretch},
    {u"center", Mode::Center},
    {u"tile", Mode::Tile},
};

constexpr std::pair<QStringView, Style> styles[] = {
    {u"previews", Style::Previews},
    {u"icons", Style::Icons},
};

QStringList displaysOf(const SessionConfig::Displays::Layout& layout)
{
    QStringList displays;
    for (const auto& placement : layout)
        displays << placement.display;
    displays.sort();
    return displays;
}

} // namespace

const SessionConfig::Displays::Layout* SessionConfig::Displays::find(const QStringList& displays) const
{
    QStringList wanted = displays;
    wanted.sort();
    const auto it = std::ranges::find_if(layouts, [&](const Layout& layout) { return displaysOf(layout) == wanted; });
    return it == layouts.end() ? nullptr : &*it;
}

void SessionConfig::Displays::keep(Layout layout)
{
    const QStringList displays = displaysOf(layout);
    std::erase_if(layouts, [&](const Layout& other) { return displaysOf(other) == displays; });
    layouts.push_back(std::move(layout));
}

QString sessionConfigPath()
{
    return tde::configDirectory() + u"/session/config.lua"_s;
}

SessionConfig loadSessionConfig(const QString& path)
{
    SessionConfig config;
    tde::loadLuaConfig(path, [&](tde::LuaTableReader& reader) {
        reader.table("background", [&] {
            auto& background = config.background;
            if (const auto image = reader.string("image"))
                background.image = *image;
            if (const auto mode = reader.choice<Mode>("mode", modes))
                background.mode = *mode;
            if (const auto color = reader.string("color")) {
                if (QColor::isValidColorName(*color))
                    background.color = QColor::fromString(*color);
                else
                    reader.warn("color", u"expected \"#rrggbb\", not \"%1\""_s.arg(*color));
            }
        });
        reader.table("switcher", [&] {
            if (const auto style = reader.choice<Style>("style", styles))
                config.switcher.style = *style;
        });
        reader.table("animations", [&] {
            auto& animations = config.animations;
            if (const auto overview = reader.integer("overview", 0, MaxDuration))
                animations.overview = *overview;
            if (const auto flip = reader.integer("flip", 0, MaxDuration))
                animations.flip = *flip;
            if (const auto step = reader.integer("flip_step", 0, MaxDuration))
                animations.flipStep = *step;
        });
        reader.table("displays", [&] {
            auto& displays = config.displays;
            if (const auto primary = reader.string("primary"))
                displays.primary = *primary;
            reader.table("layouts", [&] {
                reader.forEachArrayTable([&](int) {
                    SessionConfig::Displays::Layout layout;
                    reader.forEachArrayTable([&](int) {
                        SessionConfig::Displays::Placement placement;
                        if (const auto display = reader.string("display"))
                            placement.display = *display;
                        if (const auto enabled = reader.boolean("enabled"))
                            placement.enabled = *enabled;
                        const auto width = reader.integer("width", 1, MaxPixels);
                        const auto height = reader.integer("height", 1, MaxPixels);
                        if (width && height)
                            placement.size = QSize(*width, *height);
                        if (const auto refresh = reader.integer("refresh", 0, 1'000'000))
                            placement.refresh = *refresh;
                        const auto x = reader.integer("x", -MaxPixels, MaxPixels);
                        const auto y = reader.integer("y", -MaxPixels, MaxPixels);
                        placement.position = QPoint(x.value_or(0), y.value_or(0));
                        if (const auto scale = reader.integer("scale", 25, 500))
                            placement.scale = *scale;
                        if (const auto transform = reader.integer("transform", 0, 7))
                            placement.transform = *transform;
                        if (!placement.display.isEmpty())
                            layout.push_back(placement);
                    });
                    if (!layout.empty())
                        displays.keep(std::move(layout));
                });
            });
        });
        reader.table("clock", [&] {
            if (const auto seconds = reader.boolean("seconds"))
                config.clock.seconds = *seconds;
        });
        reader.table("power", [&] {
            constexpr int MaxMinutes = 24 * 60;
            if (const auto blank = reader.integer("blank", 0, MaxMinutes))
                config.power.blank = *blank;
            if (const auto suspend = reader.integer("suspend", 0, MaxMinutes))
                config.power.suspend = *suspend;
            if (const auto onBattery = reader.integer("suspend_on_battery", 0, MaxMinutes))
                config.power.suspendOnBattery = *onBattery;
        });
    });
    return config;
}

bool saveSessionConfig(const SessionConfig& config, const QString& path)
{
    const auto& background = config.background;
    QStringList layouts;
    for (const auto& layout : config.displays.layouts) {
        layouts << u"            {"_s;
        for (const auto& placement : layout) {
            layouts << u"                { display = %1, enabled = %2, width = %3, height = %4, refresh = %5, x = %6, "
                       "y = %7, scale = %8, transform = %9 },"_s.arg(tde::luaString(placement.display),
                           placement.enabled ? u"true"_s : u"false"_s, QString::number(placement.size.width()),
                           QString::number(placement.size.height()), QString::number(placement.refresh),
                           QString::number(placement.position.x()), QString::number(placement.position.y()),
                           QString::number(placement.scale), QString::number(placement.transform));
        }
        layouts << u"            },"_s;
    }
    QStringList lines {
        u"-- Settings of the TDE session, written by Daedalus (Settings); editing them here works too."_s,
        u"return {"_s,
        u"    background = {"_s,
        u"        -- A picture for the desktop; without one, the colour fills it."_s,
        u"        image = %1,"_s.arg(tde::luaString(background.image)),
        u"        -- How the picture covers the screen: \"fill\", \"fit\", \"stretch\", \"center\" or \"tile\"."_s,
        u"        mode = %1,"_s.arg(tde::luaString(tde::choiceName<Mode>(modes, background.mode))),
        u"        -- Around a picture that leaves room, as \"#rrggbb\"."_s,
        u"        color = %1,"_s.arg(tde::luaString(background.color.name())),
        u"    },"_s,
        u"    switcher = {"_s,
        u"        -- What Alt+Tab shows of every window: \"previews\" or \"icons\"."_s,
        u"        style = %1,"_s.arg(tde::luaString(tde::choiceName<Style>(styles, config.switcher.style))),
        u"    },"_s,
        u"    animations = {"_s,
        u"        -- How long they take, in milliseconds, up to %1; 0 leaves them out."_s.arg(MaxDuration),
        u"        overview = %1, -- opening and closing the overview"_s.arg(config.animations.overview),
        u"        flip = %1, -- opening and closing Flip 3D"_s.arg(config.animations.flip),
        u"        flip_step = %1, -- turning to the next window in it"_s.arg(config.animations.flipStep),
        u"    },"_s,
        u"    displays = {"_s,
        u"        -- The display with the bar, by its make, model and serial number; none for the first."_s,
        u"        primary = %1,"_s.arg(tde::luaString(config.displays.primary)),
        u"        -- How displays are arranged, for each set of them plugged in together: sizes and"_s,
        u"        -- places in pixels, refresh rates in mHz, scales in percent, turns as wl_output has them."_s,
        u"        layouts = {"_s,
    };
    lines << layouts;
    lines << QStringList {
        u"        },"_s,
        u"    },"_s,
        u"    clock = {"_s,
        u"        -- Whether the clocks of the bar and the lock screen show seconds."_s,
        u"        seconds = %1,"_s.arg(config.clock.seconds ? u"true"_s : u"false"_s),
        u"    },"_s,
        u"    power = {"_s,
        u"        -- Minutes without input until it happens; 0 never does it."_s,
        u"        blank = %1, -- the screens turn off"_s.arg(config.power.blank),
        u"        suspend = %1, -- the computer sleeps, plugged in"_s.arg(config.power.suspend),
        u"        suspend_on_battery = %1, -- and on battery"_s.arg(config.power.suspendOnBattery),
        u"    },"_s,
        u"}"_s,
    };
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    file.write((lines.join(u'\n') + u'\n').toUtf8());
    return file.commit();
}

QImage loadBackgroundPicture(const QString& path, double scale, QString* error)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    // Only as much of a large picture as is wanted is read.
    if (const QSize full = reader.size(); scale < 1 && full.isValid())
        reader.setScaledSize((QSizeF(full) * scale).toSize().expandedTo(QSize(1, 1)));
    QImage picture = reader.read();
    if (picture.isNull() && error)
        *error = reader.errorString();
    return picture;
}

QStringList pictureFilePatterns()
{
    QStringList patterns;
    for (const QByteArray& format : QImageReader::supportedImageFormats())
        patterns << u"*."_s + QString::fromLatin1(format);
    return patterns;
}

QImage drawBackground(const SessionConfig::Background& background, const QImage& picture, QSize size)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(background.color);
    if (picture.isNull() || size.isEmpty())
        return image;

    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRect area(QPoint(), size);
    const auto centred = [&](const QSize& pictureSize) {
        QRect rect(QPoint(), pictureSize);
        rect.moveCenter(area.center());
        return rect;
    };
    switch (background.mode) {
    case Mode::Fill:
        painter.drawImage(centred(picture.size().scaled(size, Qt::KeepAspectRatioByExpanding)), picture);
        break;
    case Mode::Fit:
        painter.drawImage(centred(picture.size().scaled(size, Qt::KeepAspectRatio)), picture);
        break;
    case Mode::Stretch:
        painter.drawImage(area, picture);
        break;
    case Mode::Center:
        painter.drawImage(centred(picture.size()), picture);
        break;
    case Mode::Tile:
        painter.fillRect(area, QBrush(picture));
        break;
    }
    return image;
}

} // namespace shell
