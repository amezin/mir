/*
 * Copyright © 2020 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "pointer_constraints_unstable_v1.h"
#include "pointer-constraints-unstable-v1_wrapper.h"
#include "basic_wayland_surface_observer.h"
#include "wl_region.h"
#include "wl_surface.h"

#include <mir/scene/surface.h>
#include <mir/shell/shell.h>
#include <mir/shell/surface_specification.h>

namespace mir
{
namespace frontend
{
class PointerConstraintsV1 : public wayland::PointerConstraintsV1
{
public:
    PointerConstraintsV1(wl_resource* resource, WlSeat* seat, std::shared_ptr<shell::Shell> shell);

    class Global : public wayland::PointerConstraintsV1::Global
    {
    public:
        Global(wl_display* display, WlSeat* const seat, std::shared_ptr<shell::Shell> shell);

    private:
        void bind(wl_resource* new_zwp_pointer_constraints_v1) override;
        WlSeat* const seat;
        std::shared_ptr<shell::Shell> const shell;
    };

    enum class Lifetime : uint32_t
    {
        oneshot = wayland::PointerConstraintsV1::Lifetime::oneshot,
        persistent = wayland::PointerConstraintsV1::Lifetime::persistent,
    };

private:
    WlSeat* const seat;
    std::shared_ptr<shell::Shell> const shell;

    void destroy() override;

    void lock_pointer(wl_resource* id,
                      wl_resource* surface,
                      wl_resource* pointer,
                      const std::experimental::optional<wl_resource*>& region,
                      uint32_t lifetime) override;

    void confine_pointer(wl_resource* id,
                         wl_resource* surface,
                         wl_resource* pointer,
                         const std::experimental::optional<wl_resource*>& region,
                         uint32_t lifetime) override;
};

class LockedPointerV1 : public wayland::LockedPointerV1
{
public:
    LockedPointerV1(
        wl_resource* id,
        WlSeat* seat,
        std::shared_ptr<shell::Shell> shell,
        std::shared_ptr<scene::Surface> const& scene_surface,
        std::experimental::optional<wl_resource*> const& region,
        PointerConstraintsV1::Lifetime lifetime);

private:
    WlSeat* const seat;
    std::shared_ptr<shell::Shell> const shell;
    std::weak_ptr<scene::Surface> const weak_scene_surface;

    struct MyWaylandSurfaceObserver;

    std::shared_ptr<MyWaylandSurfaceObserver> const my_surface_observer;

    void destroy() override;
    void set_cursor_position_hint(double /*surface_x*/, double /*surface_y*/) override;
    void set_region(const std::experimental::optional<wl_resource*>& /*region*/) override;
};

class ConfinedPointerV1 : public wayland::ConfinedPointerV1
{
public:
    ConfinedPointerV1(
        wl_resource* id,
        std::shared_ptr<shell::Shell> shell,
        std::shared_ptr<scene::Surface> const& scene_surface,
        std::experimental::optional<wl_resource*> const& region,
        PointerConstraintsV1::Lifetime lifetime);

private:
    std::shared_ptr<shell::Shell> const shell;
    std::weak_ptr<scene::Surface> const weak_scene_surface;

    void destroy() override;
    void set_region(const std::experimental::optional<wl_resource*>& /*region*/) override;
};

struct LockedPointerV1::MyWaylandSurfaceObserver : public BasicWaylandSurfaceObserver
{
    MyWaylandSurfaceObserver(LockedPointerV1* const self, WlSeat* seat) :
        BasicWaylandSurfaceObserver{seat}, self{self} {}

    void attrib_changed(const scene::Surface* surf, MirWindowAttrib attrib, int value) override;
    LockedPointerV1* const self;
};

void LockedPointerV1::MyWaylandSurfaceObserver::attrib_changed(
    scene::Surface const* surf,
    MirWindowAttrib attrib,
    int value)
{
    if (attrib == mir_window_attrib_focus)
    {
        switch (auto const state = surf->confine_pointer_state())
        {
        case mir_pointer_locked_persistent:
        case mir_pointer_locked_oneshot:
            if (value)
            {
                run_on_wayland_thread_unless_destroyed([self=self]()
                    { self->send_locked_event(); });
            }
            else
            {
                run_on_wayland_thread_unless_destroyed([self=self, state]()
                    {
                        self->send_unlocked_event();

                        if (state == mir_pointer_locked_oneshot)
                        {
                            self->destroy();
                        }
                    });
            }

            break;

        default:
            break;
        }
    }
    NullSurfaceObserver::attrib_changed(surf, attrib, value);
}
}
}

auto mir::frontend::create_pointer_constraints_unstable_v1(wl_display* display, WlSeat* seat, std::shared_ptr<shell::Shell> shell)
    -> std::shared_ptr<void>
{
    return std::make_shared<PointerConstraintsV1::Global>(display, seat, std::move(shell));
}

mir::frontend::PointerConstraintsV1::Global::Global(wl_display* display, WlSeat* const seat, std::shared_ptr<shell::Shell> shell) :
    wayland::PointerConstraintsV1::Global::Global{display, Version<1>{}},
    seat{seat},
    shell{std::move(shell)}
{
}

void mir::frontend::PointerConstraintsV1::Global::bind(wl_resource* new_zwp_pointer_constraints_v1)
{
    new PointerConstraintsV1{new_zwp_pointer_constraints_v1, seat, shell};
}

