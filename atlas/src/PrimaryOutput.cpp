#include "PrimaryOutput.hpp"

#include <xcb/randr.h>
#include <xcb/xfixes.h>

#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>

namespace atlas {
namespace {

// A reply of xcb's, freed along with its owner.
template <typename T> using Reply = std::unique_ptr<T, decltype(&std::free)>;
template <typename T> Reply<T> reply(T* value)
{
    return Reply<T>(value, &std::free);
}

} // namespace

PrimaryOutput::~PrimaryOutput()
{
    disconnect();
}

void PrimaryOutput::connect(wl_event_loop* loop, const char* display)
{
    disconnect();
    m_connection = xcb_connect(display, nullptr);
    if (xcb_connection_has_error(m_connection)) {
        wlr_log(WLR_ERROR, "cannot reach Xwayland to name the primary output");
        disconnect();
        return;
    }
    const xcb_query_extension_reply_t* randr = xcb_get_extension_data(m_connection, &xcb_randr_id);
    if (!randr || !randr->present) {
        disconnect();
        return;
    }
    m_randrEvents = randr->first_event;
    // Xwayland goes once the programs that use it have, which this is not one of, as with the
    // window manager wlroots runs in it.
    reply(xcb_randr_query_version_reply(m_connection, xcb_randr_query_version(m_connection, 1, 3), nullptr));
    reply(xcb_xfixes_query_version_reply(m_connection, xcb_xfixes_query_version(m_connection, 6, 0), nullptr));
    xcb_xfixes_set_client_disconnect_mode(m_connection, XCB_XFIXES_CLIENT_DISCONNECT_FLAGS_TERMINATE);

    // Outputs come and go in Xwayland a little after they do here; each time, the primary one
    // is named again.
    const xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(m_connection)).data;
    xcb_randr_select_input(m_connection, screen->root,
        XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE | XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE);
    xcb_flush(m_connection);
    m_source.reset(wl_event_loop_add_fd(
        loop, xcb_get_file_descriptor(m_connection), WL_EVENT_READABLE,
        [](int, uint32_t mask, void* data) {
            static_cast<PrimaryOutput*>(data)->read(mask);
            return 0;
        },
        this));
    apply();
}

void PrimaryOutput::set(std::string name)
{
    if (name == m_name)
        return;
    m_name = std::move(name);
    apply();
}

void PrimaryOutput::read(uint32_t mask)
{
    if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
        disconnect();
        return;
    }
    // Events that come in while waiting for the replies of apply() wait in xcb's queue, where
    // the loop does not see them, so they are taken from there until none is left.
    for (;;) {
        bool changed = false;
        while (xcb_generic_event_t* event = xcb_poll_for_event(m_connection)) {
            const uint8_t type = event->response_type & 0x7f;
            changed |= type == m_randrEvents + XCB_RANDR_SCREEN_CHANGE_NOTIFY || type == m_randrEvents + XCB_RANDR_NOTIFY;
            std::free(event);
        }
        if (xcb_connection_has_error(m_connection)) {
            disconnect();
            return;
        }
        if (!changed)
            return;
        apply();
    }
}

void PrimaryOutput::disconnect()
{
    m_source.reset();
    if (m_connection)
        xcb_disconnect(std::exchange(m_connection, nullptr));
}

void PrimaryOutput::apply()
{
    if (!m_connection || m_name.empty())
        return;
    const xcb_window_t root = xcb_setup_roots_iterator(xcb_get_setup(m_connection)).data->root;
    const auto resources = reply(xcb_randr_get_screen_resources_current_reply(
        m_connection, xcb_randr_get_screen_resources_current(m_connection, root), nullptr));
    if (!resources)
        return;
    const xcb_randr_output_t* outputs = xcb_randr_get_screen_resources_current_outputs(resources.get());
    const int count = xcb_randr_get_screen_resources_current_outputs_length(resources.get());
    for (int i = 0; i < count; ++i) {
        const auto info = reply(xcb_randr_get_output_info_reply(m_connection,
            xcb_randr_get_output_info(m_connection, outputs[i], resources->config_timestamp), nullptr));
        if (!info)
            continue;
        const std::string_view name(reinterpret_cast<const char*>(xcb_randr_get_output_info_name(info.get())),
            xcb_randr_get_output_info_name_length(info.get()));
        if (name != m_name)
            continue;
        // Naming it again would only tell everyone of a change that is none.
        const auto primary = reply(
            xcb_randr_get_output_primary_reply(m_connection, xcb_randr_get_output_primary(m_connection, root), nullptr));
        if (!primary || primary->output != outputs[i]) {
            xcb_randr_set_output_primary(m_connection, root, outputs[i]);
            xcb_flush(m_connection);
        }
        return;
    }
}

} // namespace atlas
