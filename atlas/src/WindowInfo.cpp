#include "Server.hpp"
#include "View.hpp"

#include "tde-window-info-v1-protocol.h"

#include <algorithm>

namespace atlas {
namespace {

void getInfo(wl_client* client, wl_resource* manager, uint32_t id, wl_resource* toplevel)
{
    const auto& server = *static_cast<const Server*>(wl_resource_get_user_data(manager));
    wl_resource* info = wl_resource_create(client, &tde_window_info_v1_interface, wl_resource_get_version(manager), id);
    if (!info) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(info, nullptr, nullptr, nullptr);

    // A window that is gone, or no longer listed, answers with nothing.
    wlr_box box {};
    uint32_t recency = 0;
    uint32_t flags = 0;
    wlr_ext_foreign_toplevel_handle_v1* handle = wlr_ext_foreign_toplevel_handle_v1_from_resource(toplevel);
    if (const auto* view = handle ? static_cast<const View*>(handle->data) : nullptr) {
        box = view->geometry();
        const auto& order = server.stackingOrder();
        recency = uint32_t(std::ranges::find(order, view) - order.begin());
        if (view->minimized)
            flags |= TDE_WINDOW_INFO_V1_FLAGS_MINIMIZED;
    }
    // The answer is the object's last word.
    tde_window_info_v1_send_info(
        info, box.x, box.y, uint32_t(std::max(box.width, 0)), uint32_t(std::max(box.height, 0)), recency, flags);
    wl_resource_destroy(info);
}

void moveToOutput(wl_client*, wl_resource* manager, wl_resource* toplevel, wl_resource* output)
{
    auto& server = *static_cast<Server*>(wl_resource_get_user_data(manager));
    wlr_ext_foreign_toplevel_handle_v1* handle = wlr_ext_foreign_toplevel_handle_v1_from_resource(toplevel);
    auto* view = handle ? static_cast<View*>(handle->data) : nullptr;
    wlr_output* target = wlr_output_from_resource(output);
    if (view && target)
        server.moveToOutput(*view, *target);
}

// The struct shares its name with the wl_interface, so it needs its keyword.
const struct tde_window_info_manager_v1_interface managerImplementation {
    .destroy = [](wl_client*, wl_resource* resource) { wl_resource_destroy(resource); },
    .get_info = getInfo,
    .move_to_output = moveToOutput,
};

} // namespace

// Where windows are, for the shell to animate them from there, and moving them to another
// output, as the overview does.
void Server::setUpWindowInfo()
{
    wl_global_create(display, &tde_window_info_manager_v1_interface, 2, this,
        [](wl_client* client, void* data, uint32_t version, uint32_t id) {
            wl_resource* manager = wl_resource_create(client, &tde_window_info_manager_v1_interface, int(version), id);
            if (!manager) {
                wl_client_post_no_memory(client);
                return;
            }
            wl_resource_set_implementation(manager, &managerImplementation, data, nullptr);
        });
}

} // namespace atlas
