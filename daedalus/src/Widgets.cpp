#include "Widgets.hpp"

#include <tde/Theme.hpp>

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

#include <memory>

using namespace Qt::StringLiterals;

namespace daedalus {
namespace {

constexpr int ColumnWidth = 640;

QFont scaled(QFont font, double factor, bool bold)
{
    font.setPointSizeF(font.pointSizeF() * factor);
    font.setBold(bold);
    return font;
}

} // namespace

// Switch ----------------------------------------------------------------------------------

Switch::Switch(QWidget* parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(sizeHint());
    connect(&m_knob, &QVariantAnimation::valueChanged, this, qOverload<>(&QWidget::update));
}

void Switch::checkStateSet()
{
    // Set from outside: no sliding.
    m_knob.jump(isChecked() ? 1 : 0);
    update();
}

void Switch::nextCheckState()
{
    QAbstractButton::nextCheckState();
    m_knob.go(isChecked() ? 1 : 0, 150);
}

void Switch::paintEvent(QPaintEvent*)
{
    const auto& colors = tde::theme::colors();
    const double on = m_knob.now();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(isEnabled() ? 1 : 0.5);
    const QRectF track = QRectF(rect()).adjusted(1, 1, -1, -1);
    QColor off = colors.text;
    off.setAlphaF(0.15f);
    const QColor fill = QColor::fromRgbF(off.redF() + (colors.accent.redF() - off.redF()) * float(on),
        off.greenF() + (colors.accent.greenF() - off.greenF()) * float(on),
        off.blueF() + (colors.accent.blueF() - off.blueF()) * float(on), off.alphaF() + (1 - off.alphaF()) * float(on));
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawRoundedRect(track, track.height() / 2, track.height() / 2);
    const double knob = track.height() - 4;
    const double x = track.left() + 2 + (track.width() - knob - 4) * on;
    painter.setBrush(QColor(0xfafafa));
    painter.drawEllipse(QRectF(x, track.top() + 2, knob, knob));
}

// Row -------------------------------------------------------------------------------------

Row::Row(const QString& title, const QString& subtitle, QWidget* control, QWidget* parent)
    : QWidget(parent)
    , m_control(control)
{
    setMinimumHeight(52);
    auto text = std::make_unique<QVBoxLayout>();
    text->setSpacing(2);
    auto* titleLabel = new QLabel(title, this);
    titleLabel->setWordWrap(true);
    text->addWidget(titleLabel);
    if (!subtitle.isEmpty()) {
        auto* subtitleLabel = new QLabel(subtitle, this);
        subtitleLabel->setWordWrap(true);
        subtitleLabel->setFont(scaled(font(), 0.9, false));
        subtitleLabel->setForegroundRole(QPalette::PlaceholderText);
        text->addWidget(subtitleLabel);
    }

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 8, 14, 8);
    layout->setSpacing(12);
    layout->addLayout(text.release(), 1);
    if (control) {
        control->setParent(this);
        layout->addWidget(control, 0, Qt::AlignVCenter);
    }
    if (qobject_cast<Switch*>(control))
        setCursor(Qt::PointingHandCursor);
}

void Row::mouseReleaseEvent(QMouseEvent* event)
{
    if (auto* button = qobject_cast<Switch*>(m_control); button && button->isEnabled()
        && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
        button->click();
}

// Group -----------------------------------------------------------------------------------

Group::Group(const QString& title, QWidget* parent)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(10);

    auto header = std::make_unique<QHBoxLayout>();
    m_header = header.get();
    header->setContentsMargins(2, 0, 0, 0);
    m_title = new QLabel(title, this);
    m_title->setFont(scaled(font(), 1.0, true));
    m_title->setVisible(!title.isEmpty());
    header->addWidget(m_title, 1);
    m_layout->addLayout(header.release());

    m_box = new QWidget(this);
    m_box->setObjectName(u"BoxedList"_s);
    m_box->setAttribute(Qt::WA_StyledBackground);
    m_rows = new QVBoxLayout(m_box);
    m_rows->setContentsMargins(0, 0, 0, 0);
    m_rows->setSpacing(0);
    m_box->hide();
    m_layout->addWidget(m_box);
}

Row* Group::addRow(const QString& title, const QString& subtitle, QWidget* control)
{
    if (m_rows->count() > 0) {
        auto* line = new QFrame(m_box);
        line->setObjectName(u"Separator"_s);
        line->setFixedHeight(1);
        m_rows->addWidget(line);
    }
    auto* row = new Row(title, subtitle, control, m_box);
    m_rows->addWidget(row);
    m_box->show();
    return row;
}

void Group::addNote(const QString& text)
{
    auto* note = new QLabel(text, this);
    note->setWordWrap(true);
    note->setFont(scaled(font(), 0.9, false));
    note->setForegroundRole(QPalette::PlaceholderText);
    note->setContentsMargins(2, 0, 2, 0);
    m_layout->addWidget(note);
}

void Group::setTitle(const QString& title)
{
    m_title->setText(title);
    m_title->setVisible(!title.isEmpty());
}

void Group::setHeaderWidget(QWidget* widget)
{
    widget->setParent(this);
    m_header->addWidget(widget);
    m_title->show();
}

// Page ------------------------------------------------------------------------------------

Page::Page(QWidget* parent)
    : QScrollArea(parent)
{
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget(this);
    content->setObjectName(u"PageContent"_s);
    content->setStyleSheet(u"#PageContent { background: transparent; }"_s);
    viewport()->setAutoFillBackground(false);

    auto* outer = new QHBoxLayout(content);
    outer->setContentsMargins(24, 28, 24, 28);
    auto* column = new QWidget(content);
    column->setMaximumWidth(ColumnWidth);
    m_column = new QVBoxLayout(column);
    m_column->setContentsMargins(0, 0, 0, 0);
    m_column->setSpacing(28);
    m_column->addStretch(1);
    // As wide as there is room for, up to its width, in the middle.
    outer->addStretch(1);
    outer->addWidget(column, 100);
    outer->addStretch(1);
    setWidget(content);
}

Group* Page::addGroup(const QString& title)
{
    auto* group = new Group(title, this);
    addWidget(group);
    return group;
}

void Page::addWidget(QWidget* widget, Qt::Alignment alignment)
{
    m_column->insertWidget(m_column->count() - 1, widget, 0, alignment);
}

} // namespace daedalus
