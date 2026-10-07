#include "ImageCopy.hpp"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QScreen>

#include <algorithm>
#include <cstring>

#include <sys/mman.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcCapture, "tde.argus.capture")

namespace argus {

// Memory shared with the compositor, which copies a picture into it.
class SharedMemory {
public:
    explicit SharedMemory(size_t size)
        : m_size(size)
    {
        m_fd = memfd_create("tde-argus", MFD_CLOEXEC);
        if (m_fd < 0 || ftruncate(m_fd, static_cast<off_t>(size)) < 0)
            return;
        void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
        if (data != MAP_FAILED)
            m_data = data;
    }
    ~SharedMemory()
    {
        if (m_data)
            munmap(m_data, m_size);
        if (m_fd >= 0)
            ::close(m_fd);
    }
    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    bool isValid() const { return m_data != nullptr; }
    int fd() const { return m_fd; }
    const uchar* data() const { return static_cast<const uchar*>(m_data); }

private:
    size_t m_size = 0;
    int m_fd = -1;
    void* m_data = nullptr;
};

namespace {

// The QImage format with the same layout in memory, for the 32-bit formats the compositor
// may copy pictures in.
std::optional<QImage::Format> imageFormat(uint32_t format)
{
    switch (format) {
    case WL_SHM_FORMAT_XRGB8888:
        return QImage::Format_RGB32;
    case WL_SHM_FORMAT_ARGB8888:
        return QImage::Format_ARGB32_Premultiplied;
    case WL_SHM_FORMAT_XBGR8888:
        return QImage::Format_RGBX8888;
    case WL_SHM_FORMAT_ABGR8888:
        return QImage::Format_RGBA8888_Premultiplied;
    default:
        return std::nullopt;
    }
}

} // namespace

void flushRequests()
{
    if (auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>())
        wl_display_flush(wayland->display());
}

// ImageCopy -------------------------------------------------------------------------------

ImageCopy::ImageCopy(wl_shm* shm, ext_image_copy_capture_manager_v1* copier, ext_image_capture_source_v1* source,
    std::function<void(QImage)> done)
    : m_shm(shm)
    , m_done(std::move(done))
    , m_source(source)
    , m_session(ext_image_copy_capture_manager_v1_create_session(copier, source, 0))
{
    static const ext_image_copy_capture_session_v1_listener listener {
        .buffer_size = [](void* data, ext_image_copy_capture_session_v1*, uint32_t width,
                           uint32_t height) { static_cast<ImageCopy*>(data)->m_size = QSize(int(width), int(height)); },
        .shm_format =
            [](void* data, ext_image_copy_capture_session_v1*, uint32_t format) {
                auto* self = static_cast<ImageCopy*>(data);
                const auto image = imageFormat(format);
                // Formats with alpha keep rounded corners and the like see-through.
                if (image && (!self->m_format || QImage(1, 1, *image).hasAlphaChannel()))
                    self->m_format = format;
            },
        .dmabuf_device = [](void*, ext_image_copy_capture_session_v1*, wl_array*) { },
        .dmabuf_format = [](void*, ext_image_copy_capture_session_v1*, uint32_t, wl_array*) { },
        .done = [](void* data, ext_image_copy_capture_session_v1*) { static_cast<ImageCopy*>(data)->copy(); },
        .stopped = [](void* data, ext_image_copy_capture_session_v1*) { static_cast<ImageCopy*>(data)->finish({}); },
    };
    ext_image_copy_capture_session_v1_add_listener(m_session.get(), &listener, this);
    flushRequests();
}

ImageCopy::~ImageCopy() = default;

void ImageCopy::copy()
{
    if (m_frame)
        return; // the buffer constraints changed while copying; the copy still fits or fails
    if (!m_format || m_size.isEmpty()) {
        finish({});
        return;
    }
    const int stride = m_size.width() * 4;
    m_memory = std::make_unique<SharedMemory>(size_t(stride) * size_t(m_size.height()));
    if (!m_memory->isValid()) {
        finish({});
        return;
    }
    Proxy<wl_shm_pool> pool(wl_shm_create_pool(m_shm, m_memory->fd(), stride * m_size.height()));
    m_buffer.reset(wl_shm_pool_create_buffer(pool.get(), 0, m_size.width(), m_size.height(), stride, *m_format));

    static const ext_image_copy_capture_frame_v1_listener listener {
        .transform = [](void*, ext_image_copy_capture_frame_v1*, uint32_t) { },
        .damage = [](void*, ext_image_copy_capture_frame_v1*, int32_t, int32_t, int32_t, int32_t) { },
        .presentation_time = [](void*, ext_image_copy_capture_frame_v1*, uint32_t, uint32_t, uint32_t) { },
        .ready = [](void* data, ext_image_copy_capture_frame_v1*) { static_cast<ImageCopy*>(data)->ready(); },
        .failed =
            [](void* data, ext_image_copy_capture_frame_v1*, uint32_t reason) {
                qCDebug(lcCapture) << "capture failed, reason" << reason;
                static_cast<ImageCopy*>(data)->finish({});
            },
    };
    m_frame.reset(ext_image_copy_capture_session_v1_create_frame(m_session.get()));
    ext_image_copy_capture_frame_v1_add_listener(m_frame.get(), &listener, this);
    ext_image_copy_capture_frame_v1_attach_buffer(m_frame.get(), m_buffer.get());
    ext_image_copy_capture_frame_v1_damage_buffer(m_frame.get(), 0, 0, m_size.width(), m_size.height());
    ext_image_copy_capture_frame_v1_capture(m_frame.get());
    flushRequests();
}

void ImageCopy::ready()
{
    const QImage view(m_memory->data(), m_size.width(), m_size.height(), m_size.width() * 4, *imageFormat(*m_format));
    finish(view.copy());
}

void ImageCopy::finish(QImage image)
{
    // Held here, as calling it may destroy this copy.
    const auto done = std::move(m_done);
    if (done)
        done(std::move(image));
}

// ScreenCapture ---------------------------------------------------------------------------

ScreenCapture::ScreenCapture()
{
    auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wayland)
        return;
    static const wl_registry_listener listener {
        .global = global,
        .global_remove = [](void*, wl_registry*, uint32_t) { },
    };
    m_registry.reset(wl_display_get_registry(wayland->display()));
    wl_registry_add_listener(m_registry.get(), &listener, this);
    wl_display_roundtrip(wayland->display());
}

