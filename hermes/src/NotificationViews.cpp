#include "NotificationViews.hpp"

#include "Notifications.hpp"

#include <tde/Theme.hpp>

#include <LayerShellQt/Window>

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace hermes {
namespace {

constexpr int BannerWidth = 420;
constexpr int ListWidth = 360;
constexpr int MaxBanners = 3;
constexpr int IconSize = 40;
constexpr qsizetype MaxBannerBody = 300;

QPixmap iconPixmap(const Notification& notification, qreal ratio)
{
    const QSize size(IconSize, IconSize);
    if (!notification.image.isNull()) {
        QPixmap pixmap = QPixmap::fromImage(
            notification.image.scaled(size * ratio, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        pixmap.setDevicePixelRatio(ratio);
        return pixmap;
    }
    QIcon icon;
    if (notification.icon.startsWith(u'/'))
        icon = QIcon(notification.icon);
    else if (!notification.icon.isEmpty())
        icon = QIcon::fromTheme(notification.icon);
    if (icon.isNull() && !notification.desktopEntry.isEmpty())
        icon = QIcon::fromTheme(notification.desktopEntry);
    if (icon.isNull())
        icon = QIcon::fromTheme(u"dialog-information"_s);
    return icon.pixmap(size, ratio);
}

QString timeText(const QDateTime& time)
{
    const qint64 seconds = time.secsTo(QDateTime::currentDateTime());
    if (seconds < 60)
        return u"now"_s;
    return QLocale().toString(time.time(), QLocale::ShortFormat);
}

} // namespace

NotificationCard::NotificationCard(
    NotificationServer& server, const Notification& notification, Mode mode, QWidget* parent)
    : QFrame(parent)
    , m_server(server)
    , m_id(notification.id)
    , m_hasDefault(notification.hasDefaultAction())
{
    const auto& colors = tde::theme::colors();
    setObjectName(u"NotificationCard"_s);
    setFixedWidth(mode == Mode::Banner ? BannerWidth : ListWidth);
    setCursor(Qt::PointingHandCursor);
    setStyleSheet(u"#NotificationCard { background: %1; border: 1px solid %2; border-radius: %3px; }"_s
            .arg((mode == Mode::Banner ? colors.header : colors.window).name(), colors.border.name())
            .arg(tde::theme::radius(tde::theme::RadiusSize::Large)));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 10, 8, 12);
    layout->setSpacing(12);

    auto* icon = new QLabel(this);
    icon->setPixmap(iconPixmap(notification, devicePixelRatioF()));
    icon->setFixedSize(IconSize, IconSize);
    icon->setAlignment(Qt::AlignCenter);
    layout->addWidget(icon, 0, Qt::AlignTop);

    auto* text = new QVBoxLayout;
    text->setSpacing(2);
    layout->addLayout(text, 1);

    auto* header = new QHBoxLayout;
    auto* app = new QLabel(notification.appName, this);
    auto* time = new QLabel(timeText(notification.time), this);
    for (QLabel* label : {app, time}) {
        label->setTextFormat(Qt::PlainText);
        label->setStyleSheet(u"color: %1; font-size: 9pt;"_s.arg(colors.dimText.name()));
    }
    auto* close = new QToolButton(this);
    close->setText(u"✕"_s);
    close->setAutoRaise(true);
    close->setCursor(Qt::ArrowCursor);
    close->setToolTip(u"Dismiss"_s);
    connect(close, &QToolButton::clicked, this, [this] {
        const uint id = m_id;
        NotificationServer& server = m_server;
        emit handled();
        server.dismiss(id); // this card may be gone afterwards
    });
    header->addWidget(app);
    header->addStretch();
    header->addWidget(time);
    header->addWidget(close);
    text->addLayout(header);

    auto* summary = new QLabel(notification.summary, this);
    summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(true);
    summary->setStyleSheet(u"font-weight: bold;"_s);
    text->addWidget(summary);

    // Only the little markup the spec allows, and in a banner only the start of a long text.
    if (!notification.body.isEmpty()) {
        auto* label = new QLabel(bodyMarkup(notification.body, mode == Mode::Banner ? MaxBannerBody : -1), this);
        label->setTextFormat(Qt::RichText);
        label->setOpenExternalLinks(true);
        label->setWordWrap(true);
        text->addWidget(label);
    }

    // The actions other than the default one, which a click on the notification picks.
    auto* actions = new QHBoxLayout;
    actions->setContentsMargins(0, 6, 0, 0);
    const QStringList list = notification.actions;
    for (qsizetype i = 0; i + 1 < list.size(); i += 2) {
        if (list[i] == u"default")
            continue;
        auto* button = new QPushButton(list[i + 1], this);
        button->setCursor(Qt::ArrowCursor);
        const QString key = list[i];
        connect(button, &QPushButton::clicked, this, [this, key] {
            const uint id = m_id;
            emit handled();
            emit actionPicked(id, key);
        });
        actions->addWidget(button);
    }
    if (actions->count() > 0)
        text->addLayout(actions);
    else
        delete actions;

    if (mode == Mode::Banner) {
        const int duration = notification.bannerTime();
        m_expiry.setSingleShot(true);
        m_expiry.setInterval(duration);
        connect(&m_expiry, &QTimer::timeout, this, [this] { m_server.expire(m_id); });
        if (duration > 0)
            m_expiry.start();
    }
}

void NotificationCard::retire()
{
    m_expiry.stop();
    hide();
    deleteLater();
}

void NotificationCard::enterEvent(QEnterEvent* event)
{
    // Being read: it waits.
    m_expiry.stop();
    QFrame::enterEvent(event);
}

void NotificationCard::leaveEvent(QEvent* event)
{
    if (m_expiry.interval() > 0 && !m_expiry.isActive())
        m_expiry.start();
    QFrame::leaveEvent(event);
}

void NotificationCard::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !rect().contains(event->position().toPoint()))
        return;
    const uint id = m_id;
    const bool hasDefault = m_hasDefault;
    NotificationServer& server = m_server;
    emit handled();
    // Its program is asked to open what it is about; without a default action, the bar
    // still brings its window forward.
    emit actionPicked(id, hasDefault ? u"default"_s : QString());
    if (!hasDefault)
        server.dismiss(id);
}

