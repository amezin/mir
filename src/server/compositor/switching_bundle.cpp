/*
 * Copyright © 2013 Canonical Ltd.
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
 * Authored by:
 * Kevin DuBois <kevin.dubois@canonical.com>
 */

#include "mir/compositor/buffer_swapper.h"
#include "mir/compositor/buffer_allocation_strategy.h"
#include "switching_bundle.h"

#include <boost/throw_exception.hpp>

namespace mc=mir::compositor;

mc::SwitchingBundle::SwitchingBundle(
    std::shared_ptr<BufferAllocationStrategy> const& swapper_factory, BufferProperties const& property_request)
    : swapper_factory(swapper_factory),
      swapper(swapper_factory->create_swapper_new_buffers(
                  bundle_properties, property_request, mc::SwapperType::synchronous)),
      should_retry(false)
{
}

std::shared_ptr<mc::Buffer> mc::SwitchingBundle::client_acquire()
{
    /* lock is for use of 'swapper' below' */
    std::unique_lock<mc::ReadLock> lk(rw_lock);

    printf("cli ac\n");
    std::shared_ptr<mc::Buffer> buffer = nullptr;
    while (!buffer) 
    {
        try
        {
            buffer = swapper->client_acquire();
        } catch (std::logic_error& e)
        {
            /* if failure is non recoverable, rethrow. */
            if (!should_retry || (e.what() != std::string("forced_completion")))
                throw e;

            /* wait for the swapper to change before retrying */
            cv.wait(lk);
        }
    }
    return buffer;
}

void mc::SwitchingBundle::client_release(std::shared_ptr<mc::Buffer> const& released_buffer)
{
    printf("cli rel\n");
    std::unique_lock<mc::ReadLock> lk(rw_lock);
    return swapper->client_release(released_buffer);
}

std::shared_ptr<mc::Buffer> mc::SwitchingBundle::compositor_acquire()
{
    printf("comp acq\n");
    std::unique_lock<mc::ReadLock> lk(rw_lock);
    return swapper->compositor_acquire();
}

void mc::SwitchingBundle::compositor_release(std::shared_ptr<mc::Buffer> const& released_buffer)
{
    printf("comp rel\n");
    std::unique_lock<mc::ReadLock> lk(rw_lock);
    return swapper->compositor_release(released_buffer);
}

void mc::SwitchingBundle::force_client_completion()
{
    std::unique_lock<mc::ReadLock> lk(rw_lock);
    should_retry = false;
    swapper->force_client_completion();
}

void mc::SwitchingBundle::allow_framedropping(bool allow_dropping)
{
    {
        std::unique_lock<mc::ReadLock> lk(rw_lock);
        should_retry = true;
        swapper->force_client_completion();
    }

    std::unique_lock<mc::WriteLock> lk(rw_lock);
    std::vector<std::shared_ptr<mc::Buffer>> list{};
    size_t size = 0;
    swapper->end_responsibility(list, size);

    if (allow_dropping)
    {

        printf("async\n");
        swapper = swapper_factory->create_swapper_reuse_buffers(bundle_properties, list, size, mc::SwapperType::framedropping);
    }
    else
    {
        printf("sync\n");
        swapper = swapper_factory->create_swapper_reuse_buffers(bundle_properties, list, size, mc::SwapperType::synchronous);
    }

    cv.notify_all();
}

mc::BufferProperties mc::SwitchingBundle::properties() const
{
    return bundle_properties;
}
