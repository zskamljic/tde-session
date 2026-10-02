#pragma once

#include <wayland-server-core.h>

#include <functional>
#include <type_traits>

namespace atlas {

// A wl_listener that calls a function, and leaves its signal when destroyed.
class Listener {
public:
    Listener()
    {
        wl_list_init(&m_hook.listener.link);
        m_hook.listener.notify = &Listener::notify;
        m_hook.owner = this;
    }
    ~Listener() { disconnect(); }

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    // The callback takes the signal's data as a Data*, or nothing.
    template <typename Data = void, typename Callback> void connect(wl_signal& signal, Callback callback)
    {
        disconnect();
        m_callback = [callback = std::move(callback)](void* data) {
            if constexpr (std::is_invocable_v<Callback, Data*>)
                callback(static_cast<Data*>(data));
            else
                callback();
        };
        wl_signal_add(&signal, &m_hook.listener);
    }

    void disconnect()
    {
        wl_list_remove(&m_hook.listener.link);
        wl_list_init(&m_hook.listener.link);
    }

private:
    // wl_listener first, so the pointer libwayland hands back finds its way here.
    struct Hook {
        wl_listener listener;
        Listener* owner;
    };

    static void notify(wl_listener* listener, void* data)
    {
        reinterpret_cast<Hook*>(listener)->owner->m_callback(data);
    }

    Hook m_hook {};
    std::function<void(void*)> m_callback;
};

} // namespace atlas