// Banners ---------------------------------------------------------------------------------

Banners::Banners(NotificationServer& server, QWidget* parent)
    : QWidget(parent)
    , m_server(server)
{
    setWindowTitle(u"Notifications"_s);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_AlwaysShowToolTips);
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(8);
    m_layout->setSizeConstraint(QLayout::SetFixedSize);

    // In the top right corner of the screen, below the bar, which keeps its space.
    create();
    if (auto* layer = LayerShellQt::Window::get(windowHandle())) {
        layer->setLayer(LayerShellQt::Window::LayerTop);
        layer->setAnchors(
            LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorRight));
        layer->setMargins(QMargins(0, 8, 8, 0));
        layer->setExclusiveZone(0);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layer->setScope(u"tde-hermes-notifications"_s);
    }

    connect(&m_server, &NotificationServer::changed, this, &Banners::sync);
    connect(&m_server, &NotificationServer::replaced, this, [this](uint id) {
        // A replaced notification shows anew, with its time starting over: its card goes, and
        // the sync that follows makes a new one.
        if (const auto it = m_cards.find(id); it != m_cards.end()) {
            m_layout->removeWidget(it->second);
            it->second->retire();
            m_cards.erase(it);
        }
    });
}

void Banners::sync()
{
    std::vector<uint> wanted;
    for (const Notification& notification : m_server.notifications()) {
        if (notification.banner && wanted.size() < MaxBanners)
            wanted.push_back(notification.id);
    }

    for (auto it = m_cards.begin(); it != m_cards.end();) {
        if (std::ranges::contains(wanted, it->first)) {
            ++it;
        } else {
            m_layout->removeWidget(it->second);
            it->second->retire();
            it = m_cards.erase(it);
        }
    }
    for (size_t i = 0; i < wanted.size(); ++i) {
        NotificationCard*& card = m_cards[wanted[i]];
        if (!card) {
            card = new NotificationCard(m_server, *m_server.find(wanted[i]), NotificationCard::Mode::Banner, this);
            connect(card, &NotificationCard::actionPicked, this, &Banners::actionPicked);
        }
        // Newest on top.
        m_layout->removeWidget(card);
        m_layout->insertWidget(int(i), card);
    }

    if (m_cards.empty()) {
        hide();
        return;
    }
    adjustSize();
    show();
}

// The list --------------------------------------------------------------------------------

NotificationList::NotificationList(NotificationServer& server, QWidget* parent)
    : QWidget(parent)
    , m_server(server)
{
    setFixedSize(ListWidth + 24, 380);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(u"QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"_s);
    auto* content = new QWidget(scroll);
    m_cards = new QVBoxLayout(content);
    m_cards->setContentsMargins(0, 0, 0, 0);
    m_cards->setSpacing(8);
    m_cards->addStretch();
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    m_scroll = scroll;

    m_empty = new QLabel(u"No Notifications"_s, this);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setStyleSheet(u"color: %1;"_s.arg(tde::theme::colors().dimText.name()));
    layout->addWidget(m_empty, 1);

    auto* clear = new QPushButton(u"Clear"_s, this);
    connect(clear, &QPushButton::clicked, this, [this] { m_server.dismissAll(); });
    m_clear = clear;
    layout->addWidget(clear, 0, Qt::AlignRight);

    connect(&m_server, &NotificationServer::changed, this, &NotificationList::rebuild);
    rebuild();
}

void NotificationList::rebuild()
{
    // Everything but the stretch at the end.
    while (m_cards->count() > 1) {
        QLayoutItem* item = m_cards->takeAt(0);
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    int shown = 0;
    for (const Notification& notification : m_server.notifications()) {
        if (notification.banner)
            continue;
        auto* card = new NotificationCard(m_server, notification, NotificationCard::Mode::List);
        connect(card, &NotificationCard::handled, this, &NotificationList::handled);
        connect(card, &NotificationCard::actionPicked, this, &NotificationList::actionPicked);
        m_cards->insertWidget(m_cards->count() - 1, card);
        ++shown;
    }
    m_scroll->setVisible(shown > 0);
    m_empty->setVisible(shown == 0);
    m_clear->setVisible(shown > 0);
}

} // namespace hermes
