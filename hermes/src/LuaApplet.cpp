#include "LuaApplet.hpp"

#include "SystemUsage.hpp"

#include <tde/Theme.hpp>

#include <QColor>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <print>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

using namespace Qt::StringLiterals;

// Lua raises its errors with longjmp, which skips the destructors of C++ objects on the way.
// The functions it calls here therefore check all they are given before they make anything
// that needs one.

namespace hermes {
namespace {

constexpr int Api = 1; // what applets are written against, as tde.api tells them
constexpr qint64 Allowed = 200; // ms a call may take before it is stopped
constexpr int MinInterval = 100; // ms
constexpr int MaxWidth = 1000; // pixels in the bar
constexpr int MaxPopup = 2000; // pixels each way
const char* const CanvasType = "tde.canvas";

// Where a call is up to, for the hook that stops one taking too long.
struct Running {
    QElapsedTimer timer;
};

Running*& running(lua_State* L)
{
    return *static_cast<Running**>(lua_getextraspace(L));
}

void stopSlowCall(lua_State* L, lua_Debug*)
{
    if (const Running* call = running(L); call && call->timer.elapsed() > Allowed)
        luaL_error(L, "took longer than %d ms, so it was stopped", int(Allowed));
}

SystemUsage& usageOf(lua_State* L)
{
    return *static_cast<SystemUsage*>(lua_touserdata(L, lua_upvalueindex(1)));
}

void setNumber(lua_State* L, const char* key, double value)
{
    lua_pushnumber(L, value);
    lua_setfield(L, -2, key);
}

int cpu(lua_State* L)
{
    const CpuUsage usage = usageOf(L).cpu();
    lua_createtable(L, 0, 2);
    setNumber(L, "total", usage.total);
    lua_createtable(L, int(usage.cores.size()), 0);
    for (size_t i = 0; i < usage.cores.size(); ++i) {
        lua_pushnumber(L, usage.cores[i]);
        lua_rawseti(L, -2, lua_Integer(i + 1));
    }
    lua_setfield(L, -2, "cores");
    return 1;
}

int memory(lua_State* L)
{
    const MemoryUsage usage = usageOf(L).memory();
    lua_createtable(L, 0, 4);
    setNumber(L, "total", double(usage.total));
    setNumber(L, "used", double(usage.used));
    setNumber(L, "swap_total", double(usage.swapTotal));
    setNumber(L, "swap_used", double(usage.swapUsed));
    return 1;
}

int network(lua_State* L)
{
    const NetworkSpeed speed = usageOf(L).network();
    lua_createtable(L, 0, 2);
    setNumber(L, "down", speed.down);
    setNumber(L, "up", speed.up);
    return 1;
}

// tde.theme: the desktop's colours, as "#aarrggbb", made anew before every call, as the
// theme may have changed since.
void pushTheme(lua_State* L)
{
    const auto& colors = tde::theme::colors();
    const std::pair<const char*, QColor> named[] = {
        {"window", colors.window},
        {"base", colors.base},
        {"header", colors.header},
        {"text", colors.text},
        {"dim_text", colors.dimText},
        {"accent", colors.accent},
        {"accent_text", colors.accentText},
        {"border", colors.border},
        {"error", colors.error},
    };
    lua_createtable(L, 0, int(std::size(named)));
    for (const auto& [name, color] : named) {
        lua_pushstring(L, color.name(QColor::HexArgb).toUtf8().constData());
        lua_setfield(L, -2, name);
    }
}

// The canvas an applet draws on, valid only while it draws.
struct Canvas {
    QPainter* painter = nullptr;
    int width = 0;
    int height = 0;
};

Canvas& checkCanvas(lua_State* L)
{
    auto* canvas = static_cast<Canvas*>(luaL_checkudata(L, 1, CanvasType));
    if (!canvas->painter)
        luaL_error(L, "the canvas is drawn on only within paint");
    return *canvas;
}

QColor checkColor(lua_State* L, int index)
{
    const char* name = luaL_checkstring(L, index);
    const QColor color = QColor::fromString(QLatin1StringView(name));
    if (!color.isValid())
        luaL_argerror(L, index, "expected a colour such as \"#rrggbb\" or \"#aarrggbb\"");
    return color;
}

// A list of numbers, x and y in turn, of at least `minimum` points; checked before any is used.
int checkPoints(lua_State* L, int index, int minimum)
{
    luaL_checktype(L, index, LUA_TTABLE);
    const auto length = lua_Integer(lua_rawlen(L, index));
    if (length % 2 != 0 || length < 2 * minimum)
        luaL_argerror(L, index, "expected x and y in turn, for enough points");
    for (lua_Integer i = 1; i <= length; ++i) {
        if (lua_rawgeti(L, index, i) != LUA_TNUMBER)
            luaL_argerror(L, index, "expected numbers only");
        lua_pop(L, 1);
    }
    return int(length / 2);
}

QPolygonF points(lua_State* L, int index, int count)
{
    QPolygonF polygon;
    polygon.reserve(count);
    for (int i = 0; i < count; ++i) {
        lua_rawgeti(L, index, 2 * i + 1);
        lua_rawgeti(L, index, 2 * i + 2);
        polygon << QPointF(lua_tonumber(L, -2), lua_tonumber(L, -1));
        lua_pop(L, 2);
    }
    return polygon;
}

// canvas:rect(x, y, width, height, color [, radius])
int canvasRect(lua_State* L)
{
    Canvas& canvas = checkCanvas(L);
    const QRectF rect(luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_checknumber(L, 4), luaL_checknumber(L, 5));
    const QColor color = checkColor(L, 6);
    const double radius = luaL_optnumber(L, 7, 0);
    canvas.painter->setPen(Qt::NoPen);
    canvas.painter->setBrush(color);
    canvas.painter->drawRoundedRect(rect, radius, radius);
    return 0;
}

// canvas:line({x1, y1, x2, y2, ...}, color [, width])
int canvasLine(lua_State* L)
{
    Canvas& canvas = checkCanvas(L);
    const int count = checkPoints(L, 2, 2);
    const QColor color = checkColor(L, 3);
    const double width = luaL_optnumber(L, 4, 1);
    const QPolygonF line = points(L, 2, count);
    canvas.painter->setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    canvas.painter->setBrush(Qt::NoBrush);
    canvas.painter->drawPolyline(line);
    return 0;
}

// canvas:area({x1, y1, x2, y2, x3, y3, ...}, color)
int canvasArea(lua_State* L)
{
    Canvas& canvas = checkCanvas(L);
    const int count = checkPoints(L, 2, 3);
    const QColor color = checkColor(L, 3);
    const QPolygonF area = points(L, 2, count);
    canvas.painter->setPen(Qt::NoPen);
    canvas.painter->setBrush(color);
    canvas.painter->drawPolygon(area);
    return 0;
}

// canvas:text(x, y, width, height, text, color [, { size = 9, bold = false, align = "left" }]),
// centred from top to bottom within the box.
int canvasText(lua_State* L)
{
    Canvas& canvas = checkCanvas(L);
    const double x = luaL_checknumber(L, 2), y = luaL_checknumber(L, 3);
    const double width = luaL_checknumber(L, 4), height = luaL_checknumber(L, 5);
    const char* text = luaL_checkstring(L, 6);
    const QColor color = checkColor(L, 7);
    double size = 0;
    bool bold = false;
    Qt::Alignment align = Qt::AlignLeft;
    if (!lua_isnoneornil(L, 8)) {
        luaL_checktype(L, 8, LUA_TTABLE);
        if (lua_getfield(L, 8, "size") != LUA_TNIL)
            size = luaL_checknumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 8, "bold");
        bold = lua_toboolean(L, -1);
        lua_pop(L, 1);
        if (lua_getfield(L, 8, "align") != LUA_TNIL) {
            const char* const names[] = {"left", "center", "right", nullptr};
            const int picked = luaL_checkoption(L, -1, nullptr, names);
            align = picked == 0 ? Qt::AlignLeft : picked == 1 ? Qt::AlignHCenter : Qt::AlignRight;
        }
        lua_pop(L, 1);
    }
    QFont font = canvas.painter->font();
    if (size > 0)
        font.setPointSizeF(size);
    font.setBold(bold);
    canvas.painter->setFont(font);
    canvas.painter->setPen(color);
    canvas.painter->drawText(QRectF(x, y, width, height), int(align | Qt::AlignVCenter), QString::fromUtf8(text));
    return 0;
}

// canvas.width and canvas.height, and the methods.
int canvasIndex(lua_State* L)
{
    const auto* canvas = static_cast<Canvas*>(luaL_checkudata(L, 1, CanvasType));
    const char* key = luaL_checkstring(L, 2);
    if (std::strcmp(key, "width") == 0)
        lua_pushinteger(L, canvas->width);
    else if (std::strcmp(key, "height") == 0)
        lua_pushinteger(L, canvas->height);
    else
        lua_getfield(L, lua_upvalueindex(1), key);
    return 1;
}

void registerCanvas(lua_State* L)
{
    luaL_newmetatable(L, CanvasType);
    const luaL_Reg methods[] = {
        {"rect", canvasRect},
        {"line", canvasLine},
        {"area", canvasArea},
        {"text", canvasText},
        {nullptr, nullptr},
    };
    luaL_newlib(L, methods);
    lua_pushcclosure(L, canvasIndex, 1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
}

// Only what needs nothing outside the applet: no files, no programs, no leaving the bar.
void openLibraries(lua_State* L)
{
    const luaL_Reg libraries[] = {
        {LUA_GNAME, luaopen_base},
        {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string},
        {LUA_MATHLIBNAME, luaopen_math},
        {LUA_UTF8LIBNAME, luaopen_utf8},
    };
    for (const luaL_Reg& library : libraries) {
        luaL_requiref(L, library.name, library.func, 1);
        lua_pop(L, 1);
    }
    // The base library can still read and run files.
    for (const char* name : {"dofile", "loadfile"}) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
}

} // namespace

LuaApplet::LuaApplet(QString path, SystemUsage& usage)
    : m_path(std::move(path))
    , m_name(QFileInfo(m_path).completeBaseName())
    , m_usage(usage)
{
}

LuaApplet::~LuaApplet()
{
    if (m_lua)
        lua_close(m_lua);
}

bool LuaApplet::load()
{
    m_lua = luaL_newstate();
    if (!m_lua) {
        fail(u"cannot make a Lua state"_s);
        return false;
    }
    lua_State* L = m_lua;
    running(L) = nullptr;
    lua_sethook(L, stopSlowCall, LUA_MASKCOUNT, 1000);
    openLibraries(L);
    registerCanvas(L);

    const luaL_Reg calls[] = {
        {"cpu", cpu},
        {"memory", memory},
        {"network", network},
        {nullptr, nullptr},
    };
    luaL_newlibtable(L, calls);
    lua_pushlightuserdata(L, &m_usage);
    luaL_setfuncs(L, calls, 1);
    lua_pushinteger(L, Api);
    lua_setfield(L, -2, "api");
    lua_setglobal(L, "tde");

    if (luaL_loadfile(L, QFile::encodeName(m_path).constData()) != LUA_OK) {
        fail(QString::fromUtf8(lua_tostring(L, -1)));
        lua_pop(L, 1);
        return false;
    }
    // The file itself is called as a hook would be, with no applet yet.
    if (!call(-1))
        return false;
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        fail(u"the file must return a table"_s);
        return false;
    }
    m_applet = luaL_ref(L, LUA_REGISTRYINDEX);
    readSettings();
    return true;
}

