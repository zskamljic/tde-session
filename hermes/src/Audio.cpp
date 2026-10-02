#include "Audio.hpp"

#include <pulse/pulseaudio.h>

#include <QMetaObject>

#include <algorithm>
#include <cmath>

namespace hermes {

struct Audio::Loop {
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

// Requests that could not be made return nothing to let go of.
void release(pa_operation* operation)
{
    if (operation)
        pa_operation_unref(operation);
}

// Holds libpulse's lock while requests are made from the bar's thread.
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

} // namespace

Audio::Audio(QObject* parent)
    : QObject(parent)
    , m_loop(std::make_unique<Loop>())
{
    m_loop->mainloop = pa_threaded_mainloop_new();
    if (!m_loop->mainloop)
        return;
    pa_proplist* properties = pa_proplist_new();
    pa_proplist_sets(properties, PA_PROP_APPLICATION_NAME, "Hermes");
    pa_proplist_sets(properties, PA_PROP_APPLICATION_ID, "tde-hermes");
    m_loop->context
        = pa_context_new_with_proplist(pa_threaded_mainloop_get_api(m_loop->mainloop), "Hermes", properties);
    pa_proplist_free(properties);

    pa_context_set_state_callback(
        m_loop->context,
        [](pa_context* context, void* data) {
            auto* self = static_cast<Audio*>(data);
            switch (pa_context_get_state(context)) {
            case PA_CONTEXT_READY:
                QMetaObject::invokeMethod(self, &Audio::connected, Qt::QueuedConnection);
                break;
            case PA_CONTEXT_FAILED:
            case PA_CONTEXT_TERMINATED:
                QMetaObject::invokeMethod(
                    self, [self] { self->update(false, 0, false, {}, {}, 2); }, Qt::QueuedConnection);
                break;
            default:
                break;
            }
        },
        this);
    // The sound server may start after the bar, or come back after a restart.
    pa_context_connect(m_loop->context, nullptr, PA_CONTEXT_NOFAIL, nullptr);
    pa_threaded_mainloop_start(m_loop->mainloop);
}

Audio::~Audio() = default;

void Audio::connected()
{
    Locked lock(m_loop->mainloop);
    // Told of every change to outputs and to which is the default.
    pa_context_set_subscribe_callback(
        m_loop->context,
        [](pa_context*, pa_subscription_event_type_t, uint32_t, void* data) {
            QMetaObject::invokeMethod(static_cast<Audio*>(data), &Audio::refresh, Qt::QueuedConnection);
        },
        this);
    release(pa_context_subscribe(m_loop->context,
        pa_subscription_mask_t(PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER), nullptr, nullptr));
    query();
}

void Audio::refresh()
{
    if (!m_loop->context)
        return;
    Locked lock(m_loop->mainloop);
    if (pa_context_get_state(m_loop->context) == PA_CONTEXT_READY)
        query();
}

// Asks for the default output and how loud it is; the lock is held.
void Audio::query()
{
    release(pa_context_get_server_info(
        m_loop->context,
        [](pa_context* context, const pa_server_info* info, void* data) {
            if (!info || !info->default_sink_name)
                return;
            release(pa_context_get_sink_info_by_name(
                context, info->default_sink_name,
                [](pa_context*, const pa_sink_info* sink, int last, void* data) {
                    if (last || !sink)
                        return;
                    auto* self = static_cast<Audio*>(data);
                    const double volume = double(pa_cvolume_avg(&sink->volume)) / PA_VOLUME_NORM;
                    const bool muted = sink->mute;
                    const QString output = QString::fromUtf8(sink->description ? sink->description : sink->name);
                    const QString name = QString::fromUtf8(sink->name);
                    const int channels = sink->volume.channels;
                    QMetaObject::invokeMethod(
                        self, [=] { self->update(true, volume, muted, output, name, channels); }, Qt::QueuedConnection);
                },
                data));
        },
        this));
}

void Audio::update(bool available, double volume, bool muted, const QString& output, const QString& sink, int channels)
{
    if (available == m_available && std::abs(volume - m_volume) < 0.001 && muted == m_muted && output == m_output
        && sink == m_sink)
        return;
    m_available = available;
    m_volume = volume;
    m_muted = muted;
    m_output = output;
    m_sink = sink;
    m_channels = std::max(channels, 1);
    emit changed();
}

void Audio::setVolume(double volume)
{
    if (!m_available || m_sink.isEmpty())
        return;
    volume = std::clamp(volume, 0.0, 1.0);
    pa_cvolume levels;
    pa_cvolume_set(&levels, uint8_t(m_channels), pa_volume_t(std::lround(volume * PA_VOLUME_NORM)));
    const QByteArray sink = m_sink.toUtf8();
    {
        Locked lock(m_loop->mainloop);
        release(pa_context_set_sink_volume_by_name(m_loop->context, sink.constData(), &levels, nullptr, nullptr));
        // Turning it up unmutes it, as a slider is expected to.
        if (m_muted && volume > 0)
            release(pa_context_set_sink_mute_by_name(m_loop->context, sink.constData(), 0, nullptr, nullptr));
    }
}

void Audio::setMuted(bool muted)
{
    if (!m_available || m_sink.isEmpty())
        return;
    const QByteArray sink = m_sink.toUtf8();
    Locked lock(m_loop->mainloop);
    release(pa_context_set_sink_mute_by_name(m_loop->context, sink.constData(), muted, nullptr, nullptr));
}

} // namespace hermes
