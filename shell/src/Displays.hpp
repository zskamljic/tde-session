#pragma once

#include "wlr-output-management-unstable-v1-client-protocol.h"

#include <Proxy.hpp>
#include <SessionConfig.hpp>

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QScreen>
#include <QSize>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

SHELL_PROXY(zwlr_output_manager_v1, zwlr_output_manager_v1_destroy);
SHELL_PROXY(zwlr_output_head_v1, zwlr_output_head_v1_release);
SHELL_PROXY(zwlr_output_mode_v1, zwlr_output_mode_v1_release);
SHELL_PROXY(zwlr_output_configuration_v1, zwlr_output_configuration_v1_destroy);
SHELL_PROXY(zwlr_output_configuration_head_v1, zwlr_output_configuration_head_v1_destroy);

namespace shell {

struct DisplayMode {
    QSize size {}; // pixels
    int refresh = 0; // mHz; 0 when not known

    bool operator==(const DisplayMode&) const = default;
};

// A display as the compositor has it.
struct Display {
    QString name; // of its connector, such as "DP-1"
    QString make;
    QString model;
    QString serial;
    std::vector<DisplayMode> modes;

    bool enabled = false;
    int mode = -1; // the one in use, of modes
    QPoint position {}; // in the layout of all of them
    int scale = 100; // percent
    int transform = 0; // as wl_output has it: rotations, then flipped

    // What tells this display apart from others, the same wherever it is plugged in.
    QString identity() const;
    // A name for people: its make and model.
    QString title() const;
};

// What a display is to be: turned off, or on with this mode, place, scale and turn.
struct DisplaySetting {
    QString name;
    bool enabled = true;
    QSize size {};
    int refresh = 0;
    QPoint position {};
    int scale = 100; // percent
    int transform = 0;

    static DisplaySetting of(const Display& display);
    // How much of the layout the display covers, and where.
    QSize logicalSize() const;
    QRect area() const { return {position, logicalSize()}; }

    // A display turned off is only that, whatever mode or place it would have.
    bool operator==(const DisplaySetting& other) const;
};

// Between the displays as they are set and the layouts the session's settings keep, which
// know displays by their identity rather than where they are plugged in.
using DisplayLayout = SessionConfig::Displays::Layout;
DisplayLayout layoutOf(const std::vector<DisplaySetting>& settings, const std::vector<Display>& displays);
std::vector<DisplaySetting> settingsOf(const DisplayLayout& layout, const std::vector<Display>& displays);
QStringList identitiesOf(const std::vector<Display>& displays);
// Whether two sets of settings set the displays alike, in whatever order they come.
bool sameSettings(std::vector<DisplaySetting> a, std::vector<DisplaySetting> b);
// The connector of the display with the bar: the one the settings name, or else the first
// one turned on.
QString primaryDisplay(const SessionConfig::Displays& settings, const std::vector<Display>& displays);
// Its screen, or the first one when there is none of that name.
QScreen* primaryScreen(const SessionConfig::Displays& settings, const std::vector<Display>& displays);

// The displays of the compositor Qt is connected to, through wlr-output-management, which
// can also change them.
class Displays : public QObject {
    Q_OBJECT

public:
    explicit Displays(QObject* parent = nullptr);
    ~Displays() override;

    bool isSupported() const { return m_manager != nullptr; }
    const std::vector<Display>& displays() const { return m_displays; }
    // How they are set now.
    std::vector<DisplaySetting> settings() const;

    // Sets every display at once, or none of them; `done` hears which.
    void apply(const std::vector<DisplaySetting>& settings, std::function<void(bool)> done = {});

signals:
    void changed();

private:
    struct Head;
    struct Mode;
    struct Configuration;

    static void global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
    void addHead(zwlr_output_head_v1* handle);
    void done(uint32_t serial);
    void finished(Configuration& configuration, bool succeeded);

    wl_display* m_display = nullptr;
    Proxy<wl_registry> m_registry;
    Proxy<zwlr_output_manager_v1> m_manager;
    uint32_t m_serial = 0;
    std::vector<std::unique_ptr<Head>> m_heads;
    std::vector<std::unique_ptr<Configuration>> m_configurations;
    std::vector<Display> m_displays;
};

} // namespace shell
