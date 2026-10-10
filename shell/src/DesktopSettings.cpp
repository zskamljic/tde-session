#include "DesktopSettings.hpp"

#include <tde/DesktopConfig.hpp>
#include <tde/LuaConfig.hpp>

#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>

#include <vector>

using namespace Qt::StringLiterals;

namespace shell {
namespace {

// The end of a long bracket such as [==[ starting at `start`, past its closing ]==]; -1 when
// there is none at `start`.
qsizetype longBracketEnd(const QString& text, qsizetype start)
{
    if (start >= text.size() || text[start] != u'[')
        return -1;
    qsizetype i = start + 1;
    while (i < text.size() && text[i] == u'=')
        ++i;
    if (i >= text.size() || text[i] != u'[')
        return -1;
    const QString closing = u']' + QString(i - start - 1, u'=') + u']';
    const qsizetype end = text.indexOf(closing, i + 1);
    return end < 0 ? text.size() : end + closing.size();
}

// How deep in braces every character of the code is; -1 for those in comments and strings.
std::vector<int> depths(const QString& text)
{
    std::vector<int> depth(size_t(text.size()), -1);
    int level = 0;
    qsizetype i = 0;
    while (i < text.size()) {
        const QChar c = text[i];
        if (c == u'-' && i + 1 < text.size() && text[i + 1] == u'-') {
            const qsizetype end = longBracketEnd(text, i + 2);
            if (end >= 0) {
                i = end;
            } else {
                const qsizetype line = text.indexOf(u'\n', i);
                i = line < 0 ? text.size() : line;
            }
            continue;
        }
        if (c == u'"' || c == u'\'') {
            qsizetype j = i + 1;
            while (j < text.size() && text[j] != c && text[j] != u'\n')
                j += text[j] == u'\\' ? 2 : 1;
            i = j + 1;
            continue;
        }
        if (const qsizetype end = longBracketEnd(text, i); end >= 0) {
            i = end;
            continue;
        }
        if (c == u'}')
            --level;
        depth[size_t(i)] = level;
        if (c == u'{')
            ++level;
        ++i;
    }
    return depth;
}

// The first match of `pattern` from `from` to `to` that starts in code `level` braces deep.
QRegularExpressionMatch matchAt(
    const QString& text, const std::vector<int>& depth, const QString& pattern, int level, qsizetype from, qsizetype to)
{
    const QRegularExpression expression(pattern);
    auto matches = expression.globalMatch(text, from);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        if (match.capturedStart() >= to)
            break;
        if (depth[size_t(match.capturedStart())] == level)
            return match;
    }
    return {};
}

// Where the brace closing the one at `open` is.
qsizetype closingBrace(const QString& text, const std::vector<int>& depth, qsizetype open)
{
    const int level = depth[size_t(open)];
    for (qsizetype i = open + 1; i < text.size(); ++i) {
        if (text[i] == u'}' && depth[size_t(i)] == level)
            return i;
    }
    return -1;
}

// Where the table the file returns ends, its closing brace; -1 when there is none.
qsizetype returnedEnd(const QString& text, const std::vector<int>& depth)
{
    for (qsizetype i = text.size() - 1; i >= 0; --i) {
        if (text[i] == u'}' && depth[size_t(i)] == 0)
            return i;
    }
    return -1;
}

// Replaces the value of the setting `found`, at `level` braces deep: up to the comma, the
// line's end or the table's.
QString replaced(QString text, const std::vector<int>& depth, const QRegularExpressionMatch& found, int level,
    qsizetype close, const QString& value)
{
    const qsizetype start = found.capturedEnd();
    qsizetype end = start;
    while (end < close) {
        const int at = depth[size_t(end)];
        if (at == level && (text[end] == u',' || text[end] == u'\n'))
            break;
        if (at < 0 && text.mid(end, 2) == u"--")
            break;
        ++end;
    }
    while (end > start && text[end - 1].isSpace())
        --end;
    return text.replace(start, end - start, value);
}

QString edited(QString text, const QString& table, const QString& key, const QString& value)
{
    const std::vector<int> depth = depths(text);
    const qsizetype end = returnedEnd(text, depth);
    if (end < 0)
        return {};
    const QString setting = uR"(\b%1\s*=\s*)"_s.arg(QRegularExpression::escape(key));
    if (table.isEmpty()) {
        const QRegularExpressionMatch found = matchAt(text, depth, setting, 1, 0, end);
        if (!found.hasMatch())
            return text.insert(end, u"    %1 = %2,\n"_s.arg(key, value));
        return replaced(text, depth, found, 1, end, value);
    }

    // The table the file returns holds the others, one brace deep.
    const QRegularExpressionMatch found
        = matchAt(text, depth, uR"(\b%1\s*=\s*\{)"_s.arg(QRegularExpression::escape(table)), 1, 0, text.size());
    if (!found.hasMatch())
        return text.insert(end, u"    %1 = { %2 = %3 },\n"_s.arg(table, key, value));
    const qsizetype open = found.capturedEnd() - 1;
    const qsizetype close = closingBrace(text, depth, open);
    if (close < 0)
        return {};
    const QRegularExpressionMatch inside = matchAt(text, depth, setting, 2, open + 1, close);
    if (!inside.hasMatch())
        return text.insert(open + 1, u" %1 = %2,"_s.arg(key, value));
    return replaced(text, depth, inside, 2, close, value);
}

} // namespace

bool setDesktopSetting(const QString& path, const QString& table, const QString& key, const QString& value)
{
    tde::createDesktopConfig(path);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QString text = edited(QString::fromUtf8(file.readAll()), table, key, value);
    file.close();
    if (text.isEmpty())
        return false;

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    out.write(text.toUtf8());
    return out.commit();
}

bool setDesktopString(const QString& path, const QString& table, const QString& key, const QString& value)
{
    return setDesktopSetting(path, table, key, tde::luaString(value));
}

} // namespace shell
