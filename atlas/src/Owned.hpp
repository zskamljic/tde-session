#pragma once

#include "wlr.hpp"

#include <unistd.h>

#include <memory>
#include <utility>

// What the compositor holds of its event loop, of wlroots and of the system, let go of along
// with its owner.
namespace atlas {

struct EventSourceRemover {
    void operator()(wl_event_source* source) const { wl_event_source_remove(source); }
};
// A source of the event loop, removed from it along with its owner.
using EventSource = std::unique_ptr<wl_event_source, EventSourceRemover>;

// A timer calling `Method` of `object` when it runs out, as wl_event_source_timer_update() sets it.
template <auto Method, typename T> EventSource addTimer(wl_event_loop* loop, T* object)
{
    return EventSource(wl_event_loop_add_timer(
        loop,
        [](void* data) {
            (static_cast<T*>(data)->*Method)();
            return 0;
        },
        object));
}

// Calls `Method` of `object` whenever `fd` has something to read, which it reads itself.
template <auto Method, typename T> EventSource addReader(wl_event_loop* loop, int fd, T* object)
{
    return EventSource(wl_event_loop_add_fd(
        loop, fd, WL_EVENT_READABLE,
        [](int, uint32_t, void* data) {
            (static_cast<T*>(data)->*Method)();
            return 0;
        },
        object));
}

struct BufferUnlocker {
    void operator()(wlr_buffer* buffer) const { wlr_buffer_unlock(buffer); }
};
// A buffer held from being reused or freed, until its owner lets go.
using LockedBuffer = std::unique_ptr<wlr_buffer, BufferUnlocker>;

struct SwapchainDestroyer {
    void operator()(wlr_swapchain* swapchain) const { wlr_swapchain_destroy(swapchain); }
};
using Swapchain = std::unique_ptr<wlr_swapchain, SwapchainDestroyer>;

struct TextureDestroyer {
    void operator()(wlr_texture* texture) const { wlr_texture_destroy(texture); }
};
using Texture = std::unique_ptr<wlr_texture, TextureDestroyer>;

// A file descriptor, closed along with its owner.
class FileDescriptor {
public:
    FileDescriptor() = default;
    explicit FileDescriptor(int fd)
        : m_fd(fd)
    {
    }
    ~FileDescriptor() { reset(); }

    FileDescriptor(FileDescriptor&& other) noexcept
        : m_fd(std::exchange(other.m_fd, -1))
    {
    }
    FileDescriptor& operator=(FileDescriptor&& other) noexcept
    {
        if (this != &other) {
            reset();
            m_fd = std::exchange(other.m_fd, -1);
        }
        return *this;
    }

    int get() const { return m_fd; }
    explicit operator bool() const { return m_fd >= 0; }

    void reset()
    {
        if (m_fd >= 0)
            close(m_fd);
        m_fd = -1;
    }

private:
    int m_fd = -1;
};

} // namespace atlas
