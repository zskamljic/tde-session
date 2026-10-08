#include "SoundPage.hpp"

#include <tde/Theme.hpp>

#include <pulse/pulseaudio.h>

#include <QComboBox>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace daedalus {

struct SoundDevices::Loop {
    pa_threaded_mainloop* mainloop = nullptr;
    pa_context* context = nullptr;

    ~Loop()
    {
        if (!mainloop)
            return;
        pa_threaded_mainloop_stop(mainloop);
        if (context) {
            pa_context_disconnect(context);
            pa_context_unref(context);
        }
        pa_threaded_mainloop_free(mainloop);
    }
};

namespace {

constexpr int Steps = 100;

void release(pa_operation* operation)
{
    if (operation)
        pa_operation_unref(operation);
}

// Holds libpulse's lock while requests are made from the window's thread.
class Locked {
public:
    explicit Locked(pa_threaded_mainloop* mainloop)
        : m_mainloop(mainloop)
    {
        pa_threaded_mainloop_lock(m_mainloop);
    }
    ~Locked() { pa_threaded_mainloop_unlock(m_mainloop); }
    Locked(const Locked&) = delete;
    Locked& operator=(const Locked&) = delete;

private:
    pa_threaded_mainloop* m_mainloop;
};

// A list being read, one device per callback, handed over once it is complete.
struct Collecting {
    SoundDevices* devices;
    bool output;
    std::vector<SoundDevice> found;
};

template <typename Info> SoundDevice deviceOf(const Info& info)
{
    return {QString::fromUtf8(info.name), QString::fromUtf8(info.description ? info.description : info.name),
        double(pa_cvolume_avg(&info.volume)) / PA_VOLUME_NORM, bool(info.mute), std::max<int>(info.volume.channels, 1)};
}

} // namespace

SoundDevices::SoundDevices(QObject* parent)
    : QObject(parent)
    , m_loop(std::make_unique<Loop>())
{
    m_loop->mainloop = pa_threaded_mainloop_new();
    if (!m_loop->mainloop)
        return;
    pa_proplist* properties = pa_proplist_new();
    pa_proplist_sets(properties, PA_PROP_APPLICATION_NAME, "Settings");
    pa_proplist_sets(properties, PA_PROP_APPLICATION_ID, "tde-daedalus");
    m_loop->context
        = pa_context_new_with_proplist(pa_threaded_mainloop_get_api(m_loop->mainloop), "Settings", properties);
    pa_proplist_free(properties);
    pa_context_set_state_callback(
        m_loop->context,
        [](pa_context* context, void* data) {
            auto* self = static_cast<SoundDevices*>(data);
            const pa_context_state_t state = pa_context_get_state(context);
            if (state == PA_CONTEXT_READY) {
                QMetaObject::invokeMethod(self, &SoundDevices::connected, Qt::QueuedConnection);
            } else if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
                QMetaObject::invokeMethod(
                    self,
                    [self] {
                        self->m_available = false;
                        emit self->changed();
                    },
                    Qt::QueuedConnection);
            }
        },
        this);
    pa_context_connect(m_loop->context, nullptr, PA_CONTEXT_NOFAIL, nullptr);
    pa_threaded_mainloop_start(m_loop->mainloop);
}

SoundDevices::~SoundDevices() = default;

void SoundDevices::connected()
{
    m_available = true;
    Locked lock(m_loop->mainloop);
    pa_context_set_subscribe_callback(
        m_loop->context,
        [](pa_context*, pa_subscription_event_type_t, uint32_t, void* data) {
            QMetaObject::invokeMethod(static_cast<SoundDevices*>(data), &SoundDevices::refresh, Qt::QueuedConnection);
        },
        this);
    release(pa_context_subscribe(m_loop->context,
        pa_subscription_mask_t(PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE | PA_SUBSCRIPTION_MASK_SERVER),
        nullptr, nullptr));
    query();
}

