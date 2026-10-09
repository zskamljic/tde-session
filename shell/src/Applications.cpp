#include "Applications.hpp"

#include <QCollator>
#include <QDir>
#include <QDirListing>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QProcess>
#include <QRandomGenerator>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <cctype>
#include <optional>

using namespace Qt::StringLiterals;

namespace shell {
namespace {

// Undoes the escapes of desktop entry string values: \s \n \t \r and \\.
QString unescape(const QString& value)
{
    QString result;
    result.reserve(value.size());
    for (qsizetype i = 0; i < value.size(); ++i) {
        if (value[i] != u'\\' || i + 1 == value.size()) {
            result += value[i];
            continue;
        }
        switch (value[++i].unicode()) {
        case 's':
            result += u' ';
            break;
        case 'n':
            result += u'\n';
            break;
        case 't':
            result += u'\t';
            break;
        case 'r':
            result += u'\r';
            break;
        default:
            result += value[i];
        }
    }
    return result;
}

// A list value, like Keywords or OnlyShowIn: separated by semicolons, with \; for a literal one.
QStringList splitList(const QString& value)
{
    QStringList items;
    QString current;
    for (qsizetype i = 0; i < value.size(); ++i) {
        if (value[i] == u'\\' && i + 1 < value.size() && value[i + 1] == u';') {
            current += u';';
            ++i;
        } else if (value[i] == u';') {
            items << unescape(std::exchange(current, {})).trimmed();
        } else {
            current += value[i];
        }
    }
    items << unescape(current).trimmed();
    items.removeAll(QString());
    return items;
}

class Entry {
public:
    explicit Entry(QHash<QString, QString> values)
        : m_values(std::move(values))
    {
    }

    QString value(const QString& key) const { return unescape(m_values.value(key)); }
    // A list value, split and unescaped once, as \; is a semicolon and not a separator.
    QStringList list(const QString& key) const { return splitList(m_values.value(key)); }
    bool flag(const QString& key) const { return m_values.value(key) == u"true"; }

    // The value in the user's language when the entry has it: Name[sl_SI], then Name[sl].
    QString localized(const QString& key) const
    {
        const QString locale = QLocale().name();
        for (const QString& suffix : {locale, locale.section(u'_', 0, 0)}) {
            const auto it = m_values.constFind(key + u'[' + suffix + u']');
            if (it != m_values.cend())
                return unescape(*it);
        }
        return value(key);
    }

    QStringList localizedList(const QString& key) const
    {
        const QString locale = QLocale().name();
        for (const QString& suffix : {locale, locale.section(u'_', 0, 0)}) {
            const auto it = m_values.constFind(key + u'[' + suffix + u']');
            if (it != m_values.cend())
                return splitList(*it);
        }
        return splitList(m_values.value(key));
    }

private:
    QHash<QString, QString> m_values;
};

std::optional<Entry> readEntry(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;
    QHash<QString, QString> values;
    bool inEntry = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(u'[')) {
            inEntry = line == u"[Desktop Entry]";
            continue;
        }
        const qsizetype equals = line.indexOf(u'=');
        if (inEntry && equals > 0 && !line.startsWith(u'#'))
            values.insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
    }
    return Entry(std::move(values));
}

bool shownIn(const Entry& entry, const QStringList& desktops)
{
    const auto any = [&](const QStringList& names) {
        return std::ranges::any_of(
            names, [&](const QString& name) { return desktops.contains(name, Qt::CaseInsensitive); });
    };
    const QStringList only = entry.list(u"OnlyShowIn"_s);
    if (!only.isEmpty() && !any(only))
        return false;
    return !any(entry.list(u"NotShowIn"_s));
}

bool installed(const QString& tryExec)
{
    if (tryExec.isEmpty())
        return true;
    return QFileInfo(tryExec).isAbsolute() ? QFileInfo(tryExec).isExecutable()
                                           : !QStandardPaths::findExecutable(tryExec).isEmpty();
}

// Text reduced for matching: lower case, without accents, so "cafe" finds "Café".
QString folded(const QString& text)
{
    QString result;
    const QString decomposed = text.normalized(QString::NormalizationForm_KD);
    result.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() != QChar::Mark_NonSpacing)
            result += c.toCaseFolded();
    }
    return result;
}

