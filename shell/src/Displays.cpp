#include "Displays.hpp"

#include <QGuiApplication>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Qt::StringLiterals;

namespace shell {
namespace {

QString fromUtf8(const char* text)
{
    return QString::fromUtf8(text ? text : "");
}

} // namespace

QString Display::identity() const
{
    // Two of the same model without serial numbers are told apart by where they are plugged in.
    const QString base = QStringList {make, model}.join(u' ').trimmed();
    if (base.isEmpty())
        return name;
    return serial.isEmpty() ? base + u" ("_s + name + u')' : base + u' ' + serial;
}

QString Display::title() const
{
    const QString base = QStringList {make, model}.join(u' ').trimmed();
    return base.isEmpty() ? name : base;
}

DisplaySetting DisplaySetting::of(const Display& display)
{
    DisplaySetting setting;
    setting.name = display.name;
    setting.enabled = display.enabled;
    if (display.mode >= 0 && display.mode < int(display.modes.size())) {
        setting.size = display.modes[size_t(display.mode)].size;
        setting.refresh = display.modes[size_t(display.mode)].refresh;
    }
    setting.position = display.position;
    setting.scale = display.scale;
    setting.transform = display.transform;
    return setting;
}

QSize DisplaySetting::logicalSize() const
{
    const QSize pixels = transform % 2 ? size.transposed() : size;
    return QSize(int(std::lround(pixels.width() * 100.0 / scale)), int(std::lround(pixels.height() * 100.0 / scale)));
}

bool DisplaySetting::operator==(const DisplaySetting& other) const
{
    if (name != other.name || enabled != other.enabled)
        return false;
    return !enabled
        || (size == other.size && refresh == other.refresh && position == other.position && scale == other.scale
            && transform == other.transform);
}

DisplayLayout layoutOf(const std::vector<DisplaySetting>& settings, const std::vector<Display>& displays)
{
    DisplayLayout layout;
    for (const DisplaySetting& setting : settings) {
        const auto display = std::ranges::find(displays, setting.name, &Display::name);
        if (display == displays.end())
            continue;
        layout.push_back({
            .display = display->identity(),
            .enabled = setting.enabled,
            .size = setting.size,
            .refresh = setting.refresh,
            .position = setting.position,
            .scale = setting.scale,
            .transform = setting.transform,
        });
    }
    return layout;
}

std::vector<DisplaySetting> settingsOf(const DisplayLayout& layout, const std::vector<Display>& displays)
{
    std::vector<DisplaySetting> settings;
    for (const auto& placement : layout) {
        const auto display = std::ranges::find_if(
            displays, [&](const Display& display) { return display.identity() == placement.display; });
        if (display == displays.end())
            continue;
        settings.push_back({
            .name = display->name,
            .enabled = placement.enabled,
            .size = placement.size,
            .refresh = placement.refresh,
            .position = placement.position,
            .scale = placement.scale,
            .transform = placement.transform,
        });
    }
    return settings;
}

QStringList identitiesOf(const std::vector<Display>& displays)
{
    QStringList identities;
    for (const Display& display : displays)
        identities << display.identity();
    return identities;
}

bool sameSettings(std::vector<DisplaySetting> a, std::vector<DisplaySetting> b)
{
    std::ranges::sort(a, {}, &DisplaySetting::name);
    std::ranges::sort(b, {}, &DisplaySetting::name);
    return a == b;
}

QString primaryDisplay(const SessionConfig::Displays& settings, const std::vector<Display>& displays)
{
    for (const Display& display : displays) {
        if (display.enabled && display.identity() == settings.primary)
            return display.name;
    }
    const auto first = std::ranges::find_if(displays, &Display::enabled);
    return first == displays.end() ? QString() : first->name;
}

QScreen* primaryScreen(const SessionConfig::Displays& settings, const std::vector<Display>& displays)
{
    const QString name = primaryDisplay(settings, displays);
    const QList<QScreen*> screens = QGuiApplication::screens();
    const auto it = std::ranges::find(screens, name, &QScreen::name);
    return it != screens.end() ? *it : QGuiApplication::primaryScreen();
}

struct Displays::Mode {
    Head* head = nullptr;
    Proxy<zwlr_output_mode_v1> handle;
    DisplayMode info;
};

// A display as it is announced: what arrives is kept until the manager says it is done.
struct Displays::Head {
    Displays* owner = nullptr;
    Proxy<zwlr_output_head_v1> handle;
    Display display;
    std::vector<std::unique_ptr<Mode>> modes;
    zwlr_output_mode_v1* current = nullptr;
};

struct Displays::Configuration {
    Displays* owner = nullptr;
    Proxy<zwlr_output_configuration_v1> handle;
    std::vector<Proxy<zwlr_output_configuration_head_v1>> heads;
    std::function<void(bool)> done;
};

Displays::Displays(QObject* parent)
    : QObject(parent)
{
    auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wayland)
        return;
    m_display = wayland->display();
    static const wl_registry_listener listener {
        .global = global,
        .global_remove = [](void*, wl_registry*, uint32_t) { },
    };
    m_registry.reset(wl_display_get_registry(m_display));
    wl_registry_add_listener(m_registry.get(), &listener, this);
    // Whether there is a manager is known at once; the displays follow, with changed().
    wl_display_roundtrip(m_display);
}

