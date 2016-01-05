/*
 * Copyright © 2016 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
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
 * Authored By: Kevin DuBois <kevin.dubois@canonical.com>
 */

#ifndef MIR_TEST_FRAMEWORK_VISIBLE_SURFACE_H_
#define MIR_TEST_FRAMEWORK_VISIBLE_SURFACE_H_

#include "mir_toolkit/mir_client_library.h"
#include <mutex>
#include <condition_variable>

namespace mir_test_framework
{
class VisibleSurface
{
public:
    explicit VisibleSurface(MirSurfaceSpec* spec);
    ~VisibleSurface();
    static void event_callback(MirSurface* surf, MirEvent const* ev, void* context);
    void set_visibility(MirSurface* surf, bool vis);
    operator MirSurface*() const;
    VisibleSurface(VisibleSurface&& that);
private:
    std::mutex mutex;
    std::condition_variable cv;

    VisibleSurface(VisibleSurface const&) = delete;
    MirSurface* surface;
    bool visible;
};
}

#endif /* MIR_TEST_FRAMEWORK_VISIBLE_SURFACE_H_ */
