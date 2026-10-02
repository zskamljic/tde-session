#include "Groups.hpp"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace hermes {
namespace {

const OpenWindow* findWindow(const std::vector<OpenWindow>& windows, quint64 id)
{
    const auto it = std::ranges::find(windows, id, &OpenWindow::id);
    return it == windows.end() ? nullptr : &*it;
}

} // namespace

std::vector<Group> groupWindows(const std::vector<OpenWindow>& windows, const std::vector<shell::Application>& apps)
{
    std::vector<Group> groups;
    for (const OpenWindow& window : windows) {
        const shell::Application* app = shell::findApplication(apps, window.appId);
        // Windows without an app id each get a button of their own.
        const QString key = app ? app->id : window.appId.isEmpty() ? u"#%1"_s.arg(window.id) : window.appId;
        auto group = std::ranges::find(groups, key, &Group::key);
        if (group == groups.end()) {
            groups.push_back(Group {.key = key, .app = app, .windows = {}});
            group = groups.end() - 1;
        }
        group->windows.push_back(window.id);
    }
    return groups;
}

Click click(const Group& group, const std::vector<OpenWindow>& windows, const std::vector<quint64>& recent)
{
    const auto active = std::ranges::find_if(group.windows, [&](quint64 id) {
        const OpenWindow* window = findWindow(windows, id);
        return window && window->activated && !window->minimized;
    });
    if (active != group.windows.end()) {
        if (group.windows.size() == 1)
            return {Click::Minimize, *active};
        const auto next = active + 1 == group.windows.end() ? group.windows.begin() : active + 1;
        return {Click::Activate, *next};
    }

    for (const quint64 id : recent) {
        if (std::ranges::contains(group.windows, id))
            return {Click::Activate, id};
    }
    return {Click::Activate, group.windows.front()};
}

bool isWindowOf(const QString& appId, const QString& desktopEntry, const QString& appName)
{
    if (appId.isEmpty())
        return false;
    // Without spaces, dashes and case: "Text Editor" is "texteditor".
    const auto simple = [](QString text) {
        text.remove(u' ');
        text.remove(u'-');
        text.remove(u'_');
        return text.toLower();
    };
    const QString window = simple(appId);
    const QString windowLast = simple(appId.section(u'.', -1));
    // The desktop entry by its whole name or its last part: "org.mozilla.firefox" or "firefox".
    const QString entryName = QString(desktopEntry).remove(u".desktop"_s);
    if (!entryName.isEmpty() && (window == simple(entryName) || windowLast == simple(entryName.section(u'.', -1))))
        return true;
    const QString name = simple(appName);
    return !name.isEmpty() && (window == name || windowLast == name);
}

} // namespace hermes