void LuaApplet::readSettings()
{
    // Only raw reads, which run none of the applet's code.
    lua_State* L = m_lua;
    lua_rawgeti(L, LUA_REGISTRYINDEX, m_applet);
    const auto integer = [L](const char* key, int min, int max, int fallback) {
        lua_pushstring(L, key);
        lua_rawget(L, -2);
        int isInteger = 0;
        const lua_Integer value = lua_tointegerx(L, -1, &isInteger);
        lua_pop(L, 1);
        return isInteger ? int(std::clamp<lua_Integer>(value, min, max)) : fallback;
    };
    m_width = integer("width", 1, MaxWidth, 100);
    m_interval = integer("interval", MinInterval, 24 * 60 * 60 * 1000, 1000);
    lua_pushstring(L, "popup");
    if (lua_rawget(L, -1 - 1) == LUA_TTABLE) {
        const int width = integer("width", 1, MaxPopup, 0);
        const int height = integer("height", 1, MaxPopup, 0);
        if (width && height)
            m_popupSize = QSize(width, height);
        else
            fail(u"popup needs a width and a height"_s);
    }
    lua_pop(L, 2);
}

bool LuaApplet::pushHook(const char* name, const char* within)
{
    lua_State* L = m_lua;
    if (!L || !m_applet)
        return false;
    lua_rawgeti(L, LUA_REGISTRYINDEX, m_applet);
    if (within) {
        lua_pushstring(L, within);
        if (lua_rawget(L, -2) != LUA_TTABLE) {
            lua_pop(L, 2);
            return false;
        }
        lua_remove(L, -2);
    }
    lua_pushstring(L, name);
    if (lua_rawget(L, -2) != LUA_TFUNCTION) {
        lua_pop(L, 2);
        return false;
    }
    lua_remove(L, -2);
    // The applet itself is the first argument, as with a method.
    lua_rawgeti(L, LUA_REGISTRYINDEX, m_applet);
    return true;
}

