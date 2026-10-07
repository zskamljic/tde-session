#include "DisplaysPage.hpp"

#include <tde/Dialog.hpp>
#include <tde/Theme.hpp>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numeric>

using namespace Qt::StringLiterals;

namespace daedalus {
namespace {

constexpr int Margin = 16; // around the displays in the arrangement
constexpr int KeepFor = 15; // seconds to say whether changes stay
constexpr int Scales[] = {100, 125, 150, 175, 200, 250, 300}; // percent

const std::pair<int, const char*> orientations[] = {
    {0, "Landscape"},
    {1, "Portrait Left"},
    {3, "Portrait Right"},
    {2, "Landscape (Flipped)"},
};

// The fastest a display goes at `size`.
int fastestRefresh(const shell::Display& display, QSize size)
{
    int fastest = 0;
    for (const auto& mode : display.modes) {
        if (mode.size == size)
            fastest = std::max(fastest, mode.refresh);
    }
    return fastest;
}

QString resolutionName(QSize size)
{
    const int divisor = std::gcd(size.width(), size.height());
    QString ratio = divisor > 0 ? u"%1:%2"_s.arg(size.width() / divisor).arg(size.height() / divisor) : QString();
    // As people know them, not as the numbers come out.
    if (ratio == u"8:5"_s)
        ratio = u"16:10"_s;
    if (ratio.length() > 5)
        ratio.clear();
    return ratio.isEmpty() ? u"%1 × %2"_s.arg(size.width()).arg(size.height())
                           : u"%1 × %2 (%3)"_s.arg(size.width()).arg(size.height()).arg(ratio);
}

} // namespace

// Arrangement -----------------------------------------------------------------------------

Arrangement::Arrangement(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void Arrangement::setItems(std::vector<Item> items, const QString& selected)
{
    m_items = std::move(items);
    m_selected = selected;
    if (!m_grab) {
        m_bounds = {};
        for (const Item& item : m_items)
            m_bounds = m_bounds.united(item.area);
    }
    update();
}

double Arrangement::scale() const
{
    if (m_bounds.isEmpty())
        return 1;
    return std::min((width() - 2.0 * Margin) / m_bounds.width(), (height() - 2.0 * Margin) / m_bounds.height());
}

QPointF Arrangement::origin() const
{
    const double s = scale();
    return QPointF((width() - m_bounds.width() * s) / 2 - m_bounds.x() * s,
        (height() - m_bounds.height() * s) / 2 - m_bounds.y() * s);
}

QRectF Arrangement::onWidget(const QRect& area) const
{
    const double s = scale();
    return QRectF(origin() + QPointF(area.topLeft()) * s, QSizeF(area.size()) * s);
}

QPoint Arrangement::snap(const QRect& area, const std::vector<QRect>& others)
{
    if (others.empty())
        return area.topLeft();
    const auto overlaps = [&](const QRect& candidate) {
        return std::ranges::any_of(others, [&](const QRect& other) { return candidate.intersects(other); });
    };
    QPoint best = area.topLeft();
    double bestDistance = std::numeric_limits<double>::max();
    // Edges lined up are what people mostly want, so those pull from a little further away.
    const auto consider = [&](QPoint position, double pull = 0) {
        const QRect candidate(position, area.size());
        if (overlaps(candidate))
            return;
        const QPoint delta = position - area.topLeft();
        const double distance = std::hypot(delta.x(), delta.y()) - pull;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = position;
        }
    };
    for (const QRect& other : others) {
        // Beside it, sliding along its edge as far as they still touch.
        const int y = std::clamp(area.y(), other.top() - area.height() + 1, other.bottom());
        const int x = std::clamp(area.x(), other.left() - area.width() + 1, other.right());
        consider({other.left() - area.width(), y});
        consider({other.right() + 1, y});
        consider({x, other.top() - area.height()});
        consider({x, other.bottom() + 1});
        // And lined up with its edges.
        const double pull = std::max(other.width(), other.height()) * 0.025;
        consider({other.left() - area.width(), other.top()}, pull);
        consider({other.right() + 1, other.top()}, pull);
        consider({other.left(), other.top() - area.height()}, pull);
        consider({other.left(), other.bottom() + 1}, pull);
        consider({other.left() - area.width(), other.bottom() + 1 - area.height()}, pull);
        consider({other.right() + 1, other.bottom() + 1 - area.height()}, pull);
        consider({other.right() + 1 - area.width(), other.top() - area.height()}, pull);
        consider({other.right() + 1 - area.width(), other.bottom() + 1}, pull);
    }
    return best;
}

void Arrangement::paintEvent(QPaintEvent*)
{
    const auto& colors = tde::theme::colors();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    for (const Item& item : m_items) {
        const QRectF box = onWidget(item.area).adjusted(2, 2, -2, -2);
        const bool selected = item.name == m_selected;
        painter.setPen(QPen(selected ? colors.accent : colors.border, selected ? 2.5 : 1));
        painter.setBrush(colors.base);
        painter.drawRoundedRect(box, 6, 6);
        // The bar along the top of the primary one.
        if (item.primary) {
            QPainterPath shape;
            shape.addRoundedRect(box.adjusted(1, 1, -1, -1), 5, 5);
            painter.save();
            painter.setClipPath(shape);
            painter.fillRect(
                QRectF(box.left(), box.top(), box.width(), std::max(4.0, box.height() * 0.08)), colors.header);
            painter.restore();
        }
        painter.setPen(colors.text);
        QFont number = font();
        number.setPointSizeF(number.pointSizeF() * 1.6);
        number.setBold(true);
        painter.setFont(number);
        painter.drawText(box.adjusted(0, 0, 0, -box.height() / 4), Qt::AlignCenter, item.label);
        painter.setFont(font());
        painter.setPen(colors.dimText);
        painter.drawText(box.adjusted(6, box.height() / 2, -6, -4), Qt::AlignHCenter | Qt::AlignTop,
            painter.fontMetrics().elidedText(item.name, Qt::ElideRight, int(box.width()) - 12));
    }
}

void Arrangement::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    for (const Item& item : m_items) {
        if (!onWidget(item.area).contains(event->position()))
            continue;
        m_selected = item.name;
        emit selected(item.name);
        // One display alone has nowhere to go.
        if (m_items.size() > 1)
            m_grab = (event->position() - origin()) / scale() - QPointF(item.area.topLeft());
        update();
        return;
    }
}

