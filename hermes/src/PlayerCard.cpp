#include "PlayerCard.hpp"

#include "Media.hpp"

#include <Icons.hpp>
#include <tde/Theme.hpp>

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace hermes {
namespace {

constexpr int ArtSize = 56;
constexpr int IconSize = 16;

QToolButton* controlButton(const QString& tooltip, QWidget* parent)
{
    const auto& colors = tde::theme::colors();
    auto* button = new QToolButton(parent);
    button->setAutoRaise(true);
    button->setToolTip(tooltip);
    button->setIconSize(QSize(IconSize, IconSize));
    button->setFixedSize(32, 32);
    button->setStyleSheet(u"QToolButton { background: transparent; border: none; border-radius: 16px; }"
                          " QToolButton:hover { background: %1; }"
                          " QToolButton:pressed { background: %2; }"_s.arg(
                              colors.hover.name(QColor::HexArgb), colors.pressed.name(QColor::HexArgb)));
    return button;
}

// The cover with rounded corners, filling its square; the program's icon without one.
QPixmap coverOf(const QImage& art, const QIcon& fallback, qreal ratio)
{
    QPixmap pixmap(QSize(ArtSize, ArtSize) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath shape;
    shape.addRoundedRect(QRectF(0, 0, ArtSize, ArtSize), 8, 8);
    if (art.isNull()) {
        painter.fillPath(shape, tde::theme::colors().pressed);
        fallback.paint(&painter, QRect(12, 12, ArtSize - 24, ArtSize - 24));
    } else {
        painter.setClipPath(shape);
        const QSize size = art.size().scaled(QSize(ArtSize, ArtSize), Qt::KeepAspectRatioByExpanding);
        painter.drawImage(QRect(QPoint((ArtSize - size.width()) / 2, (ArtSize - size.height()) / 2), size), art);
    }
    return pixmap;
}

} // namespace

PlayerCard::PlayerCard(Media& media, QWidget* parent)
    : QFrame(parent)
    , m_media(media)
{
    const auto& colors = tde::theme::colors();
    setObjectName(u"PlayerCard"_s);
    setStyleSheet(u"#PlayerCard { background: %1; border: 1px solid %2; border-radius: %3px; }"_s
            .arg(colors.window.name(), colors.border.name())
            .arg(tde::theme::radius(tde::theme::RadiusSize::Large)));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);
    m_art = new QLabel(this);
    m_art->setFixedSize(ArtSize, ArtSize);
    layout->addWidget(m_art);

    auto* column = new QVBoxLayout;
    column->setSpacing(2);
    m_title = new QLabel(this);
    m_title->setTextFormat(Qt::PlainText);
    m_title->setStyleSheet(u"font-weight: bold;"_s);
    m_artist = new QLabel(this);
    m_artist->setTextFormat(Qt::PlainText);
    m_artist->setStyleSheet(u"color: %1;"_s.arg(colors.dimText.name()));
    column->addWidget(m_title);
    column->addWidget(m_artist);
    auto* controls = new QHBoxLayout;
    controls->setSpacing(4);
    m_previous = controlButton(u"Previous"_s, this);
    m_play = controlButton(u"Play"_s, this);
    m_next = controlButton(u"Next"_s, this);
    connect(m_previous, &QToolButton::clicked, &m_media, &Media::previous);
    connect(m_play, &QToolButton::clicked, &m_media, &Media::playPause);
    connect(m_next, &QToolButton::clicked, &m_media, &Media::next);
    controls->addWidget(m_previous);
    controls->addWidget(m_play);
    controls->addWidget(m_next);
    controls->addStretch(1);
    column->addLayout(controls);
    layout->addLayout(column, 1);

    connect(&m_media, &Media::changed, this, &PlayerCard::sync);
    sync();
}

void PlayerCard::resizeEvent(QResizeEvent* event)
{
    QFrame::resizeEvent(event);
    sync();
}

void PlayerCard::sync()
{
    const Player* player = m_media.current();
    setVisible(player);
    if (!player)
        return;
    const qreal ratio = devicePixelRatioF();
    const QColor text = tde::theme::colors().text;
    const auto icon
        = [&](const QString& name) { return shell::tintedIcon(name, QSize(IconSize, IconSize), ratio, text); };
    const QIcon appIcon = QIcon::fromTheme(player->desktopEntry, QIcon::fromTheme(u"multimedia-player"_s));
    m_art->setPixmap(coverOf(m_media.art(), appIcon, ratio));
    // As wide as there is room for, cut short with an ellipsis.
    const int room = std::max(80, width() - ArtSize - 40);
    const QString title = player->title.isEmpty() ? player->identity : player->title;
    m_title->setText(m_title->fontMetrics().elidedText(title, Qt::ElideRight, room));
    m_title->setToolTip(title);
    const QString artist = player->artist.isEmpty() && !player->title.isEmpty() ? player->identity : player->artist;
    m_artist->setText(m_artist->fontMetrics().elidedText(artist, Qt::ElideRight, room));
    m_artist->setVisible(!artist.isEmpty());
    m_previous->setIcon(icon(u"media-skip-backward-symbolic"_s));
    m_previous->setEnabled(player->canGoPrevious);
    m_play->setIcon(icon(player->playing ? u"media-playback-pause-symbolic"_s : u"media-playback-start-symbolic"_s));
    m_play->setToolTip(player->playing ? u"Pause"_s : u"Play"_s);
    m_play->setEnabled(player->canControl);
    m_next->setIcon(icon(u"media-skip-forward-symbolic"_s));
    m_next->setEnabled(player->canGoNext);
}

} // namespace hermes