bool wordStartsWith(const QString& text, const QString& prefix)
{
    for (qsizetype at = text.indexOf(prefix); at >= 0; at = text.indexOf(prefix, at + 1)) {
        if (at == 0 || !text[at - 1].isLetterOrNumber())
            return true;
    }
    return false;
}

// How well `app` matches one word of a query, lower is better; nothing when it does not.
std::optional<int> rank(const Application& app, const QString& word)
{
    const QString name = folded(app.name);
    if (name.startsWith(word))
        return 0;
    if (wordStartsWith(name, word))
        return 1;
    if (wordStartsWith(folded(app.genericName), word))
        return 2;
    if (std::ranges::any_of(app.keywords, [&](const QString& keyword) { return folded(keyword).startsWith(word); }))
        return 3;
    if (name.contains(word))
        return 4;
    const QStringList command = splitExec(app.exec);
    if (!command.isEmpty() && folded(QFileInfo(command.first()).fileName()).startsWith(word))
        return 5;
    if (folded(app.comment).contains(word))
        return 6;
    return std::nullopt;
}

// A systemd unit name for `id`: characters other than letters, digits, ':', '_' and '.' escaped
// as \xNN, like systemd-escape does.
QString unitName(const QString& id)
{
    QString escaped;
    for (const char c : id.toUtf8()) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == ':' || c == '_' || c == '.')
            escaped += QLatin1Char(c);
        else
            escaped += u"\\x%1"_s.arg(static_cast<unsigned char>(c), 2, 16, u'0');
    }
    return u"app-tde-%1-%2.scope"_s.arg(escaped).arg(QRandomGenerator::global()->generate(), 8, 16, u'0');
}

} // namespace

QStringList splitExec(const QString& exec)
{
    QStringList arguments;
    QString current;
    bool quoted = false;
    bool inArgument = false;
    for (qsizetype i = 0; i < exec.size(); ++i) {
        const QChar c = exec[i];
        if (quoted) {
            if (c == u'\\' && i + 1 < exec.size() && QStringView(u"\"`$\\").contains(exec[i + 1]))
                current += exec[++i];
            else if (c == u'"')
                quoted = false;
            else
                current += c;
        } else if (c.isSpace()) {
            if (inArgument)
                arguments << std::exchange(current, {});
            inArgument = false;
        } else {
            inArgument = true;
            if (c == u'"')
                quoted = true;
            else
                current += c;
        }
    }
    if (inArgument)
        arguments << current;
    return arguments;
}

QStringList commandLine(const Application& app)
{
    QStringList command;
    for (const QString& argument : splitExec(app.exec)) {
        if (argument == u"%i") {
            if (!app.icon.isEmpty())
                command << u"--icon"_s << app.icon;
            continue;
        }
        QString expanded;
        bool hadCode = false;
        for (qsizetype i = 0; i < argument.size(); ++i) {
            if (argument[i] != u'%' || i + 1 == argument.size()) {
                expanded += argument[i];
                continue;
            }
            hadCode = true;
            switch (argument[++i].unicode()) {
            case 'c':
                expanded += app.name;
                break;
            case '%':
                expanded += u'%';
                break;
            default: // files, URLs, and the deprecated codes: nothing to put in
                break;
            }
        }
        // An argument that was only a field code with nothing to put in disappears.
        if (!expanded.isEmpty() || !hadCode)
            command << expanded;
    }
    return command;
}

QStringList applicationFolders()
{
    return QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
}

std::vector<Application> loadApplications(const QStringList& desktops)
{
    std::vector<Application> apps;
    QSet<QString> seen;
    for (const QString& folder : applicationFolders()) {
        const QDir base(folder);
        for (const auto& file : QDirListing(folder, {u"*.desktop"_s},
                 QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::FilesOnly)) {
            const QString id = base.relativeFilePath(file.absoluteFilePath()).replace(u'/', u'-');
            if (seen.contains(id))
                continue;
            seen.insert(id);

            const auto entry = readEntry(file.absoluteFilePath());
            if (!entry || entry->value(u"Type"_s) != u"Application" || entry->flag(u"Hidden"_s)
                || entry->value(u"Exec"_s).isEmpty() || !installed(entry->value(u"TryExec"_s)))
                continue;
            apps.push_back(Application {
                .id = id,
                .name = entry->localized(u"Name"_s),
                .genericName = entry->localized(u"GenericName"_s),
                .comment = entry->localized(u"Comment"_s),
                .keywords = entry->localizedList(u"Keywords"_s),
                .exec = entry->value(u"Exec"_s),
                .icon = entry->value(u"Icon"_s),
                .workingDirectory = entry->value(u"Path"_s),
                .startupWmClass = entry->value(u"StartupWMClass"_s),
                .mimeTypes = entry->list(u"MimeType"_s),
                .categories = entry->list(u"Categories"_s),
                .terminal = entry->flag(u"Terminal"_s),
                .inMenus = !entry->flag(u"NoDisplay"_s) && shownIn(*entry, desktops),
            });
            if (apps.back().name.isEmpty())
                apps.back().name = QFileInfo(id).completeBaseName();
        }
    }

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::ranges::sort(
        apps, [&](const Application& a, const Application& b) { return collator.compare(a.name, b.name) < 0; });
    return apps;
}

