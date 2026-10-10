#include "KeyboardPage.hpp"

#include <Layouts.hpp>
#include <tde/Dialog.hpp>

#include <QComboBox>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QFile>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace daedalus {
namespace {

const QString Locale = u"org.freedesktop.locale1"_s;
const QString LocalePath = u"/org/freedesktop/locale1"_s;

// The ways of switching layouts offered, as XKB options.
const std::pair<const char*, const char*> SwitchKeys[] = {
    {"grp:win_space_toggle", "Super+Space"},
    {"grp:alt_shift_toggle", "Alt+Shift"},
    {"grp:ctrl_shift_toggle", "Ctrl+Shift"},
    {"grp:caps_toggle", "Caps Lock"},
    {"", "Not by keys"},
};

// The keys offered for composing characters, such as Compose, then ' and e for é.
const std::pair<const char*, const char*> ComposeKeys[] = {
    {"", "None"},
    {"compose:ralt", "Right Alt"},
    {"compose:rctrl", "Right Ctrl"},
    {"compose:rwin", "Right Super"},
    {"compose:menu", "Menu"},
    {"compose:caps", "Caps Lock"},
    {"compose:prsc", "Print Screen"},
    {"compose:sclk", "Scroll Lock"},
};

QString localeProperty(const QString& name)
{
    QDBusMessage call
        = QDBusMessage::createMethodCall(Locale, LocalePath, u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
    call << Locale << name;
    const QDBusReply<QDBusVariant> reply = QDBusConnection::systemBus().call(call);
    return reply.isValid() ? reply.value().variant().toString() : QString();
}

} // namespace

std::vector<InputSource> parseXkbList(const QString& text)
{
    // Sections start with "! name"; their lines are "  name  description", a variant's
    // description starting with its layout and a colon.
    static const QRegularExpression line(uR"(^\s+(\S+)\s+(.+)$)"_s);
    std::vector<InputSource> sources;
    QString section;
    for (const QString& row : text.split(u'\n')) {
        if (row.startsWith(u'!')) {
            section = row.mid(1).trimmed();
            continue;
        }
        const auto match = line.match(row);
        if (!match.hasMatch())
            continue;
        if (section == u"layout") {
            sources.push_back({match.captured(1), {}, match.captured(2).trimmed()});
        } else if (section == u"variant") {
            const QString rest = match.captured(2);
            const qsizetype colon = rest.indexOf(u':');
            if (colon > 0)
                sources.push_back({rest.left(colon), match.captured(1), rest.mid(colon + 1).trimmed()});
        }
    }
    std::ranges::sort(sources,
        [](const InputSource& a, const InputSource& b) { return a.description.localeAwareCompare(b.description) < 0; });
    return sources;
}

std::vector<InputSource> sourcesOf(const QString& layouts, const QString& variants)
{
    std::vector<InputSource> sources;
    const QStringList names = layouts.split(u',', Qt::SkipEmptyParts);
    const QStringList kinds = variants.split(u',');
    for (qsizetype i = 0; i < names.size(); ++i)
        sources.push_back({names[i].trimmed(), i < kinds.size() ? kinds[i].trimmed() : QString(), {}});
    return sources;
}

std::pair<QString, QString> layoutsOf(const std::vector<InputSource>& sources)
{
    QStringList layouts;
    QStringList variants;
    for (const InputSource& source : sources) {
        layouts << source.layout;
        variants << source.variant;
    }
    // Without any variant, there is no list of empty ones.
    const bool anyVariant = std::ranges::any_of(sources, [](const InputSource& s) { return !s.variant.isEmpty(); });
    return {layouts.join(u','), anyVariant ? variants.join(u',') : QString()};
}

QString optionOf(const QString& options, const QString& group)
{
    const QString prefix = group + u':';
    for (const QString& option : options.split(u',', Qt::SkipEmptyParts)) {
        if (option.trimmed().startsWith(prefix))
            return option.trimmed();
    }
    return {};
}

QString withOption(const QString& options, const QString& group, const QString& option)
{
    const QString prefix = group + u':';
    QStringList kept;
    for (const QString& other : options.split(u',', Qt::SkipEmptyParts)) {
        if (!other.trimmed().startsWith(prefix))
            kept << other.trimmed();
    }
    if (!option.isEmpty())
        kept << option;
    return kept.join(u',');
}

KeyboardPage::KeyboardPage(QWidget* parent)
    : Page(parent)
{
    QFile list(u"/usr/share/X11/xkb/rules/evdev.lst"_s);
    if (list.open(QIODevice::ReadOnly | QIODevice::Text))
        m_known = parseXkbList(QString::fromUtf8(list.readAll()));

    m_content = new QWidget(this);
    auto* layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(24);
    addWidget(m_content);
    load();
}

void KeyboardPage::load()
{
    m_sources = sourcesOf(localeProperty(u"X11Layout"_s), localeProperty(u"X11Variant"_s));
    m_model = localeProperty(u"X11Model"_s);
    m_options = localeProperty(u"X11Options"_s);
    if (m_sources.empty())
        m_sources.push_back({u"us"_s, {}, {}});
    for (InputSource& source : m_sources) {
        const auto known = std::ranges::find_if(
            m_known, [&](const InputSource& k) { return k.layout == source.layout && k.variant == source.variant; });
        source.description = known != m_known.end() ? known->description
            : source.variant.isEmpty()              ? source.layout
                                                    : u"%1 (%2)"_s.arg(source.layout, source.variant);
    }
    sync();
}

void KeyboardPage::sync()
{
    QLayout* layout = m_content->layout();
    shell::clearLayout(*layout);

    auto* sources = new Group(u"Input Sources"_s, m_content);
    auto* add = new QPushButton(u"Add…"_s, sources);
    connect(add, &QPushButton::clicked, this, &KeyboardPage::addSource);
    sources->setHeaderWidget(add);
    for (size_t i = 0; i < m_sources.size(); ++i) {
        auto* controls = new QWidget(sources);
        auto* row = new QHBoxLayout(controls);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        if (i > 0) {
            auto* up = new QPushButton(u"Move Up"_s, controls);
            connect(up, &QPushButton::clicked, this, [this, i] {
                auto sources = m_sources;
                std::swap(sources[i - 1], sources[i]);
                save(sources, m_options);
            });
            row->addWidget(up);
        }
        if (m_sources.size() > 1) {
            auto* remove = new QPushButton(u"Remove"_s, controls);
            connect(remove, &QPushButton::clicked, this, [this, i] {
                auto sources = m_sources;
                sources.erase(sources.begin() + qsizetype(i));
                save(sources, m_options);
            });
            row->addWidget(remove);
        }
        sources->addRow(
            m_sources[i].description, i == 0 && m_sources.size() > 1 ? u"Used first"_s : QString(), controls);
    }
    sources->addNote(u"The layouts are the computer's: the login screen and the text console have them too."_s);
    // Set for the session, it is what the compositor uses.
    if (const QString forced = qEnvironmentVariable("XKB_DEFAULT_LAYOUT"); !forced.isEmpty()) {
        sources->addNote(u"XKB_DEFAULT_LAYOUT is set to “%1” for this session, and used in place of these until it "
                         "is unset."_s.arg(forced));
    }
    layout->addWidget(sources);

    auto* switching = new Group(u"Switching"_s, m_content);
    auto* keys = new QComboBox(switching);
    const QString current = switchOption(m_options);
    for (const auto& [option, label] : SwitchKeys)
        keys->addItem(QString::fromUtf8(label), QString::fromLatin1(option));
    if (keys->findData(current) < 0)
        keys->insertItem(0, current, current);
    keys->setCurrentIndex(keys->findData(current));
    keys->setEnabled(m_sources.size() > 1);
    connect(keys, &QComboBox::activated, this,
        [this, keys] { save(m_sources, withSwitchOption(m_options, keys->currentData().toString())); });
    switching->addRow(u"Switch layouts with"_s, u"Moves on to the next input source"_s, keys);
    layout->addWidget(switching);

    auto* special = new Group(u"Special Keys"_s, m_content);
    auto* compose = new QComboBox(special);
    const QString composeNow = optionOf(m_options, u"compose"_s);
    for (const auto& [option, label] : ComposeKeys)
        compose->addItem(QString::fromUtf8(label), QString::fromLatin1(option));
    if (compose->findData(composeNow) < 0)
        compose->insertItem(1, composeNow, composeNow);
    compose->setCurrentIndex(compose->findData(composeNow));
    connect(compose, &QComboBox::activated, this,
        [this, compose] { save(m_sources, withOption(m_options, u"compose"_s, compose->currentData().toString())); });
    special->addRow(
        u"Compose key"_s, u"Followed by others, types what is not on the keyboard: ' then e for é"_s, compose);
    special->addNote(u"Right Alt is the AltGr key of many layouts, which then types only what Compose does."_s);
    layout->addWidget(special);
}

void KeyboardPage::save(const std::vector<InputSource>& sources, const QString& options)
{
    const auto [layouts, variants] = layoutsOf(sources);
    QDBusMessage call = QDBusMessage::createMethodCall(Locale, LocalePath, Locale, u"SetX11Keyboard"_s);
    // Not converted to the console's keymap, and asking for a password when the system wants one.
    call << layouts << m_model << variants << options << false << true;
    call.setInteractiveAuthorizationAllowed(true);
    // Long enough for the password to be typed.
    const QDBusMessage reply = QDBusConnection::systemBus().call(call, QDBus::Block, 120000);
    if (reply.type() == QDBusMessage::ErrorMessage)
        qWarning("tde-daedalus: the keyboard layout was not changed: %s", qPrintable(reply.errorMessage()));
    load();
}

void KeyboardPage::addSource()
{
    tde::Dialog dialog(u"Add an Input Source"_s, window());
    dialog.resize(420, 480);
    auto* search = new QLineEdit(&dialog);
    search->setPlaceholderText(u"Search languages and layouts"_s);
    auto* choices = new QListWidget(&dialog);
    for (const InputSource& source : m_known) {
        if (std::ranges::contains(m_sources, std::pair(source.layout, source.variant),
                [](const InputSource& s) { return std::pair(s.layout, s.variant); }))
            continue;
        auto* item = new QListWidgetItem(source.description, choices);
        item->setData(Qt::UserRole, source.layout);
        item->setData(Qt::UserRole + 1, source.variant);
    }
    connect(search, &QLineEdit::textChanged, choices, [choices](const QString& text) {
        for (int i = 0; i < choices->count(); ++i)
            choices->item(i)->setHidden(!choices->item(i)->text().contains(text, Qt::CaseInsensitive));
    });
    auto* buttons = new QHBoxLayout;
    auto* cancel = new QPushButton(u"Cancel"_s, &dialog);
    auto* accept = new QPushButton(u"Add"_s, &dialog);
    accept->setEnabled(false);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(accept);
    dialog.contentLayout()->addWidget(search);
    dialog.contentLayout()->addWidget(choices, 1);
    dialog.contentLayout()->addLayout(buttons);
    dialog.setDefaultButton(accept);
    connect(choices, &QListWidget::currentItemChanged, accept,
        [accept](QListWidgetItem* item) { accept->setEnabled(item != nullptr); });
    connect(choices, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(accept, &QPushButton::clicked, &dialog, &QDialog::accept);
    search->setFocus();
    if (dialog.run() != QDialog::Accepted || !choices->currentItem())
        return;

    auto sources = m_sources;
    const QListWidgetItem* chosen = choices->currentItem();
    sources.push_back(
        {chosen->data(Qt::UserRole).toString(), chosen->data(Qt::UserRole + 1).toString(), chosen->text()});
    // A second layout without a way to switch to it gets the usual one.
    QString options = m_options;
    if (m_sources.size() == 1 && switchOption(options).isEmpty())
        options = withSwitchOption(options, u"grp:win_space_toggle"_s);
    save(sources, options);
}

} // namespace daedalus