Displays::~Displays()
{
    m_configurations.clear();
    m_heads.clear();
}

void Displays::global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
{
    auto* self = static_cast<Displays*>(data);
    // Version 3 lets heads and modes be released, and tells make, model and serial number.
    if (std::strcmp(interface, zwlr_output_manager_v1_interface.name) != 0 || version < 3)
        return;
    self->m_manager.reset(static_cast<zwlr_output_manager_v1*>(
        wl_registry_bind(registry, name, &zwlr_output_manager_v1_interface, std::min(version, 4u))));
    static const zwlr_output_manager_v1_listener listener {
        .head = [](void* data, zwlr_output_manager_v1*,
                    zwlr_output_head_v1* head) { static_cast<Displays*>(data)->addHead(head); },
        .done
        = [](void* data, zwlr_output_manager_v1*, uint32_t serial) { static_cast<Displays*>(data)->done(serial); },
        .finished = [](void*, zwlr_output_manager_v1*) { },
    };
    zwlr_output_manager_v1_add_listener(self->m_manager.get(), &listener, self);
}

void Displays::addHead(zwlr_output_head_v1* handle)
{
    auto head = std::make_unique<Head>();
    head->owner = this;
    head->handle.reset(handle);

    static const zwlr_output_head_v1_listener listener {
        .name = [](void* data, zwlr_output_head_v1*,
                    const char* name) { static_cast<Head*>(data)->display.name = fromUtf8(name); },
        .description = [](void*, zwlr_output_head_v1*, const char*) { },
        .physical_size = [](void*, zwlr_output_head_v1*, int32_t, int32_t) { },
        .mode =
            [](void* data, zwlr_output_head_v1*, zwlr_output_mode_v1* handle) {
                auto* head = static_cast<Head*>(data);
                auto& mode = head->modes.emplace_back(std::make_unique<Mode>());
                mode->head = head;
                mode->handle.reset(handle);
                static const zwlr_output_mode_v1_listener modeListener {
                    .size = [](void* data, zwlr_output_mode_v1*, int32_t width,
                                int32_t height) { static_cast<Mode*>(data)->info.size = QSize(width, height); },
                    .refresh = [](void* data, zwlr_output_mode_v1*,
                                   int32_t refresh) { static_cast<Mode*>(data)->info.refresh = refresh; },
                    .preferred = [](void*, zwlr_output_mode_v1*) { },
                    .finished =
                        [](void* data, zwlr_output_mode_v1*) {
                            auto* mode = static_cast<Mode*>(data);
                            std::erase_if(mode->head->modes, [mode](const auto& m) { return m.get() == mode; });
                        },
                };
                zwlr_output_mode_v1_add_listener(handle, &modeListener, mode.get());
            },
        .enabled = [](void* data, zwlr_output_head_v1*,
                       int32_t enabled) { static_cast<Head*>(data)->display.enabled = enabled != 0; },
        .current_mode
        = [](void* data, zwlr_output_head_v1*, zwlr_output_mode_v1* mode) { static_cast<Head*>(data)->current = mode; },
        .position = [](void* data, zwlr_output_head_v1*, int32_t x,
                        int32_t y) { static_cast<Head*>(data)->display.position = QPoint(x, y); },
        .transform = [](void* data, zwlr_output_head_v1*,
                         int32_t transform) { static_cast<Head*>(data)->display.transform = transform; },
        .scale =
            [](void* data, zwlr_output_head_v1*, wl_fixed_t scale) {
                static_cast<Head*>(data)->display.scale = int(std::lround(wl_fixed_to_double(scale) * 100));
            },
        .finished =
            [](void* data, zwlr_output_head_v1*) {
                // Unplugged: gone from the list once the manager is done.
                auto* head = static_cast<Head*>(data);
                std::erase_if(head->owner->m_heads, [head](const auto& h) { return h.get() == head; });
            },
        .make = [](void* data, zwlr_output_head_v1*,
                    const char* make) { static_cast<Head*>(data)->display.make = fromUtf8(make); },
        .model = [](void* data, zwlr_output_head_v1*,
                     const char* model) { static_cast<Head*>(data)->display.model = fromUtf8(model); },
        .serial_number = [](void* data, zwlr_output_head_v1*,
                             const char* serial) { static_cast<Head*>(data)->display.serial = fromUtf8(serial); },
        .adaptive_sync = [](void*, zwlr_output_head_v1*, uint32_t) { },
    };
    zwlr_output_head_v1_add_listener(handle, &listener, head.get());
    m_heads.push_back(std::move(head));
}