void SoundDevices::refresh()
{
    Locked lock(m_loop->mainloop);
    if (pa_context_get_state(m_loop->context) == PA_CONTEXT_READY)
        query();
}

// Asks for the defaults and both lists; the lock is held.
void SoundDevices::query()
{
    release(pa_context_get_server_info(
        m_loop->context,
        [](pa_context*, const pa_server_info* info, void* data) {
            if (!info)
                return;
            auto* self = static_cast<SoundDevices*>(data);
            const QString output = QString::fromUtf8(info->default_sink_name ? info->default_sink_name : "");
            const QString input = QString::fromUtf8(info->default_source_name ? info->default_source_name : "");
            QMetaObject::invokeMethod(self, [=] { self->receivedDefaults(output, input); }, Qt::QueuedConnection);
        },
        this));
    release(pa_context_get_sink_info_list(
        m_loop->context,
        [](pa_context*, const pa_sink_info* info, int last, void* data) {
            auto* collecting = static_cast<Collecting*>(data);
            if (!last && info) {
                collecting->found.push_back(deviceOf(*info));
                return;
            }
            QMetaObject::invokeMethod(
                collecting->devices,
                [devices = collecting->devices, found = std::move(collecting->found)]() mutable {
                    devices->received(true, std::move(found));
                },
                Qt::QueuedConnection);
            delete collecting;
        },
        new Collecting {this, true, {}}));
    release(pa_context_get_source_info_list(
        m_loop->context,
        [](pa_context*, const pa_source_info* info, int last, void* data) {
            auto* collecting = static_cast<Collecting*>(data);
            if (!last && info) {
                // What an output plays can be recorded too; those are no inputs to pick.
                if (info->monitor_of_sink == PA_INVALID_INDEX)
                    collecting->found.push_back(deviceOf(*info));
                return;
            }
            QMetaObject::invokeMethod(
                collecting->devices,
                [devices = collecting->devices, found = std::move(collecting->found)]() mutable {
                    devices->received(false, std::move(found));
                },
                Qt::QueuedConnection);
            delete collecting;
        },
        new Collecting {this, false, {}}));
}

void SoundDevices::received(bool output, std::vector<SoundDevice> devices)
{
    auto& list = output ? m_outputs : m_inputs;
    if (devices == list)
        return;
    list = std::move(devices);
    emit changed();
}

void SoundDevices::receivedDefaults(const QString& output, const QString& input)
{
    if (output == m_defaultOutput && input == m_defaultInput)
        return;
    m_defaultOutput = output;
    m_defaultInput = input;
    emit changed();
}

void SoundDevices::setDefault(bool output, const QString& name)
{
    const QByteArray device = name.toUtf8();
    Locked lock(m_loop->mainloop);
    release(output ? pa_context_set_default_sink(m_loop->context, device.constData(), nullptr, nullptr)
                   : pa_context_set_default_source(m_loop->context, device.constData(), nullptr, nullptr));
}

void SoundDevices::setVolume(bool output, const SoundDevice& device, double volume)
{
    pa_cvolume levels;
    pa_cvolume_set(
        &levels, uint8_t(device.channels), pa_volume_t(std::lround(std::clamp(volume, 0.0, 1.0) * PA_VOLUME_NORM)));
    const QByteArray name = device.name.toUtf8();
    Locked lock(m_loop->mainloop);
    release(output
            ? pa_context_set_sink_volume_by_name(m_loop->context, name.constData(), &levels, nullptr, nullptr)
            : pa_context_set_source_volume_by_name(m_loop->context, name.constData(), &levels, nullptr, nullptr));
}

void SoundDevices::setMuted(bool output, const QString& name, bool muted)
{
    const QByteArray device = name.toUtf8();
    Locked lock(m_loop->mainloop);
    release(output ? pa_context_set_sink_mute_by_name(m_loop->context, device.constData(), muted, nullptr, nullptr)
                   : pa_context_set_source_mute_by_name(m_loop->context, device.constData(), muted, nullptr, nullptr));
}

