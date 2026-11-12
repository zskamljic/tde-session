#pragma once

#include <QSize>
#include <QString>

#include <optional>

class QPainter;
struct lua_State;

namespace hermes {

class SystemUsage;

// An applet of the bar, written in Lua: a file that returns a table, which says how wide it
// is in the bar, how often it is updated, and draws itself there and, when clicked, in a
// larger panel below it. It reads how busy the computer is through the `tde` table. What goes
// wrong in it is told on stderr; it never takes the bar down, and a call that takes too long
// is stopped.
class LuaApplet {
public:
    LuaApplet(QString path, SystemUsage& usage);
    ~LuaApplet();
    LuaApplet(const LuaApplet&) = delete;
    LuaApplet& operator=(const LuaApplet&) = delete;

    // Runs the file; false when it cannot be run or returns no table.
    bool load();
    QString name() const { return m_name; }
    // The last thing that went wrong, until something works again.
    QString error() const { return m_error; }

    int width() const { return m_width; } // in the bar, in pixels
    int interval() const { return m_interval; } // milliseconds between updates
    std::optional<QSize> popupSize() const { return m_popupSize; } // none without a panel

    void update();
    void paint(QPainter& painter, QSize size);
    void paintPopup(QPainter& painter, QSize size);

private:
    // Calls the function on top of the stack with the applet and `arguments` more below it,
    // within the time allowed; false when it failed, which is told.
    bool call(int arguments);
    // Pushes the hook `name` of the applet, or of the table `within` in it; false when none.
    bool pushHook(const char* name, const char* within = nullptr);
    void paintWith(QPainter& painter, QSize size, const char* within);
    void fail(const QString& message);
    void readSettings();

    QString m_path;
    QString m_name;
    SystemUsage& m_usage;
    lua_State* m_lua = nullptr;
    int m_applet = 0; // the table, kept in the registry
    QString m_error;
    int m_width = 100;
    int m_interval = 1000;
    std::optional<QSize> m_popupSize;
};

} // namespace hermes
