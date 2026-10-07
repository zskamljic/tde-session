#include "Clipboard.hpp"

#include "ImageCopy.hpp"

#include <QGuiApplication>

#include <cstring>
#include <thread>

#include <unistd.h>

namespace argus {

Clipboard::Clipboard()
{
    auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wayland || !wayland->seat())
        return;
    static const wl_registry_listener listener {
        .global = global,
        .global_remove = [](void*, wl_registry*, uint32_t) { },
    };
    m_registry.reset(wl_display_get_registry(wayland->display()));
    wl_registry_add_listener(m_registry.get(), &listener, this);
    wl_display_roundtrip(wayland->display());
    if (!m_manager)
        return;

    // What others copy is announced too; it is not needed here.
    static const ext_data_control_device_v1_listener deviceListener {
        .data_offer = [](void*, ext_data_control_device_v1*,
                          ext_data_control_offer_v1* offer) { ext_data_control_offer_v1_destroy(offer); },
        .selection = [](void*, ext_data_control_device_v1*, ext_data_control_offer_v1*) { },
        .finished = [](void* data, ext_data_control_device_v1*) { static_cast<Clipboard*>(data)->m_device.reset(); },
        .primary_selection = [](void*, ext_data_control_device_v1*, ext_data_control_offer_v1*) { },
    };
    m_device.reset(ext_data_control_manager_v1_get_data_device(m_manager.get(), wayland->seat()));
    ext_data_control_device_v1_add_listener(m_device.get(), &deviceListener, this);
}

Clipboard::~Clipboard() = default;

void Clipboard::global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t)
{
    if (std::strcmp(interface, ext_data_control_manager_v1_interface.name) == 0) {
        static_cast<Clipboard*>(data)->m_manager.reset(static_cast<ext_data_control_manager_v1*>(
            wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1)));
    }
}

void Clipboard::set(const QString& mimeType, const QByteArray& data)
{
    if (!m_device)
        return;
    m_data = data;

    static const ext_data_control_source_v1_listener listener {
        .send =
            [](void* data, ext_data_control_source_v1*, const char*, int32_t fd) {
                // Written aside, as the one reading may take its time.
                std::thread([fd, bytes = static_cast<Clipboard*>(data)->m_data] {
                    for (qsizetype done = 0; done < bytes.size();) {
                        const ssize_t written = ::write(fd, bytes.constData() + done, size_t(bytes.size() - done));
                        if (written <= 0)
                            break;
                        done += written;
                    }
                    ::close(fd);
                }).detach();
            },
        .cancelled =
            [](void* data, ext_data_control_source_v1*) {
                auto* self = static_cast<Clipboard*>(data);
                self->m_source.reset();
                self->m_data.clear();
            },
    };
    m_source.reset(ext_data_control_manager_v1_create_data_source(m_manager.get()));
    ext_data_control_source_v1_add_listener(m_source.get(), &listener, this);
    ext_data_control_source_v1_offer(m_source.get(), mimeType.toUtf8().constData());
    ext_data_control_device_v1_set_selection(m_device.get(), m_source.get());
    flushRequests();
}

} // namespace argus
