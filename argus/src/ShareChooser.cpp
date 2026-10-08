#include "ShareChooser.hpp"

#include <Layer.hpp>

#include <tde/Theme.hpp>

#include <QAbstractButton>
#include <QButtonGroup>
#include <QDBusConnection>
#include <QGridLayout>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QStyleHints>
#include <QTimer>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace argus {
namespace {

const QSize TileSize(220, 140); // the picture's room

// A screen or a window to pick: its picture, rounded, with its name below; ringed when picked.
class Tile : public QAbstractButton {
public:
    Tile(const QImage& picture, const QString& name, QWidget* parent)
        : QAbstractButton(parent)
        , m_picture(picture)
    {
        setText(name);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(TileSize.width() + 16, TileSize.height() + 44);
        setToolTip(name);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& colors = tde::theme::colors();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF room(8, 8, TileSize.width(), TileSize.height());
        if (isChecked() || underMouse()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(isChecked() ? colors.accent : colors.hover);
            painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 12, 12);
        }
        if (!m_picture.isNull()) {
            QSizeF size = m_picture.size();
            size.scale(room.size(), Qt::KeepAspectRatio);
            QRectF place(QPointF(), size);
            place.moveCenter(room.center());
            QPainterPath shape;
            shape.addRoundedRect(place, 6, 6);
            painter.setClipPath(shape);
            painter.drawImage(place, m_picture);
            painter.setClipping(false);
        } else {
            painter.setPen(QPen(colors.border, 1));
            painter.setBrush(colors.base);
            painter.drawRoundedRect(room, 6, 6);
        }
        painter.setPen(isChecked() ? colors.accentText : colors.text);
        const QRect line(8, int(room.bottom()) + 8, TileSize.width(), height() - int(room.bottom()) - 12);
        painter.drawText(line, Qt::AlignHCenter | Qt::AlignVCenter,
            fontMetrics().elidedText(text(), Qt::ElideRight, TileSize.width()));
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    QImage m_picture;
};

QLabel* heading(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

} // namespace

ShareChooser::ShareChooser(Toplevels& toplevels, QWidget* parent)
    : QWidget(parent)
    , m_toplevels(toplevels)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    shell::coverScreen(*this, LayerShellQt::Window::LayerOverlay, true, u"tde-argus-sharing"_s);
}

ShareChooser::~ShareChooser()
{
    if (m_asking)
        answer({});
}

QString ShareChooser::Choose()
{
    // One question at a time; another is declined.
    if (m_asking)
        return {};
    m_asking = true;
    setDelayedReply(true);
    m_call = message();

    // What every screen and window shows now, for the pictures.
    m_pictures.clear();
    m_copies.clear();
    for (QScreen* screen : QGuiApplication::screens()) {
        auto copy = m_capture.capture(screen, [this, screen](QImage image) {
            m_pictures[screen] = std::move(image);
            m_copies.erase(screen);
            if (m_copies.empty() && !m_waitingForWindows)
                appear();
        });
        if (copy)
            m_copies[screen] = std::move(copy);
    }
    m_waitingForWindows = true;
    m_toplevels.refresh(this, [this] {
        m_waitingForWindows = false;
        if (m_copies.empty())
            appear();
    });
    return {};
}

QAbstractButton* ShareChooser::addTile(
    QWidget* parent, const QImage& picture, const QString& name, const QString& choice)
{
    auto* tile = new Tile(picture, name, parent);
    tile->setProperty("choice", choice);
    m_tiles->addButton(tile);
    connect(tile, &QAbstractButton::toggled, m_share, [this] { m_share->setEnabled(m_tiles->checkedButton()); });
    // A double click shares at once.
    connect(tile, &QAbstractButton::clicked, this, [this, tile] {
        if (tile->property("clicked").toBool())
            answer(tile->property("choice").toString());
        tile->setProperty("clicked", true);
        QTimer::singleShot(QGuiApplication::styleHints()->mouseDoubleClickInterval(), tile,
            [tile] { tile->setProperty("clicked", false); });
    });
    return tile;
}