const Application* findApplication(const std::vector<Application>& apps, const QString& appId)
{
    if (appId.isEmpty())
        return nullptr;
    const auto first = [&](auto&& matches) -> const Application* {
        // Entries for menus win over helpers that happen to match too.
        const Application* found = nullptr;
        for (const Application& app : apps) {
            if (matches(app) && (!found || (app.inMenus && !found->inMenus)))
                found = &app;
        }
        return found;
    };
    const auto fileName = [](const Application& app) { return QFileInfo(app.id).completeBaseName(); };
    const auto program = [](const Application& app) {
        const QStringList command = splitExec(app.exec);
        return command.isEmpty() ? QString() : QFileInfo(command.first()).fileName();
    };
    if (const auto* app = first([&](const Application& a) { return fileName(a) == appId; }))
        return app;
    if (const auto* app = first([&](const Application& a) { return a.startupWmClass == appId; }))
        return app;
    if (const auto* app
        = first([&](const Application& a) { return fileName(a).compare(appId, Qt::CaseInsensitive) == 0; }))
        return app;
    if (const auto* app = first([&](const Application& a) {
            return !a.startupWmClass.isEmpty() && a.startupWmClass.compare(appId, Qt::CaseInsensitive) == 0;
        }))
        return app;
    // org.example.Thing for a window called "thing", and the other way round.
    const QString last = appId.section(u'.', -1);
    if (const auto* app = first([&](const Application& a) {
            return fileName(a).section(u'.', -1).compare(last, Qt::CaseInsensitive) == 0;
        }))
        return app;
    return first([&](const Application& a) { return program(a).compare(last, Qt::CaseInsensitive) == 0; });
}

std::vector<const Application*> search(const std::vector<Application>& apps, const QString& query)
{
    const QStringList words = folded(query).split(u' ', Qt::SkipEmptyParts);
    if (words.isEmpty())
        return {};

    struct Match {
        const Application* app;
        int rank;
    };
    std::vector<Match> matches;
    for (const Application& app : apps) {
        if (!app.inMenus)
            continue;
        int total = 0;
        bool matched = true;
        for (const QString& word : words) {
            const auto wordRank = rank(app, word);
            if (!wordRank) {
                matched = false;
                break;
            }
            total += *wordRank;
        }
        if (matched)
            matches.push_back({&app, total});
    }
    // Stable, so equal matches stay in name order.
    std::ranges::stable_sort(matches, {}, &Match::rank);

    std::vector<const Application*> result;
    result.reserve(matches.size());
    for (const Match& match : matches)
        result.push_back(match.app);
    return result;
}

QStringList launchCommand(const Application& app, bool systemd)
{
    QStringList command = commandLine(app);
    if (command.isEmpty())
        return {};
    if (app.terminal)
        command = QStringList {u"tde-session"_s, u"--terminal"_s, u"--"_s} + command;
    if (systemd)
        command = QStringList {u"systemd-run"_s, u"--user"_s, u"--scope"_s, u"--quiet"_s, u"--collect"_s,
                      u"--unit="_s + unitName(QFileInfo(app.id).completeBaseName()), u"--"_s}
            + command;
    return command;
}

bool launch(const Application& app)
{
    static const bool systemd = !QStandardPaths::findExecutable(u"systemd-run"_s).isEmpty();
    QStringList command = launchCommand(app, systemd);
    if (command.isEmpty())
        return false;
    const QString program = command.takeFirst();
    const QString directory = app.workingDirectory.isEmpty() ? QDir::homePath() : app.workingDirectory;
    // Detached: the program outlives the overview, which never has to wait for it.
    return QProcess::startDetached(program, command, directory);
}

} // namespace shell
