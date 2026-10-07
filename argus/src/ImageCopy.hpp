#pragma once

#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"

#include <Proxy.hpp>

#include <QImage>
#include <QSize>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

class QScreen;

SHELL_PROXY(wl_shm, wl_shm_destroy);
SHELL_PROXY(wl_shm_pool, wl_shm_pool_destroy);
SHELL_PROXY(wl_buffer, wl_buffer_destroy);
SHELL_PROXY(ext_image_capture_source_v1, ext_image_capture_source_v1_destroy);
SHELL_PROXY(ext_output_image_capture_source_manager_v1, ext_output_image_capture_source_manager_v1_destroy);
SHELL_PROXY(ext_image_copy_capture_manager_v1, ext_image_copy_capture_manager_v1_destroy);
SHELL_PROXY(ext_image_copy_capture_session_v1, ext_image_copy_capture_session_v1_destroy);
SHELL_PROXY(ext_image_copy_capture_frame_v1, ext_image_copy_capture_frame_v1_destroy);

namespace argus {

using shell::Proxy;

class SharedMemory;

// One copy of what a window or a screen shows, through ext-image-copy-capture. The session
// first says which buffers it takes, then a frame is copied into one. Either way `done` is
// called once, with the picture or a null one when it failed, as the last thing the copy does,
// so it may destroy the copy.
class ImageCopy {
public:
    ImageCopy(wl_shm* shm, ext_image_copy_capture_manager_v1* copier, ext_image_capture_source_v1* source,
        std::function<void(QImage)> done);
    ~ImageCopy();

    ImageCopy(const ImageCopy&) = delete;
    ImageCopy& operator=(const ImageCopy&) = delete;

private:
    void copy();
    void ready();
    void finish(QImage image);

    wl_shm* m_shm;
    std::function<void(QImage)> m_done;
    Proxy<ext_image_capture_source_v1> m_source;
    Proxy<ext_image_copy_capture_session_v1> m_session;
    Proxy<ext_image_copy_capture_frame_v1> m_frame;
    Proxy<wl_buffer> m_buffer;
    std::unique_ptr<SharedMemory> m_memory;
    QSize m_size;
    std::optional<uint32_t> m_format;
};

// Copies what the screens show.
class ScreenCapture {
public:
    ScreenCapture();
    ~ScreenCapture();

    ScreenCapture(const ScreenCapture&) = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;

    // False when the compositor does not let programs copy screens.
    bool isSupported() const { return m_shm && m_copier && m_sources; }

    // Copies what `screen` shows, in its own pixels; `done` as for ImageCopy. Null when screens
    // cannot be copied.
    std::unique_ptr<ImageCopy> capture(QScreen* screen, std::function<void(QImage)> done);

private:
    static void global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version);

    Proxy<wl_registry> m_registry;
    Proxy<wl_shm> m_shm;
    Proxy<ext_image_copy_capture_manager_v1> m_copier;
    Proxy<ext_output_image_capture_source_manager_v1> m_sources;
};

// Sends what was asked of the compositor on its way.
void flushRequests();

} // namespace argus