bool LuaApplet::call(int arguments)
{
    lua_State* L = m_lua;
    pushTheme(L);
    lua_getglobal(L, "tde");
    lua_insert(L, -2);
    lua_setfield(L, -2, "theme");
    lua_pop(L, 1);

    Running call;
    call.timer.start();
    running(L) = &call;
    // -1 is the file being run: no applet goes with it, and it returns one value.
    const int status = arguments < 0 ? lua_pcall(L, 0, 1, 0) : lua_pcall(L, arguments + 1, 0, 0);
    running(L) = nullptr;
    if (status != LUA_OK) {
        fail(QString::fromUtf8(lua_tostring(L, -1)));
        lua_pop(L, 1);
        return false;
    }
    m_error.clear();
    return true;
}

void LuaApplet::update()
{
    if (pushHook("update"))
        call(0);
}

void LuaApplet::paint(QPainter& painter, QSize size)
{
    paintWith(painter, size, nullptr);
}

void LuaApplet::paintPopup(QPainter& painter, QSize size)
{
    paintWith(painter, size, "popup");
}

void LuaApplet::paintWith(QPainter& painter, QSize size, const char* within)
{
    if (!pushHook("paint", within))
        return;
    lua_State* L = m_lua;
    auto* canvas = static_cast<Canvas*>(lua_newuserdatauv(L, sizeof(Canvas), 0));
    *canvas = {&painter, size.width(), size.height()};
    luaL_setmetatable(L, CanvasType);
    // Kept by the applet past the call, it can no longer be drawn on.
    lua_pushvalue(L, -1);
    const int kept = luaL_ref(L, LUA_REGISTRYINDEX);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setClipRect(QRect(QPoint(), size));
    call(1);
    painter.restore();

    canvas->painter = nullptr;
    luaL_unref(L, LUA_REGISTRYINDEX, kept);
}

void LuaApplet::fail(const QString& message)
{
    // Told once, not each second it goes on going wrong.
    if (message == m_error)
        return;
    m_error = message;
    std::println(stderr, "tde-hermes: applet {}: {}", m_name.toStdString(), message.toStdString());
}

} // namespace hermes