void Arrangement::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_grab)
        return;
    const auto it = std::ranges::find(m_items, m_selected, &Item::name);
    if (it == m_items.end())
        return;
    it->area.moveTopLeft(((event->position() - origin()) / scale() - *m_grab).toPoint());
    update();
}

void Arrangement::mouseReleaseEvent(QMouseEvent*)
{
    if (!m_grab)
        return;
    m_grab.reset();
    const auto it = std::ranges::find(m_items, m_selected, &Item::name);
    if (it == m_items.end())
        return;
    std::vector<QRect> others;
    for (const Item& item : m_items) {
        if (item.name != m_selected)
            others.push_back(item.area);
    }
    emit moved(m_selected, snap(it->area, others));
}

// DisplaysPage ----------------------------------------------------------------------------

DisplaysPage::DisplaysPage(Settings& settings, QWidget* parent)
    : Page(parent)
    , m_settings(settings)
{
    m_unsupported = new QLabel(u"The compositor does not let displays be changed."_s, this);
    m_unsupported->setForegroundRole(QPalette::PlaceholderText);
    addWidget(m_unsupported, Qt::AlignHCenter);

    m_arrangement = new Arrangement(this);
    addWidget(m_arrangement);
    connect(m_arrangement, &Arrangement::selected, this, [this](const QString& name) {
        m_selected = name;
        showSelected();
    });
    connect(m_arrangement, &Arrangement::moved, this, [this](const QString& name, QPoint position) {
        if (shell::DisplaySetting* moved = setting(name))
            moved->position = position;
        edited();
    });

    auto* buttons = new QWidget(this);
    auto* row = new QHBoxLayout(buttons);
    row->setContentsMargins(0, 0, 0, 0);
    row->addStretch(1);
    m_reset = new QPushButton(u"Reset"_s, this);
    connect(m_reset, &QPushButton::clicked, this, &DisplaysPage::load);
    m_apply = new QPushButton(u"Apply"_s, this);
    m_apply->setDefault(true);
    connect(m_apply, &QPushButton::clicked, this, &DisplaysPage::apply);
    row->addWidget(m_reset);
    row->addWidget(m_apply);
    // Right below the displays, in sight while moving them.
    addWidget(buttons);

    m_group = addGroup();
    m_primary = new Switch(this);
    connect(m_primary, &Switch::toggled, this, [this](bool on) {
        // One is always primary: another is picked by turning it on there.
        const shell::Display* shown = display(m_selected);
        if (!on || !shown) {
            m_primary->setChecked(true);
            return;
        }
        m_settings.config.displays.primary = shown->identity();
        m_settings.save();
        edited();
    });
    m_group->addRow(u"Primary Display"_s, u"Where the bar and the notifications are"_s, m_primary);
    m_enabled = new Switch(this);
    connect(m_enabled, &Switch::toggled, this, [this](bool on) {
        shell::DisplaySetting* changed = setting(m_selected);
        const bool last = std::ranges::count_if(m_edit, &shell::DisplaySetting::enabled) == 1;
        if (!changed || (!on && last && changed->enabled)) {
            m_enabled->setChecked(true);
            return;
        }
        changed->enabled = on;
        // Off, it had no mode: it comes back with the first it offers, as fast as that goes.
        const shell::Display* shown = display(m_selected);
        if (on && !changed->size.isValid() && shown && !shown->modes.empty()) {
            changed->size = shown->modes.front().size;
            changed->refresh = fastestRefresh(*shown, changed->size);
        }
        if (on)
            settle(m_selected);
        edited();
    });
    m_group->addRow(u"On"_s, u"Whether anything shows on this display"_s, m_enabled);

    m_resolution = new QComboBox(this);
    connect(m_resolution, &QComboBox::activated, this, [this] {
        shell::DisplaySetting* changed = setting(m_selected);
        const shell::Display* shown = display(m_selected);
        if (!changed || !shown)
            return;
        changed->size = m_resolution->currentData().toSize();
        changed->refresh = fastestRefresh(*shown, changed->size);
        settle(m_selected);
        edited();
    });
    m_group->addRow(u"Resolution"_s, {}, m_resolution);
    m_refresh = new QComboBox(this);
    connect(m_refresh, &QComboBox::activated, this, [this] {
        if (shell::DisplaySetting* changed = setting(m_selected))
            changed->refresh = m_refresh->currentData().toInt();
        edited();
    });
    m_group->addRow(u"Refresh Rate"_s, {}, m_refresh);
    m_scale = new QComboBox(this);
    connect(m_scale, &QComboBox::activated, this, [this] {
        if (shell::DisplaySetting* changed = setting(m_selected))
            changed->scale = m_scale->currentData().toInt();
        settle(m_selected);
        edited();
    });
    m_group->addRow(u"Scale"_s, u"How large everything is drawn"_s, m_scale);
    m_orientation = new QComboBox(this);
    for (const auto& [transform, name] : orientations)
        m_orientation->addItem(QString::fromUtf8(name), transform);
    connect(m_orientation, &QComboBox::activated, this, [this] {
        if (shell::DisplaySetting* changed = setting(m_selected))
            changed->transform = m_orientation->currentData().toInt();
        settle(m_selected);
        edited();
    });
    m_group->addRow(u"Orientation"_s, {}, m_orientation);

    connect(&m_displays, &shell::Displays::changed, this, [this] {
        // While changes are tried out, they are what the displays show.
        if (!m_applying)
            load();
    });
    load();
}

