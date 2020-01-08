/*
 * Copyright (C) 2018 Marius Gripsgard <marius@ubports.com>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "mir/log.h"
#include "xwayland_log.h"

#include "xwayland_wm_shellsurface.h"
#include "xwayland_wm_surface.h"

#include "mir/frontend/shell.h"
#include "mir/scene/surface_creation_parameters.h"

#include "wayland_surface_observer.h"
#include "wayland_utils.h"

namespace mf = mir::frontend;
namespace ms = mir::scene;
namespace msh = mir::shell;

mf::XWaylandWMShellSurface::XWaylandWMShellSurface(
    XWaylandWMSurface* xwayland_surface,
    wl_client* client,
    WlSurface* wayland_surface,
    std::shared_ptr<msh::Shell> const& shell,
    WlSeat& seat,
    OutputManager* const output_manager)
    : WindowWlSurfaceRole{&seat, client, wayland_surface, shell, output_manager},
      surface{xwayland_surface}
{
//    params->type = mir_window_type_normal;
    set_server_side_decorated(true);
}

mf::XWaylandWMShellSurface::~XWaylandWMShellSurface()
{
    mir::log_verbose("Im gone");
}

void mf::XWaylandWMShellSurface::destroy()
{

}

void mf::XWaylandWMShellSurface::set_toplevel()
{
    set_state_now(MirWindowState::mir_window_state_restored);
}

void mf::XWaylandWMShellSurface::handle_resize(std::experimental::optional<geometry::Point> const& /*new_top_left*/,
                                               geometry::Size const& new_size)
{
    surface->send_resize(new_size);
}

void mf::XWaylandWMShellSurface::handle_close_request()
{
    surface->send_close_request();
}
