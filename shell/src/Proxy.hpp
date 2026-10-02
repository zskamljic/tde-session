#pragma once

#include <memory>

namespace shell {

// Owns a Wayland object and destroys it with the request its protocol provides. The
// protocols' functions are static inline, so the deleters are spelled out per type with
// SHELL_PROXY, next to where the protocol's header is included.
template <typename T> struct ProxyDeleter;
template <typename T> using Proxy = std::unique_ptr<T, ProxyDeleter<T>>;

} // namespace shell

#define SHELL_PROXY(type, destroy)                                                                                     \
    template <> struct shell::ProxyDeleter<type> {                                                                     \
        void operator()(type* proxy) const { destroy(proxy); }                                                         \
    }
