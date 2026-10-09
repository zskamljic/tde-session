#include "Osd.hpp"

#include "Audio.hpp"
#include "Media.hpp"
#include "QuickSettings.hpp"
#include "SystemStatus.hpp"

#include <Icons.hpp>
#include <tde/Theme.hpp>

#include <LayerShellQt/Window>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace hermes {
namespace {

constexpr int Width = 260;
constexpr int Height = 52;
constexpr int Radius = Height / 2;
constexpr int IconSize = 20;
constexpr int ShownFor = 1500; // ms
constexpr double Step = 0.05;

// Whole steps, so that a level the keys reach is a level they can leave again.
double stepped(double level, double by)
{
    return std::clamp(std::round(level / Step + by / Step) * Step, 0.0, 1.0);
}

} // namespace

Osd::Osd(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(u"Level"_s);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(Width, Height);

    // Above everything, even windows in fullscreen, and never in the way of the keyboard.
    create();
    if (auto* layer = LayerShellQt::Window::get(windowHandle())) {
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorBottom));
        layer->setMargins(QMargins(0, 0, 0, 96));
        layer->setExclusiveZone(0);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layer->setScope(u"tde-hermes-osd"_s);
    }

    m_hide.setSingleShot(true);
    m_hide.setInterval(ShownFor);
    connect(&m_hide, &QTimer::timeout, this, &QWidget::hide);
}

void Osd::present(const QString& iconName, double level)
{
    m_icon = shell::tintedIcon(iconName, QSize(IconSize, IconSize), devicePixelRatioF(), tde::theme::colors().text);
    m_level = std::clamp(level, 0.0, 1.0);
    update();
    show();
    m_hide.start();
}

void Osd::paintEvent(QPaintEvent*)
{
    const auto& colors = tde::theme::colors();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.setPen(QPen(colors.border, 1));
    painter.setBrush(colors.window);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), Radius, Radius);

    const int iconLeft = (Height - IconSize) / 2;
    m_icon.paint(&painter, QRect(iconLeft + 4, (Height - IconSize) / 2, IconSize, IconSize));

    const QRectF track(iconLeft + IconSize + 18, Height / 2.0 - 3, Width - iconLeft - IconSize - 18 - Radius, 6);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.hover);
    painter.drawRoundedRect(track, 3, 3);
    if (m_level > 0) {
        painter.setBrush(colors.accent);
        painter.drawRoundedRect(QRectF(track.topLeft(), QSizeF(std::max(6.0, track.width() * m_level), 6)), 3, 3);
    }
}

// MediaKeys -------------------------------------------------------------------------------

MediaKeys::MediaKeys(Audio& audio, Brightness& brightness, Media& media, QObject* parent)
    : QObject(parent)
    , m_audio(audio)
    , m_brightness(brightness)
    , m_media(media)
    , m_osd(std::make_unique<Osd>())
{
}

MediaKeys::~MediaKeys() = default;

void MediaKeys::RaiseVolume()
{
    changeVolume(Step);
}

void MediaKeys::LowerVolume()
{
    changeVolume(-Step);
}

void MediaKeys::ToggleMute()
{
    if (!m_audio.isAvailable())
        return;
    const bool muted = !m_audio.isMuted();
    m_audio.setMuted(muted);
    m_osd->present(volumeIconName(m_audio.volume(), muted), muted ? 0 : m_audio.volume());
}

void MediaKeys::RaiseBrightness()
{
    changeBrightness(Step);
}

void MediaKeys::LowerBrightness()
{
    changeBrightness(-Step);
}

void MediaKeys::changeVolume(double by)
{
    if (!m_audio.isAvailable())
        return;
    // Turning it down keeps it muted; turning it up lets it be heard again.
    if (m_audio.isMuted() && by < 0) {
        m_osd->present(volumeIconName(m_audio.volume(), true), 0);
        return;
    }
    // The sound server answers later; what is shown is what was asked for.
    const double volume = stepped(m_audio.volume(), by);
    m_audio.setVolume(volume);
    m_osd->present(volumeIconName(volume, false), volume);
}

void MediaKeys::changeBrightness(double by)
{
    // The firmware may have changed it on its own.
    m_brightness.refresh();
    if (!m_brightness.isAvailable())
        return;
    m_brightness.setValue(stepped(m_brightness.value(), by));
    m_osd->present(u"display-brightness-symbolic"_s, m_brightness.value());
}

void MediaKeys::PlayPause()
{
    m_media.playPause();
}

void MediaKeys::Next()
{
    m_media.next();
}

void MediaKeys::Previous()
{
    m_media.previous();
}

void MediaKeys::Stop()
{
    m_media.stop();
}

} // namespace hermes