mir::frontend::PointerConstraintsV1::PointerConstraintsV1(wl_resource* resource, WlSeat* seat, std::shared_ptr<shell::Shell> shell) :
    wayland::PointerConstraintsV1{resource, Version<1>{}},
    seat{seat},
    shell{std::move(shell)}
{
}

void mir::frontend::PointerConstraintsV1::destroy()
{
    destroy_wayland_object();
}

void mir::frontend::PointerConstraintsV1::lock_pointer(wl_resource* id,
                                                       wl_resource* surface,
                                                       wl_resource* /*pointer*/,
                                                       std::experimental::optional<wl_resource*> const& region,
                                                       uint32_t lifetime)
{
    if (auto const s = WlSurface::from(surface)->scene_surface())
    {
        if (auto const ss = s.value())
        {
            // TODO we need to be able to report "already constrained"
            new LockedPointerV1{id, seat, shell, ss, region, Lifetime{lifetime}};
        }
    }
}

void mir::frontend::PointerConstraintsV1::confine_pointer(wl_resource* id,
                                                          wl_resource* surface,
                                                          wl_resource* /*pointer*/,
                                                          std::experimental::optional<wl_resource*> const& region,
                                                          uint32_t lifetime)
{
    if (auto const s = WlSurface::from(surface)->scene_surface())
    {
        if (auto const ss = s.value())
        {
            // TODO we need to be able to report "already constrained"
            new ConfinedPointerV1{id, shell, ss, region, Lifetime{lifetime}};
        }
    }
}

mir::frontend::LockedPointerV1::LockedPointerV1(
    wl_resource* id,
    WlSeat* seat,
    std::shared_ptr<shell::Shell> shell,
    std::shared_ptr<scene::Surface> const& scene_surface,
    std::experimental::optional<wl_resource*> const& region,
    PointerConstraintsV1::Lifetime lifetime) :
    wayland::LockedPointerV1{id, Version<1>{}},
    seat{seat},
    shell{std::move(shell)},
    weak_scene_surface{scene_surface},
    my_surface_observer{std::make_shared<MyWaylandSurfaceObserver>(this, seat)}
{
    scene_surface->add_observer(my_surface_observer);

    shell::SurfaceSpecification mods;

    switch (lifetime)
    {
    case PointerConstraintsV1::Lifetime::oneshot:
        mods.confine_pointer = MirPointerConfinementState::mir_pointer_locked_oneshot;
        break;

    case PointerConstraintsV1::Lifetime::persistent:
        mods.confine_pointer = MirPointerConfinementState::mir_pointer_locked_persistent;
        break;
    }

    if (region)
    {
        if (WlRegion* wlregion = WlRegion::from(region.value()))
        {
            auto shape = wlregion->rectangle_vector();
            mods.input_shape = {shape};
        }
        else
        {
            mods.input_shape = std::vector<geometry::Rectangle>{};
        }
    }

    // TODO we need to be able to report "already constrained"
    this->shell->modify_surface(scene_surface->session().lock(), scene_surface, mods);

    if (scene_surface->focus_state() == mir_window_focus_state_focused)
        send_locked_event();
}

void mir::frontend::LockedPointerV1::destroy()
{
    my_surface_observer->disconnect();
    if (auto const scene_surface = weak_scene_surface.lock())
    {
        scene_surface->remove_observer(my_surface_observer);
        shell::SurfaceSpecification mods;
        mods.confine_pointer = MirPointerConfinementState::mir_pointer_unconfined;
        shell->modify_surface(scene_surface->session().lock(), scene_surface, mods);
    }
    destroy_wayland_object();
}

void mir::frontend::LockedPointerV1::set_cursor_position_hint(double /*surface_x*/, double /*surface_y*/)
{
}

void mir::frontend::LockedPointerV1::set_region(const std::experimental::optional<wl_resource*>& /*region*/)
{
}

mir::frontend::ConfinedPointerV1::ConfinedPointerV1(
    wl_resource* id,
    std::shared_ptr<shell::Shell> shell,
    std::shared_ptr<scene::Surface> const& scene_surface,
    std::experimental::optional<wl_resource*> const& region,
    PointerConstraintsV1::Lifetime lifetime) :
    wayland::ConfinedPointerV1{id, Version<1>{}},
    shell{std::move(shell)},
    weak_scene_surface(scene_surface)
{
    shell::SurfaceSpecification mods;

    switch (lifetime)
    {
    case PointerConstraintsV1::Lifetime::oneshot:
        mods.confine_pointer = MirPointerConfinementState::mir_pointer_confined_to_window_oneshot;
        break;

    case PointerConstraintsV1::Lifetime::persistent:
        mods.confine_pointer = MirPointerConfinementState::mir_pointer_confined_to_window_persistent;
        break;
    }

    if (region)
    {
        if (WlRegion* wlregion = WlRegion::from(region.value()))
        {
            auto shape = wlregion->rectangle_vector();
            mods.input_shape = {shape};
        }
        else
        {
            mods.input_shape = std::vector<geometry::Rectangle>{};
        }
    }

    this->shell->modify_surface(scene_surface->session().lock(), scene_surface, mods);
}

void mir::frontend::ConfinedPointerV1::destroy()
{
    if (auto const scene_surface = weak_scene_surface.lock())
    {
        shell::SurfaceSpecification mods;
        mods.confine_pointer = MirPointerConfinementState::mir_pointer_unconfined;
        shell->modify_surface(scene_surface->session().lock(), scene_surface, mods);
    }

    destroy_wayland_object();
}

void mir::frontend::ConfinedPointerV1::set_region(const std::experimental::optional<wl_resource*>& /*region*/)
{
}
