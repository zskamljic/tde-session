#include "Taskbar.hpp"

#include <Icons.hpp>
#include <tde/Theme.hpp>

#include <QGuiApplication>
#include <QHelpEvent>
#include <QIcon>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QTextDocument>
#include <QToolTip>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace hermes {
namespace {

constexpr int ButtonWidth = 44;
constexpr int IconSize = 24;
constexpr int MaxDots = 3;

} // namespace

Taskbar::Taskbar(Windows& windows, QWidget* parent)
    : QWidget(parent)
    , m_windows(windows)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    // The buttons point into the applications, which are new now.
    connect(&m_catalog, &shell::Catalog::changed, this, &Taskbar::rebuild);
    connect(&m_windows, &Windows::changed, this, &Taskbar::rebuild);
    rebuild();
}

std::vector<OpenWindow> Taskbar::openWindows() const
{
    // The bar's own dialogs, like the one asking before logging out, are no applications.
    const QString own = QGuiApplication::desktopFileName().isEmpty() ? QCoreApplication::applicationName()
                                                                     : QGuiApplication::desktopFileName();
    std::vector<OpenWindow> result;
    for (const Window* window : m_windows.windows()) {
        if (window->appId != own)
            result.push_back({window->id, window->appId, window->activated, window->minimized});
    }
    return result;
}

void Taskbar::rebuild()
{
    m_groups = groupWindows(openWindows(), m_catalog.applications());
    m_hovered = std::min(m_hovered, int(m_groups.size()) - 1);
    updateGeometry();
    update();
}

QSize Taskbar::sizeHint() const
{
    return {int(m_groups.size()) * ButtonWidth, IconSize};
}

QRect Taskbar::buttonRect(int index) const
{
    return {index * ButtonWidth, 0, ButtonWidth, height()};
}

int Taskbar::indexAt(QPoint pos) const
{
    const int index = pos.x() / ButtonWidth;
    return pos.x() >= 0 && index < int(m_groups.size()) && rect().contains(pos) ? index : -1;
}

void Taskbar::paintEvent(QPaintEvent*)
{
    const auto& colors = tde::theme::colors();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const int radius = tde::theme::radius();

    for (int i = 0; i < int(m_groups.size()); ++i) {
        const Group& group = m_groups[size_t(i)];
        const QRect button = buttonRect(i).adjusted(2, 3, -2, -3);
        bool active = false;
        QString appId;
        for (const quint64 id : group.windows) {
            if (const Window* window = m_windows.find(id)) {
                active = active || (window->activated && !window->minimized);
                appId = window->appId;
            }
        }

        // Buttons stand out a little, the active one more, like those of Windows 7.
        QColor background;
        if (active) {
            background = colors.accent;
            background.setAlphaF(i == m_hovered ? 0.5 : 0.35);
        } else if (i == m_hovered) {
            background = colors.hover;
        } else {
            background = colors.text;
            background.setAlphaF(0.08);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        painter.drawRoundedRect(button, radius, radius);

        const QRect icon(
            button.center().x() - IconSize / 2 + 1, button.center().y() - IconSize / 2, IconSize, IconSize);
        shell::applicationIcon(group.app, appId.toLower()).paint(&painter, icon);

        // A dot per window, up to three.
        const int dots = std::min(int(group.windows.size()), MaxDots);
        constexpr int dot = 4;
        constexpr int gap = 3;
        int x = button.center().x() + 1 - (dots * dot + (dots - 1) * gap) / 2;
        painter.setBrush(active ? colors.accent : colors.dimText);
        for (int d = 0; d < dots; ++d, x += dot + gap)
            painter.drawEllipse(QRect(x, button.bottom() - dot - 1, dot, dot));
    }
}

bool Taskbar::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const int index = indexAt(help->pos());
        if (index < 0) {
            QToolTip::hideText();
            return true;
        }
        const Group& group = m_groups[size_t(index)];
        QString text = group.app ? group.app->name : QString();
        if (group.windows.size() == 1) {
            if (const Window* window = m_windows.find(group.windows.front()); window && !window->title.isEmpty())
                text = window->title;
        }
        if (text.isEmpty())
            text = group.key;
        // Titles are the programs' to choose; they show as they are, not as markup.
        QToolTip::showText(help->globalPos(), Qt::convertFromPlainText(text), this, buttonRect(index));
        return true;
    }
    return QWidget::event(event);
}

void Taskbar::mouseMoveEvent(QMouseEvent* event)
{
    const int index = indexAt(event->position().toPoint());
    if (index != m_hovered) {
        m_hovered = index;
        update();
    }
}

void Taskbar::leaveEvent(QEvent*)
{
    m_hovered = -1;
    update();
}

void Taskbar::mouseReleaseEvent(QMouseEvent* event)
{
    const int index = indexAt(event->position().toPoint());
    if (index < 0)
        return;
    const Group group = m_groups[size_t(index)];
    switch (event->button()) {
    case Qt::LeftButton: {
        const Click action = click(group, openWindows(), m_windows.recent());
        if (action.action == Click::Minimize)
            m_windows.minimize(action.window);
        else
            m_windows.activate(action.window);
        break;
    }
    case Qt::MiddleButton:
        launch(group.app);
        break;
    case Qt::RightButton:
        showMenu(group, mapToGlobal(QPoint(buttonRect(index).left(), height())));
        break;
    default:
        break;
    }
}

void Taskbar::launch(const shell::Application* app)
{
    if (app && !shell::launch(*app))
        qWarning("tde-hermes: could not start %s", qPrintable(app->id));
}

void Taskbar::showMenu(const Group& group, QPoint pos)
{
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    // The windows, to pick one.
    for (const quint64 id : group.windows) {
        const Window* window = m_windows.find(id);
        if (!window)
            continue;
        const QString title = window->title.isEmpty() ? (group.app ? group.app->name : window->appId) : window->title;
        // An ampersand in a title is itself, not the mark of a shortcut.
        menu->addAction(fontMetrics().elidedText(title, Qt::ElideRight, 320).replace(u'&', u"&&"_s), this,
            [this, id] { m_windows.activate(id); });
    }
    menu->addSeparator();

    if (group.app)
        // A copy: the applications may be read again while the menu is open.
        menu->addAction(QIcon::fromTheme(u"window-new-symbolic"_s), u"New Window"_s, this,
            [this, app = *group.app] { launch(&app); });
    menu->addSeparator();
    const std::vector<quint64> ids = group.windows;
    menu->addAction(QIcon::fromTheme(u"window-close-symbolic"_s),
        ids.size() == 1 ? u"Close Window"_s : u"Close All Windows"_s, this, [this, ids] {
            for (const quint64 id : ids)
                m_windows.close(id);
        });
    menu->popup(pos);
}

} // namespace hermes