ScreenCapture::~ScreenCapture() = default;

void ScreenCapture::global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
{
    auto* self = static_cast<ScreenCapture*>(data);
    const auto bind = [&]<typename T>(const wl_interface& wanted) {
        return static_cast<T*>(wl_registry_bind(registry, name, &wanted, std::min(version, 1U)));
    };
    if (std::strcmp(interface, wl_shm_interface.name) == 0)
        self->m_shm.reset(bind.operator()<wl_shm>(wl_shm_interface));
    else if (std::strcmp(interface, ext_image_copy_capture_manager_v1_interface.name) == 0)
        self->m_copier.reset(
            bind.operator()<ext_image_copy_capture_manager_v1>(ext_image_copy_capture_manager_v1_interface));
    else if (std::strcmp(interface, ext_output_image_capture_source_manager_v1_interface.name) == 0)
        self->m_sources.reset(bind.operator()<ext_output_image_capture_source_manager_v1>(
            ext_output_image_capture_source_manager_v1_interface));
}

std::unique_ptr<ImageCopy> ScreenCapture::capture(QScreen* screen, std::function<void(QImage)> done)
{
    auto* wayland = screen ? screen->nativeInterface<QNativeInterface::QWaylandScreen>() : nullptr;
    if (!isSupported() || !wayland || !wayland->output())
        return nullptr;
    auto* source = ext_output_image_capture_source_manager_v1_create_source(m_sources.get(), wayland->output());
    return std::make_unique<ImageCopy>(m_shm.get(), m_copier.get(), source, std::move(done));
}

} // namespace argus