shell::DisplaySetting* DisplaysPage::setting(const QString& name)
{
    const auto it = std::ranges::find(m_edit, name, &shell::DisplaySetting::name);
    return it == m_edit.end() ? nullptr : &*it;
}

const shell::Display* DisplaysPage::display(const QString& name) const
{
    const auto& displays = m_displays.displays();
    const auto it = std::ranges::find(displays, name, &shell::Display::name);
    return it == displays.end() ? nullptr : &*it;
}

void DisplaysPage::load()
{
    // The displays come a moment after the page; until then there is nothing to show.
    const bool shown = !m_displays.displays().empty();
    m_unsupported->setVisible(!m_displays.isSupported());
    m_arrangement->setVisible(shown);
    m_group->setVisible(shown);
    m_edit = m_displays.settings();
    if (!setting(m_selected))
        m_selected = shell::primaryDisplay(m_settings.config.displays, m_displays.displays());
    edited();
}

// A display that grew or came back is moved to touch the others without covering them.
void DisplaysPage::settle(const QString& name)
{
    shell::DisplaySetting* moved = setting(name);
    if (!moved || !moved->enabled || moved->size.isEmpty())
        return;
    std::vector<QRect> others;
    for (const auto& other : m_edit) {
        if (other.enabled && other.name != name && !other.size.isEmpty())
            others.push_back(other.area());
    }
    const QRect area = moved->area();
    if (std::ranges::any_of(others, [&](const QRect& other) { return other.intersects(area); })
        || std::ranges::none_of(
            others, [&](const QRect& other) { return other.adjusted(-1, -1, 1, 1).intersects(area); }))
        moved->position = Arrangement::snap(area, others);
}

