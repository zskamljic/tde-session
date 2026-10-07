#pragma once

#include "ext-data-control-v1-client-protocol.h"

#include <Proxy.hpp>

#include <QByteArray>
#include <QString>

SHELL_PROXY(ext_data_control_manager_v1, ext_data_control_manager_v1_destroy);
SHELL_PROXY(ext_data_control_device_v1, ext_data_control_device_v1_destroy);
SHELL_PROXY(ext_data_control_source_v1, ext_data_control_source_v1_destroy);

namespace argus {

using shell::Proxy;

// The clipboard through ext-data-control, which, unlike Qt's, takes what it is given without a
// window of this program having the keyboard, as a screenshot taken with a key has none.
class Clipboard {
public:
    Clipboard();
    ~Clipboard();

    Clipboard(const Clipboard&) = delete;
    Clipboard& operator=(const Clipboard&) = delete;

    bool isSupported() const { return m_device != nullptr; }

    // Offers `data` of `mimeType` until something else is copied.
    void set(const QString& mimeType, const QByteArray& data);

private:
    static void global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version);

    Proxy<wl_registry> m_registry;
    Proxy<ext_data_control_manager_v1> m_manager;
    Proxy<ext_data_control_device_v1> m_device;
    Proxy<ext_data_control_source_v1> m_source; // what is offered now
    QByteArray m_data; // what is offered
};

} // namespace argus
