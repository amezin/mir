/*
 * Copyright © 2012 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Kevin DuBois <kevin.dubois@canonical.com>
 */

#include "mir/process/process.h"

#include "mir/frontend/protobuf_asio_communicator.h"
#include "mir/frontend/resource_cache.h"
#include "mir/graphics/android/android_buffer.h"
#include "mir/graphics/android/android_alloc_adaptor.h"
#include "mir/compositor/buffer_ipc_package.h"

#include "mir_test/mock_server_tool.h"
#include "mir_test/test_server.h"
#include "mir_test/empty_deleter.h"

#include <hardware/gralloc.h>

#include <gmock/gmock.h>

namespace mp=mir::process;
namespace mt=mir::test;
namespace mc=mir::compositor;
namespace mga=mir::graphics::android;
namespace geom=mir::geometry;

struct TestClient
{

/* client code */
static int main_function()
{
#if 0
    /* only use C api */

    MirConnection* connection;
    MirWaitHandle* connection_wait_handle;
    MirWaitHandle* surface_wait_handle;
    MirGraphicsRegion graphics_region;
    /* establish connection */
    connection_wait_handle = mir_connect("./test_socket_surface",
                                         "test_renderer",
                                         connected_callback, NULL);
    mir_wait(connection_wait_handle);
    if (mir_connection_is_valid(connection))
        return -1;

    /* make surface */
    surface_wait_handle = mir_surface_create( connection,
                                              parameters,
                                              create_callback, NULL);
    mir_wait(surface_wait_handle);

    /* grab a buffer*/
    mir_surface_get_graphics_region( surface, &graphics_region);

    /* render pattern */
    render_stripes(graphics_region);

    /* release */
    mir_connection_release(connection);
#endif
    return 0;
}

static int exit_function()
{
    return 0;
}

};

struct MockServerGenerator : public mt::MockServerTool
{
    MockServerGenerator(const std::shared_ptr<mc::BufferIPCPackage>&)
    {

    }


    mc::BufferIPCPackage package;
};


struct TestClientIPCRender : public testing::Test
{
    void SetUp() {

        size = geom::Size{geom::Width{64}, geom::Height{48}};
        pf = geom::PixelFormat::rgba_8888;

        int err;
        struct alloc_device_t *alloc_device_raw;
        const hw_module_t    *hw_module;
        err = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &hw_module);
        if (err < 0)
            throw std::runtime_error("Could not open hardware module");
        gralloc_open(hw_module, &alloc_device_raw);

        auto alloc_device = std::shared_ptr<struct alloc_device_t> ( alloc_device_raw, mir::EmptyDeleter());

        auto alloc_adaptor = std::make_shared<mga::AndroidAllocAdaptor>(alloc_device);

        auto android_buffer = std::make_shared<mga::AndroidBuffer>(alloc_adaptor, size, pf);

        auto package = android_buffer->get_ipc_package();

        mock_server = std::make_shared<MockServerGenerator>(package);
        test_server = std::make_shared<mt::TestServer>("./test_socket_surface", mock_server);
        test_server->comm.start();

    }

    void TearDown()
    {
        test_server->comm.stop();
    }

    std::shared_ptr<mt::TestServer> test_server;

    std::shared_ptr<MockServerGenerator> mock_server;

    geom::Size size;
    geom::PixelFormat pf; 
};

TEST_F(TestClientIPCRender, test_render)
{
    /* start server */
    auto p = mp::fork_and_run_in_a_different_process(
        TestClient::main_function,
        TestClient::exit_function);

    /* wait for connect */    
    /* wait for buffer sent back */

    EXPECT_TRUE(p->wait_for_termination().succeeded());


    /* verify pattern */
}