void DisplaysPage::edited()
{
    // The layout starts at the top left, as compositors expect.
    QRect bounds;
    for (const auto& edit : m_edit) {
        if (edit.enabled && !edit.size.isEmpty())
            bounds = bounds.united(edit.area());
    }
    for (auto& edit : m_edit) {
        if (edit.enabled)
            edit.position -= bounds.topLeft();
    }

    const QString primary = shell::primaryDisplay(m_settings.config.displays, m_displays.displays());
    std::vector<Arrangement::Item> items;
    for (const auto& edit : m_edit) {
        if (!edit.enabled || edit.size.isEmpty())
            continue;
        items.push_back({.name = edit.name, .area = edit.area(), .primary = edit.name == primary});
    }
    std::ranges::sort(items, [](const auto& a, const auto& b) { return a.area.x() < b.area.x(); });
    for (size_t i = 0; i < items.size(); ++i)
        items[i].label = QString::number(i + 1);
    m_arrangement->setItems(std::move(items), m_selected);

    const bool changed = !shell::sameSettings(m_edit, m_displays.settings());
    m_apply->setEnabled(changed);
    m_reset->setEnabled(changed);
    showSelected();
}

void DisplaysPage::showSelected()
{
    const shell::Display* shown = display(m_selected);
    const shell::DisplaySetting* edit = setting(m_selected);
    if (!shown || !edit)
        return;
    m_group->setTitle(shown->title() + u" (" + shown->name + u')');
    const QSignalBlocker blockPrimary(m_primary);
    const QSignalBlocker blockEnabled(m_enabled);
    m_primary->setChecked(shell::primaryDisplay(m_settings.config.displays, m_displays.displays()) == shown->name);
    m_primary->setEnabled(edit->enabled);
    m_enabled->setChecked(edit->enabled);

    // Sizes from the largest, once each; rates of the one picked, from the fastest.
    std::vector<QSize> sizes;
    for (const auto& mode : shown->modes) {
        if (!std::ranges::contains(sizes, mode.size))
            sizes.push_back(mode.size);
    }
    std::ranges::stable_sort(sizes, [](QSize a, QSize b) { return a.width() * a.height() > b.width() * b.height(); });
    m_resolution->clear();
    for (const QSize size : sizes)
        m_resolution->addItem(resolutionName(size), size);
    m_resolution->setCurrentIndex(m_resolution->findData(edit->size));

    std::vector<int> rates;
    for (const auto& mode : shown->modes) {
        if (mode.size == edit->size && !std::ranges::contains(rates, mode.refresh))
            rates.push_back(mode.refresh);
    }
    std::ranges::sort(rates, std::greater());
    m_refresh->clear();
    for (const int rate : rates)
        m_refresh->addItem(rate > 0 ? u"%1 Hz"_s.arg(rate / 1000.0, 0, 'f', 2) : u"Not known"_s, rate);
    m_refresh->setCurrentIndex(std::max(0, m_refresh->findData(edit->refresh)));

    // Scales that leave room for windows, and the one in use whatever it is.
    std::vector<int> scales;
    for (const int scale : Scales) {
        shell::DisplaySetting scaled = *edit;
        scaled.scale = scale;
        if (scaled.logicalSize().width() >= 800 || scale == edit->scale)
            scales.push_back(scale);
    }
    if (!std::ranges::contains(scales, edit->scale))
        scales.push_back(edit->scale);
    m_scale->clear();
    for (const int scale : scales)
        m_scale->addItem(u"%1 %"_s.arg(scale), scale);
    m_scale->setCurrentIndex(m_scale->findData(edit->scale));
    m_orientation->setCurrentIndex(std::max(0, m_orientation->findData(edit->transform)));

    for (QWidget* control : std::initializer_list<QWidget*> {m_resolution, m_refresh, m_scale, m_orientation})
        control->setEnabled(edit->enabled);
}

