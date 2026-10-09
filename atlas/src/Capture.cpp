#include "Capture.hpp"

#include "Parts.hpp"
#include "Server.hpp"
#include "View.hpp"

#include <drm_fourcc.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <vector>

namespace atlas {
namespace {

// A frame for the copies to take, with the buffer it is in.
struct FrameEvent {
    wlr_ext_image_capture_source_v1_frame_event base;
    wlr_buffer* buffer;
};

struct Part {
    wlr_scene_buffer* buffer;
    int x;
    int y;
};

std::vector<Part> partsOf(wlr_scene_node* node)
{
    std::vector<Part> parts;
    wlr_scene_node_for_each_buffer(
        node,
        [](wlr_scene_buffer* buffer, int x, int y, void* data) {
            if (buffer->buffer)
                static_cast<std::vector<Part>*>(data)->push_back({buffer, x, y});
        },
        &parts);
    return parts;
}

// The size a part is drawn at, in layout coordinates.
wlr_box boxOf(const Part& part)
{
    int width = part.buffer->dst_width;
    int height = part.buffer->dst_height;
    if (width <= 0 || height <= 0) {
        width = part.buffer->buffer->width;
        height = part.buffer->buffer->height;
        if (part.buffer->transform & WL_OUTPUT_TRANSFORM_90)
            std::swap(width, height);
    }
    return {part.x, part.y, width, height};
}

const wlr_ext_image_capture_source_v1_interface SourceImpl {
    .start = [](wlr_ext_image_capture_source_v1* source, bool) { WindowCapture::fromSource(source).start(); },
    .stop = nullptr,
    .request_frame =
        [](wlr_ext_image_capture_source_v1* source, bool) { WindowCapture::fromSource(source).requestFrame(); },
    .copy_frame =
        [](wlr_ext_image_capture_source_v1* source, wlr_ext_image_copy_capture_frame_v1* frame,
            wlr_ext_image_capture_source_v1_frame_event* event) {
            WindowCapture::fromSource(source).copy(frame, reinterpret_cast<FrameEvent*>(event)->buffer);
        },
    .get_pointer_cursor = nullptr,
};

} // namespace

WindowCapture::WindowCapture(View& view)
    : m_view(view)
{
    m_source.owner = this;
    wlr_ext_image_capture_source_v1_init(&m_source.base, &SourceImpl);
}

WindowCapture::~WindowCapture()
{
    // Copies still going are told it is gone.
    wlr_ext_image_capture_source_v1_finish(&m_source.base);
    if (m_swapchain)
        wlr_swapchain_destroy(m_swapchain);
}

WindowCapture& WindowCapture::fromSource(wlr_ext_image_capture_source_v1* source)
{
    return *reinterpret_cast<Source*>(source)->owner;
}

wlr_box WindowCapture::extents() const
{
    int left = INT_MAX;
    int top = INT_MAX;
    int right = INT_MIN;
    int bottom = INT_MIN;
    for (const Part& part : partsOf(m_view.captureNode())) {
        const wlr_box box = boxOf(part);
        left = std::min(left, box.x);
        top = std::min(top, box.y);
        right = std::max(right, box.x + box.width);
        bottom = std::max(bottom, box.y + box.height);
    }
    if (left >= right || top >= bottom)
        return {};
    return {left, top, right - left, bottom - top};
}

double WindowCapture::scale() const
{
    const wlr_box frame = m_view.geometry();
    const Output* output = m_view.server.outputAt(frame.x + frame.width / 2.0, frame.y + frame.height / 2.0);
    return output ? output->output->scale : 1.0;
}

void WindowCapture::start()
{
    // A copy starting wants a frame at once, whatever the others had.
    m_damaged = true;
    m_extents = extents();
    m_scale = scale();
    const int width = int(std::ceil(m_extents.width * m_scale));
    const int height = int(std::ceil(m_extents.height * m_scale));
    if (width <= 0 || height <= 0)
        return;
    if (m_swapchain && m_swapchain->width == width && m_swapchain->height == height)
        return;

    Server& server = m_view.server;
    // With alpha, and the modifier left to the allocator.
    wlr_drm_format_set formats {};
    wlr_drm_format_set_add(&formats, DRM_FORMAT_ARGB8888, DRM_FORMAT_MOD_INVALID);
    wlr_swapchain* swapchain
        = wlr_swapchain_create(server.allocator, width, height, wlr_drm_format_set_get(&formats, DRM_FORMAT_ARGB8888));
    wlr_drm_format_set_finish(&formats);
    if (!swapchain)
        return;
    if (m_swapchain)
        wlr_swapchain_destroy(m_swapchain);
    m_swapchain = swapchain;
    // Tells the copies the size and formats it takes.
    wlr_ext_image_capture_source_v1_set_constraints_from_swapchain(&m_source.base, m_swapchain, server.renderer);
}

void WindowCapture::requestFrame()
{
    // Copies taken one after the other, as for a live picture, wait for something new.
    if (!m_damaged) {
        m_waiting = true;
        return;
    }
    frame();
}

void WindowCapture::damage()
{
    m_damaged = true;
    if (m_waiting)
        frame();
}

void WindowCapture::frame()
{
    m_damaged = false;
    m_waiting = false;
    // The window may have changed size since; the copies then take the new one.
    start();
    wlr_buffer* buffer = render();
    if (!buffer)
        return;
    pixman_region32_t damage;
    pixman_region32_init_rect(&damage, 0, 0, buffer->width, buffer->height);
    FrameEvent event {.base = {.damage = &damage}, .buffer = buffer};
    wl_signal_emit_mutable(&m_source.base.events.frame, &event.base);
    pixman_region32_fini(&damage);
    wlr_buffer_unlock(buffer);
}

wlr_buffer* WindowCapture::snapshot(wlr_box& extents)
{
    start();
    extents = m_extents;
    return render();
}

void WindowCapture::copy(wlr_ext_image_copy_capture_frame_v1* frame, wlr_buffer* buffer)
{
    if (!wlr_ext_image_copy_capture_frame_v1_copy_buffer(frame, buffer, m_view.server.renderer))
        return;
    timespec now {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_ext_image_copy_capture_frame_v1_ready(frame, WL_OUTPUT_TRANSFORM_NORMAL, &now);
}

wlr_buffer* WindowCapture::render()
{
    if (!m_swapchain)
        return nullptr;
    wlr_renderer* renderer = m_view.server.renderer;
    wlr_buffer* buffer = wlr_swapchain_acquire(m_swapchain);
    if (!buffer)
        return nullptr;
    wlr_render_pass* pass = wlr_renderer_begin_buffer_pass(renderer, buffer, nullptr);
    if (!pass) {
        wlr_buffer_unlock(buffer);
        return nullptr;
    }

    // Nothing first, then the parts from the bottom up.
    wlr_render_rect_options clear {};
    clear.box = {0, 0, buffer->width, buffer->height};
    clear.color = {0, 0, 0, 0};
    clear.blend_mode = WLR_RENDER_BLEND_MODE_NONE;
    wlr_render_pass_add_rect(pass, &clear);
    std::vector<wlr_texture*> textures; // made here, to destroy after drawing
    for (const Part& part : partsOf(m_view.captureNode())) {
        // A program's buffer the scene made a texture of already: that one. Those in shared
        // memory are let go of once copied into it, so no other could be made.
        wlr_texture* texture = nullptr;
        if (wlr_client_buffer* client = wlr_client_buffer_get(part.buffer->buffer);
            client && client->texture && client->texture->renderer == renderer) {
            texture = client->texture;
        } else {
            texture = wlr_texture_from_buffer(renderer, part.buffer->buffer);
            if (texture)
                textures.push_back(texture);
        }
        if (!texture)
            continue;
        const wlr_box box = boxOf(part);
        const wlr_box place {
            int(std::round((box.x - m_extents.x) * m_scale)),
            int(std::round((box.y - m_extents.y) * m_scale)),
            int(std::round(box.width * m_scale)),
            int(std::round(box.height * m_scale)),
        };
        const float alpha = part.buffer->opacity;
        wlr_render_texture_options options {};
        options.texture = texture;
        options.src_box = part.buffer->src_box;
        options.dst_box = place;
        options.alpha = &alpha;
        options.transform = part.buffer->transform;
        options.filter_mode = part.buffer->filter_mode;
        options.blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED;
        wlr_render_pass_add_texture(pass, &options);
    }
    const bool drawn = wlr_render_pass_submit(pass);
    for (wlr_texture* texture : textures)
        wlr_texture_destroy(texture);
    if (!drawn) {
        wlr_buffer_unlock(buffer);
        return nullptr;
    }
    return buffer;
}

} // namespace atlas