void Displays::done(uint32_t serial)
{
    m_serial = serial;
    m_displays.clear();
    for (const auto& head : m_heads) {
        Display display = head->display;
        display.modes.clear();
        display.mode = -1;
        for (const auto& mode : head->modes) {
            if (mode->handle.get() == head->current)
                display.mode = int(display.modes.size());
            display.modes.push_back(mode->info);
        }
        if (!display.enabled)
            display.mode = -1;
        m_displays.push_back(std::move(display));
    }
    // Left to right, as they stand.
    std::ranges::stable_sort(m_displays, [](const Display& a, const Display& b) {
        if (a.enabled != b.enabled)
            return a.enabled;
        return a.position.x() != b.position.x() ? a.position.x() < b.position.x() : a.position.y() < b.position.y();
    });
    emit changed();
}

std::vector<DisplaySetting> Displays::settings() const
{
    std::vector<DisplaySetting> settings;
    for (const Display& display : m_displays)
        settings.push_back(DisplaySetting::of(display));
    return settings;
}

void Displays::apply(const std::vector<DisplaySetting>& settings, std::function<void(bool)> done)
{
    if (!m_manager) {
        if (done)
            done(false);
        return;
    }
    auto configuration = std::make_unique<Configuration>();
    configuration->owner = this;
    configuration->done = std::move(done);
    configuration->handle.reset(zwlr_output_manager_v1_create_configuration(m_manager.get(), m_serial));

    for (const auto& head : m_heads) {
        const auto setting = std::ranges::find(settings, head->display.name, &DisplaySetting::name);
        const DisplaySetting wanted = setting != settings.end() ? *setting : DisplaySetting::of(head->display);
        if (!wanted.enabled) {
            zwlr_output_configuration_v1_disable_head(configuration->handle.get(), head->handle.get());
            continue;
        }
        auto* config = zwlr_output_configuration_v1_enable_head(configuration->handle.get(), head->handle.get());
        configuration->heads.emplace_back(config);
        // The mode of that size with the nearest refresh rate, or that size made up.
        const Mode* best = nullptr;
        for (const auto& mode : head->modes) {
            if (mode->info.size == wanted.size
                && (!best
                    || std::abs(mode->info.refresh - wanted.refresh) < std::abs(best->info.refresh - wanted.refresh)))
                best = mode.get();
        }
        if (best)
            zwlr_output_configuration_head_v1_set_mode(config, best->handle.get());
        else if (wanted.size.isValid())
            zwlr_output_configuration_head_v1_set_custom_mode(
                config, wanted.size.width(), wanted.size.height(), wanted.refresh);
        zwlr_output_configuration_head_v1_set_position(config, wanted.position.x(), wanted.position.y());
        zwlr_output_configuration_head_v1_set_transform(config, wanted.transform);
        zwlr_output_configuration_head_v1_set_scale(config, wl_fixed_from_double(wanted.scale / 100.0));
    }

    static constexpr auto answered = [](void* data, bool succeeded) {
        auto* configuration = static_cast<Configuration*>(data);
        configuration->owner->finished(*configuration, succeeded);
    };
    static const zwlr_output_configuration_v1_listener listener {
        .succeeded = [](void* data, zwlr_output_configuration_v1*) { answered(data, true); },
        .failed = [](void* data, zwlr_output_configuration_v1*) { answered(data, false); },
        // The displays changed meanwhile: asked again, it would be about what is there now.
        .cancelled = [](void* data, zwlr_output_configuration_v1*) { answered(data, false); },
    };
    zwlr_output_configuration_v1_add_listener(configuration->handle.get(), &listener, configuration.get());
    zwlr_output_configuration_v1_apply(configuration->handle.get());
    wl_display_flush(m_display);
    m_configurations.push_back(std::move(configuration));
}

void Displays::finished(Configuration& configuration, bool succeeded)
{
    // Destroys the configuration that called; nothing of it may run after this.
    auto done = std::move(configuration.done);
    std::erase_if(m_configurations, [&](const auto& c) { return c.get() == &configuration; });
    if (done)
        done(succeeded);
}

} // namespace shell