void DisplaysPage::apply()
{
    std::vector<shell::DisplaySetting> previous = m_displays.settings();
    m_applying = true;
    m_displays.apply(m_edit, [this, previous](bool succeeded) {
        if (!succeeded) {
            m_applying = false;
            tde::Dialog::confirm(this, u"Displays"_s, u"The displays cannot be set that way."_s,
                u"The compositor or the displays refused it; nothing changed."_s, u"OK"_s, false);
            load();
            return;
        }
        confirm(previous);
    });
}

// Asks whether the displays stay as they are now, going back to `previous` unless the user
// says so in time: with a display that shows nothing, nobody can.
void DisplaysPage::confirm(std::vector<shell::DisplaySetting> previous)
{
    tde::Dialog dialog(u"Keep Changes?"_s, this);
    auto* text = new QLabel(u"Keep these display settings?"_s, &dialog);
    QFont bold = text->font();
    bold.setBold(true);
    text->setFont(bold);
    auto* countdown = new QLabel(&dialog);
    countdown->setForegroundRole(QPalette::PlaceholderText);
    auto buttons = std::make_unique<QHBoxLayout>();
    buttons->addStretch(1);
    auto* revert = new QPushButton(u"Revert Settings"_s, &dialog);
    auto* keep = new QPushButton(u"Keep Changes"_s, &dialog);
    buttons->addWidget(revert);
    buttons->addWidget(keep);
    dialog.contentLayout()->addWidget(text);
    dialog.contentLayout()->addWidget(countdown);
    dialog.contentLayout()->addSpacing(12);
    dialog.contentLayout()->addLayout(buttons.release());
    dialog.setDefaultButton(keep);
    connect(revert, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(keep, &QPushButton::clicked, &dialog, &QDialog::accept);

    int left = KeepFor;
    const auto tick = [&] { countdown->setText(u"They go back to how they were in %1 seconds."_s.arg(left)); };
    tick();
    QTimer timer;
    timer.setInterval(1000);
    connect(&timer, &QTimer::timeout, &dialog, [&] {
        if (--left <= 0)
            dialog.reject();
        tick();
    });
    timer.start();
    const bool kept = dialog.run() == QDialog::Accepted;
    timer.stop();

    if (kept) {
        m_settings.config.displays.keep(shell::layoutOf(m_edit, m_displays.displays()));
        m_settings.save();
        m_applying = false;
        load();
        return;
    }
    m_displays.apply(previous, [this](bool) {
        m_applying = false;
        load();
    });
}

} // namespace daedalus