void ShareChooser::appear()
{
    delete m_panel;
    m_panel = new QWidget(this);
    m_panel->setObjectName(u"SharePanel"_s);
    m_panel->setAttribute(Qt::WA_StyledBackground);
    const auto& colors = tde::theme::colors();
    m_panel->setStyleSheet(u"#SharePanel { background: %1; border: 1px solid %2; border-radius: 16px; }"_s.arg(
        colors.window.name(), colors.border.name()));
    auto* layout = new QVBoxLayout(m_panel);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    auto* title = new QLabel(u"Share Your Screen"_s, m_panel);
    QFont big = title->font();
    big.setBold(true);
    big.setPointSizeF(big.pointSizeF() * 1.3);
    title->setFont(big);
    title->setAlignment(Qt::AlignHCenter);
    auto* explanation = new QLabel(u"A program asks to record the screen. Pick what it may see."_s, m_panel);
    explanation->setAlignment(Qt::AlignHCenter);
    layout->addWidget(title);
    layout->addWidget(explanation);

    m_tiles = new QButtonGroup(m_panel);
    m_tiles->setExclusive(true);
    m_share = new QPushButton(u"Share"_s, m_panel);
    m_share->setEnabled(false);

    layout->addWidget(heading(u"Entire Screen"_s, m_panel));
    auto* screens = new QWidget(m_panel);
    m_screenRow = new QHBoxLayout(screens);
    m_screenRow->setContentsMargins(0, 0, 0, 0);
    for (QScreen* screen : QGuiApplication::screens()) {
        const auto picture = m_pictures.find(screen);
        // By its model, or where it is plugged in when that is not known, as in virtual machines.
        const QString model = screen->model();
        const QString name = model.isEmpty() || model == u"Unknown" ? screen->name() : model;
        m_screenRow->addWidget(addTile(
            screens, picture != m_pictures.end() ? picture->second : QImage(), name, u"Monitor: "_s + screen->name()));
    }
    m_screenRow->addStretch(1);
    layout->addWidget(screens);

    std::vector<const Toplevel*> windows;
    for (const auto& window : m_toplevels.windows()) {
        if (!window->identifier().isEmpty() && !window->minimized)
            windows.push_back(window.get());
    }
    if (!windows.empty()) {
        layout->addWidget(heading(u"Window"_s, m_panel));
        auto* scroll = new QScrollArea(m_panel);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* grid = new QWidget(scroll);
        m_windowGrid = new QGridLayout(grid);
        m_windowGrid->setContentsMargins(0, 0, 0, 0);
        m_windowGrid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        constexpr int Columns = 4;
        for (size_t i = 0; i < windows.size(); ++i) {
            const Toplevel* window = windows[i];
            m_windowGrid->addWidget(
                addTile(grid, window->preview, window->displayName(), u"Window: "_s + window->identifier()),
                int(i / Columns), int(i % Columns));
        }
        scroll->setWidget(grid);
        const int rows = int((windows.size() + Columns - 1) / Columns);
        scroll->setMinimumHeight(std::min(rows, 2) * (TileSize.height() + 50));
        scroll->setMinimumWidth(Columns * (TileSize.width() + 22));
        layout->addWidget(scroll, 1);
    }

    auto* buttons = new QHBoxLayout;
    auto* cancel = new QPushButton(u"Cancel"_s, m_panel);
    connect(cancel, &QPushButton::clicked, this, [this] { answer({}); });
    connect(m_share, &QPushButton::clicked, this, [this] {
        if (QAbstractButton* picked = m_tiles->checkedButton())
            answer(picked->property("choice").toString());
    });
    m_share->setDefault(true);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(m_share);
    layout->addLayout(buttons);

    QScreen* screen = m_primary ? m_primary.data() : QGuiApplication::primaryScreen();
    shell::placeOnScreen(*this, screen);
    show();
    placePanel();
    m_panel->show();
    activateWindow();
    setFocus();
}

// In the middle, as large as it wants up to most of the screen; again once the layer has its size.
void ShareChooser::placePanel()
{
    if (!m_panel)
        return;
    const QSize room = size().isEmpty() ? QSize(1200, 800) : size() * 0.9;
    const QSize panel = m_panel->sizeHint().boundedTo(room);
    m_panel->setGeometry((width() - panel.width()) / 2, (height() - panel.height()) / 2, panel.width(), panel.height());
}

void ShareChooser::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    placePanel();
}

void ShareChooser::answer(const QString& choice)
{
    if (!m_asking)
        return;
    m_asking = false;
    QDBusConnection::sessionBus().send(m_call.createReply(choice));
    hide();
    if (m_panel) {
        m_panel->deleteLater();
        m_panel = nullptr;
    }
    m_pictures.clear();
}

void ShareChooser::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        answer({});
    } else if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && m_tiles
        && m_tiles->checkedButton()) {
        answer(m_tiles->checkedButton()->property("choice").toString());
    } else {
        QWidget::keyPressEvent(event);
    }
}

void ShareChooser::paintEvent(QPaintEvent*)
{
    // The screen dimmed behind the question.
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 140));
}

} // namespace argus
