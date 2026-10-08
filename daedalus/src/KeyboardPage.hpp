#pragma once

#include "Widgets.hpp"

#include <QString>
#include <QStringList>

#include <vector>

namespace daedalus {

// A keyboard layout, or a variant of one, as XKB lists them.
struct InputSource {
    QString layout; // "us", "si"
    QString variant; // "dvorak"; empty for the layout itself
    QString description; // "English (Dvorak)"

    bool operator==(const InputSource&) const = default;
};

// The layouts and their variants in an XKB rules list, such as evdev.lst, sorted by description.
std::vector<InputSource> parseXkbList(const QString& text);

// The layouts in use as localectl keeps them, comma-separated in step with their variants.
std::vector<InputSource> sourcesOf(const QString& layouts, const QString& variants);
// The other way: layouts and variants, comma-separated, for localectl.
std::pair<QString, QString> layoutsOf(const std::vector<InputSource>& sources);

// The XKB option that switches between layouts ("grp:win_space_toggle"), or an empty one.
QString switchOption(const QString& options);
// `options` with `option` in place of the one switching layouts.
QString withSwitchOption(const QString& options, const QString& option);

// The keyboard layouts, switching between them, all for the computer: the layouts are kept by
// systemd-localed, as `localectl set-x11-keymap` keeps them, so the login screen has them too.
class KeyboardPage : public Page {
public:
    explicit KeyboardPage(QWidget* parent = nullptr);

private:
    void load();
    void sync();
    // Hands the layouts and options to localed; shows what it has after.
    void save(const std::vector<InputSource>& sources, const QString& options);
    void addSource();

    std::vector<InputSource> m_known; // every layout there is
    std::vector<InputSource> m_sources; // those in use, the first the default
    QString m_model;
    QString m_options;
    QWidget* m_content = nullptr;
};

} // namespace daedalus