// SoundPage -------------------------------------------------------------------------------

SoundPage::SoundPage(QWidget* parent)
    : Page(parent)
{
    m_none = addGroup();
    m_none->addRow(u"No Sound Server"_s, u"PipeWire or PulseAudio does not run"_s);
    m_output = addGroup(u"Output"_s);
    m_outputControls = addDevice(m_output, true);
    m_input = addGroup(u"Input"_s);
    m_inputControls = addDevice(m_input, false);
    connect(&m_devices, &SoundDevices::changed, this, &SoundPage::sync);
    sync();
}

SoundPage::Controls SoundPage::addDevice(Group* group, bool output)
{
    Controls controls;
    controls.device = new QComboBox(group);
    controls.device->setMinimumWidth(260);
    controls.device->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    connect(controls.device, &QComboBox::activated, this,
        [this, combo = controls.device, output] { m_devices.setDefault(output, combo->currentData().toString()); });
    group->addRow(output ? u"Output Device"_s : u"Input Device"_s,
        output ? u"Where sound plays"_s : u"What records sound, such as a microphone"_s, controls.device);

    auto* level = new QWidget(group);
    auto* row = new QHBoxLayout(level);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    controls.mute = new QToolButton(level);
    controls.mute->setCheckable(true);
    controls.mute->setAutoRaise(true);
    controls.mute->setToolTip(u"Mute"_s);
    controls.volume = new QSlider(Qt::Horizontal, level);
    controls.volume->setRange(0, Steps);
    controls.volume->setMinimumWidth(220);
    row->addWidget(controls.mute);
    row->addWidget(controls.volume, 1);
    connect(controls.volume, &QSlider::valueChanged, this, [this, output, combo = controls.device](int value) {
        const auto& list = output ? m_devices.outputs() : m_devices.inputs();
        const auto device = std::ranges::find(list, combo->currentData().toString(), &SoundDevice::name);
        if (device != list.end())
            m_devices.setVolume(output, *device, double(value) / Steps);
    });
    connect(controls.mute, &QToolButton::toggled, this, [this, output, combo = controls.device](bool muted) {
        m_devices.setMuted(output, combo->currentData().toString(), muted);
    });
    group->addRow(u"Volume"_s, {}, level);
    return controls;
}

void SoundPage::sync()
{
    const bool available = m_devices.isAvailable();
    m_none->setVisible(!available);
    m_output->setVisible(available && !m_devices.outputs().empty());
    m_input->setVisible(available && !m_devices.inputs().empty());
    syncDevice(m_outputControls, true);
    syncDevice(m_inputControls, false);
}

void SoundPage::syncDevice(const Controls& controls, bool output)
{
    // What is shown is what the server says; none of it is sent back.
    const auto& list = output ? m_devices.outputs() : m_devices.inputs();
    const QString current = output ? m_devices.defaultOutput() : m_devices.defaultInput();
    {
        const QSignalBlocker blocker(controls.device);
        controls.device->clear();
        for (const SoundDevice& device : list)
            controls.device->addItem(device.description, device.name);
        controls.device->setCurrentIndex(std::max(0, controls.device->findData(current)));
    }
    const auto device = std::ranges::find(list, controls.device->currentData().toString(), &SoundDevice::name);
    if (device == list.end())
        return;
    // Not while it is being dragged, which would fight the hand.
    if (!controls.volume->isSliderDown()) {
        const QSignalBlocker blocker(controls.volume);
        controls.volume->setValue(int(std::lround(device->volume * Steps)));
    }
    const QSignalBlocker blocker(controls.mute);
    controls.mute->setChecked(device->muted);
    const QString icon = output ? (device->muted ? u"audio-volume-muted"_s : u"audio-volume-high"_s)
                                : (device->muted ? u"microphone-sensitivity-muted"_s : u"audio-input-microphone"_s);
    controls.mute->setIcon(tde::theme::symbolicIcon(icon));
}

} // namespace daedalus
