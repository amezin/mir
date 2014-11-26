/*
 * Copyright © 2013-2014 Canonical Ltd.
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
 * Authored by: Alexandros Frantzis <alexandros.frantzis@canonical.com>
 *              Kevin DuBois <kevin.dubois@canonical.com>
 */

#include "mir/server_action_queue.h"
#include "mir/frontend/session_authorizer.h"
#include "mir/graphics/event_handler_register.h"
#include "src/server/scene/global_event_sender.h"

#include "mir_test_framework/connected_client_headless_server.h"
#include "mir_test_doubles/null_platform.h"
#include "mir_test_doubles/null_display.h"
#include "mir_test_doubles/null_display_changer.h"
#include "mir_test_doubles/null_display_buffer.h"
#include "mir_test_doubles/null_platform.h"
#include "mir_test/display_config_matchers.h"
#include "mir_test_doubles/stub_display_configuration.h"
#include "mir_test_doubles/stub_buffer_allocator.h"
#include "mir_test_doubles/stub_session_authorizer.h"
#include "mir_test/fake_shared.h"
#include "mir_test/pipe.h"
#include "mir_test/cross_process_action.h"
#include "mir_test/wait_condition.h"

#include "mir_toolkit/mir_client_library.h"

#include <thread>
#include <atomic>
#include <future>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace mg = mir::graphics;
namespace geom = mir::geometry;
namespace mf = mir::frontend;
namespace mtf = mir_test_framework;
namespace mtd = mir::test::doubles;
namespace mt = mir::test;

namespace
{
mtd::StubDisplayConfig stub_display_config;

mtd::StubDisplayConfig changed_stub_display_config{1};

class MockDisplay : public mtd::NullDisplay
{
public:
    MockDisplay()
        : config{std::make_shared<mtd::StubDisplayConfig>()},
          handler_called{false}
    {
    }

    void for_each_display_buffer(std::function<void(mg::DisplayBuffer&)> const& f) override
    {
        f(display_buffer);
    }

    std::unique_ptr<mg::DisplayConfiguration> configuration() const override
    {
        return std::unique_ptr<mg::DisplayConfiguration>(
            new mtd::StubDisplayConfig(*config)
        );
    }

    void register_configuration_change_handler(
        mg::EventHandlerRegister& handlers,
        mg::DisplayConfigurationChangeHandler const& handler) override
    {
        handlers.register_fd_handler(
            {p.read_fd()},
            this,
            [this, handler](int fd)
            {
                char c;
                if (read(fd, &c, 1) == 1)
                {
                    handler();
                    handler_called = true;
                }
            });
    }

    MOCK_METHOD1(configure, void(mg::DisplayConfiguration const&));

    void emit_configuration_change_event(
        std::shared_ptr<mtd::StubDisplayConfig> const& new_config)
    {
        config = new_config;
        if (write(p.write_fd(), "a", 1)) {}
    }

    void wait_for_configuration_change_handler()
    {
        while (!handler_called)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

private:
    std::shared_ptr<mtd::StubDisplayConfig> config;
    mtd::NullDisplayBuffer display_buffer;
    mt::Pipe p;
    std::atomic<bool> handler_called;
};

#if 0
void wait_for_server_actions_to_finish(mir::ServerActionQueue& server_action_queue)
{
    mt::WaitCondition last_action_done;
    server_action_queue.enqueue(
        &last_action_done,
        [&] { last_action_done.wake_up_everyone(); });

    last_action_done.wait_for_at_most_seconds(5);
}
#endif
}

struct DisplayConfigurationTest : mtf::ConnectedClientHeadlessServer
{
    void SetUp() override
    {
        preset_display(mt::fake_shared(mock_display));
        mtf::ConnectedClientHeadlessServer::SetUp();
    }

    testing::NiceMock<MockDisplay> mock_display;
};

TEST_F(DisplayConfigurationTest, display_configuration_reaches_client)
{
    auto configuration = mir_connection_create_display_config(connection);

    EXPECT_THAT(*configuration,
                mt::DisplayConfigMatches(std::cref(stub_display_config)));

    mir_display_config_destroy(configuration);
}

TEST_F(DisplayConfigurationTest, hw_display_change_notification_reaches_all_clients)
{
    mtf::CrossProcessSync client_ready_fence;
    mtf::CrossProcessSync unsubscribed_client_ready_fence;
    mtf::CrossProcessSync unsubscribed_check_fence;
    mtf::CrossProcessSync send_event_fence;
    mtf::CrossProcessSync events_all_sent;

    struct SubscribedClient
    {
        SubscribedClient(mtf::CrossProcessSync const& client_ready_fence)
            : client_ready_fence{client_ready_fence}, callback_called{false}
        {
        }

        static void change_handler(MirConnection* connection, void* context)
        {
            auto configuration = mir_connection_create_display_config(connection);

            EXPECT_THAT(*configuration,
                        mt::DisplayConfigMatches(std::cref(changed_stub_display_config)));
            mir_display_config_destroy(configuration);

            auto client_config = static_cast<SubscribedClient*>(context);
            client_config->callback_called = true;
        }

        void exec(char const* mir_test_socket)
        {
            MirConnection* connection = mir_connect_sync(mir_test_socket, "notifier");

            mir_connection_set_display_config_change_callback(connection, &change_handler, this);

            client_ready_fence.signal_ready();

            while (!callback_called)
                std::this_thread::sleep_for(std::chrono::microseconds{500});

            mir_connection_release(connection);
        }

        mtf::CrossProcessSync client_ready_fence;
        std::atomic<bool> callback_called;
    } client_config(client_ready_fence);

    struct UnsubscribedClient
    {
        UnsubscribedClient(mtf::CrossProcessSync const& client_ready_fence,
                           mtf::CrossProcessSync const& client_check_fence)
         : client_ready_fence{client_ready_fence},
           client_check_fence{client_check_fence}
        {
        }

        void exec(char const* mir_test_socket)
        {
            MirConnection* connection = mir_connect_sync(mir_test_socket, "notifier");

            client_ready_fence.signal_ready();

            //wait for display change signal sent
            client_check_fence.wait_for_signal_ready();

            //at this point, the message has gone out on the wire. since we're emulating a client
            //that is passively subscribed, we will just wait for the display configuration to change
            //and then will check the new config.
            auto configuration = mir_connection_create_display_config(connection);
            while(configuration->num_outputs != changed_stub_display_config.outputs.size())
            {
                mir_display_config_destroy(configuration);
                std::this_thread::sleep_for(std::chrono::microseconds(500));
                configuration = mir_connection_create_display_config(connection);
            }

            EXPECT_THAT(*configuration,
                        mt::DisplayConfigMatches(std::cref(changed_stub_display_config)));
            mir_display_config_destroy(configuration);

            mir_connection_release(connection);
        }

        mtf::CrossProcessSync client_ready_fence;
        mtf::CrossProcessSync client_check_fence;
    } unsubscribed_client_config(unsubscribed_client_ready_fence, unsubscribed_check_fence);

    auto const change_thread = std::async(std::launch::async, [&]
        {
            send_event_fence.wait_for_signal_ready();
            mock_display.emit_configuration_change_event(
                mt::fake_shared(changed_stub_display_config));
            events_all_sent.signal_ready();
        });

    auto const client = std::async(std::launch::async, [&]
        { client_config.exec(new_connection().c_str()); });

    auto const unsubscribed_client = std::async(std::launch::async, [&]
        { unsubscribed_client_config.exec(new_connection().c_str()); });

    client_ready_fence.wait_for_signal_ready();
    unsubscribed_client_ready_fence.wait_for_signal_ready();

    send_event_fence.signal_ready();
    events_all_sent.wait_for_signal_ready();

    unsubscribed_check_fence.signal_ready();
}

#if 0
TEST_F(DisplayConfigurationTest, display_change_request_for_unauthorized_client_fails)
{
    struct ServerConfig : TestingServerConfiguration
    {
        std::shared_ptr<mf::SessionAuthorizer> the_session_authorizer() override
        {
            class StubAuthorizer : public mtd::StubSessionAuthorizer
            {
                bool configure_display_is_allowed(mf::SessionCredentials const&) override { return false; }
            };

            if (!authorizer)
                authorizer = std::make_shared<StubAuthorizer>();

            return authorizer;
        }

        std::shared_ptr<mf::SessionAuthorizer> authorizer;
    } server_config;

    launch_server_process(server_config);

    struct Client : TestingClientConfiguration
    {
        void exec()
        {
            auto connection = mir_connect_sync(mir_test_socket, __PRETTY_FUNCTION__);
            auto configuration = mir_connection_create_display_config(connection);

            mir_wait_for(mir_connection_apply_display_config(connection, configuration));
            EXPECT_THAT(mir_connection_get_error_message(connection),
                        testing::HasSubstr("not authorized to apply display configurations"));

            mir_display_config_destroy(configuration);
            mir_connection_release(connection);
        }
    } client_config;

    launch_client_process(client_config);
}

namespace
{

struct DisplayClient : TestingClientConfiguration
{
    DisplayClient(mt::CrossProcessAction const& connect,
                  mt::CrossProcessAction const& apply_config,
                  mt::CrossProcessAction const& disconnect)
        : connect{connect},
          apply_config{apply_config},
          disconnect{disconnect}
    {
    }

    void exec()
    {
        MirConnection* connection;

        connect.exec([&]
        {
            connection = mir_connect_sync(mir_test_socket, __PRETTY_FUNCTION__);
        });

        apply_config.exec([&]
        {
            auto configuration = mir_connection_create_display_config(connection);
            mir_wait_for(mir_connection_apply_display_config(connection, configuration));
            EXPECT_STREQ("", mir_connection_get_error_message(connection));
            mir_display_config_destroy(configuration);
        });

        disconnect.exec([&]
        {
            mir_connection_release(connection);
        });
    }

    mt::CrossProcessAction connect;
    mt::CrossProcessAction apply_config;
    mt::CrossProcessAction disconnect;
};

struct SimpleClient : TestingClientConfiguration
{
    SimpleClient(mt::CrossProcessAction const& connect,
                 mt::CrossProcessAction const& disconnect)
        : connect{connect},
          disconnect{disconnect}
    {
    }

    void exec()
    {
        MirConnection* connection;

        connect.exec([&]
        {
            connection = mir_connect_sync(mir_test_socket, __PRETTY_FUNCTION__);
        });

        disconnect.exec([&]
        {
            mir_connection_release(connection);
        });
    }

    mt::CrossProcessAction connect;
    mt::CrossProcessAction disconnect;
};

}

TEST_F(DisplayConfigurationTest, changing_config_for_focused_client_configures_display)
{
    mt::CrossProcessAction display_client_connect;
    mt::CrossProcessAction display_client_apply_config;
    mt::CrossProcessAction display_client_disconnect;
    mt::CrossProcessAction verify_connection_expectations;
    mt::CrossProcessAction verify_apply_config_expectations;

    struct ServerConfig : TestingServerConfiguration
    {
        ServerConfig(mt::CrossProcessAction const& verify_connection_expectations,
                     mt::CrossProcessAction const& verify_apply_config_expectations)
            : verify_connection_expectations{verify_connection_expectations},
              verify_apply_config_expectations{verify_apply_config_expectations}
        {
        }

        std::shared_ptr<mg::Platform> the_graphics_platform() override
        {
            using namespace testing;

            if (!platform)
            {
                platform = std::make_shared<StubPlatform>();
                EXPECT_CALL(platform->mock_display, configure(_)).Times(0);
            }

            return platform;
        }

        void exec() override
        {
            t = std::thread([this](){
                verify_connection_expectations.exec([&]
                {
                    wait_for_server_actions_to_finish(*the_server_action_queue());
                    testing::Mock::VerifyAndClearExpectations(&platform->mock_display);
                    EXPECT_CALL(platform->mock_display, configure(testing::_)).Times(1);
                });

                verify_apply_config_expectations.exec([&]
                {
                    wait_for_server_actions_to_finish(*the_server_action_queue());
                    testing::Mock::VerifyAndClearExpectations(&platform->mock_display);
                });
            });
        }

        void on_exit() override
        {
            t.join();
        }

        std::shared_ptr<StubPlatform> platform;
        std::thread t;
        mt::CrossProcessAction verify_connection_expectations;
        mt::CrossProcessAction verify_apply_config_expectations;
    } server_config{verify_connection_expectations,
                    verify_apply_config_expectations};

    launch_server_process(server_config);

    DisplayClient display_client_config{display_client_connect,
                                        display_client_apply_config,
                                        display_client_disconnect};

    launch_client_process(display_client_config);

    run_in_test_process([&]
    {
        display_client_connect();
        verify_connection_expectations();

        display_client_apply_config();
        verify_apply_config_expectations();

        display_client_disconnect();
    });
}

TEST_F(DisplayConfigurationTest, focusing_client_with_display_config_configures_display)
{
    mt::CrossProcessAction display_client_connect;
    mt::CrossProcessAction display_client_apply_config;
    mt::CrossProcessAction display_client_disconnect;
    mt::CrossProcessAction simple_client_connect;
    mt::CrossProcessAction simple_client_disconnect;
    mt::CrossProcessAction verify_apply_config_expectations;
    mt::CrossProcessAction verify_focus_change_expectations;

    struct ServerConfig : TestingServerConfiguration
    {
        ServerConfig(mt::CrossProcessAction const& verify_apply_config_expectations,
                     mt::CrossProcessAction const& verify_focus_change_expectations)
            : verify_apply_config_expectations{verify_apply_config_expectations},
              verify_focus_change_expectations{verify_focus_change_expectations}
        {
        }

        std::shared_ptr<mg::Platform> the_graphics_platform() override
        {
            using namespace testing;

            if (!platform)
            {
                platform = std::make_shared<StubPlatform>();
                EXPECT_CALL(platform->mock_display, configure(_)).Times(0);
            }

            return platform;
        }

        void exec() override
        {
            t = std::thread([this](){
                verify_apply_config_expectations.exec([&]
                {
                    wait_for_server_actions_to_finish(*the_server_action_queue());
                    testing::Mock::VerifyAndClearExpectations(&platform->mock_display);
                    EXPECT_CALL(platform->mock_display, configure(testing::_)).Times(1);
                });

                verify_focus_change_expectations.exec([&]
                {
                    wait_for_server_actions_to_finish(*the_server_action_queue());
                    testing::Mock::VerifyAndClearExpectations(&platform->mock_display);
                });
            });
        }

        void on_exit() override
        {
            t.join();
        }

        std::shared_ptr<StubPlatform> platform;
        std::thread t;
        mt::CrossProcessAction verify_apply_config_expectations;
        mt::CrossProcessAction verify_focus_change_expectations;
    } server_config{verify_apply_config_expectations,
                    verify_focus_change_expectations};

    launch_server_process(server_config);

    DisplayClient display_client_config{display_client_connect,
                                        display_client_apply_config,
                                        display_client_disconnect};

    SimpleClient simple_client_config{simple_client_connect,
                                      simple_client_disconnect};

    launch_client_process(display_client_config);
    launch_client_process(simple_client_config);

    run_in_test_process([&]
    {
        display_client_connect();

        /* Connect the simple client. After this the simple client should have the focus. */
        simple_client_connect();

        /* Apply the display config while not focused */
        display_client_apply_config();
        verify_apply_config_expectations();

        /*
         * Shut down the simple client. After this the focus should have changed to the
         * display client and its configuration should have been applied.
         */
        simple_client_disconnect();
        verify_focus_change_expectations();

        display_client_disconnect();
    });
}

TEST_F(DisplayConfigurationTest, changing_focus_from_client_with_config_to_client_without_config_configures_display)
{
    mt::CrossProcessAction display_client_connect;
    mt::CrossProcessAction display_client_apply_config;
    mt::CrossProcessAction display_client_disconnect;
    mt::CrossProcessAction simple_client_connect;
    mt::CrossProcessAction simple_client_disconnect;
    mt::CrossProcessAction verify_apply_config_expectations;
    mt::CrossProcessAction verify_focus_change_expectations;

    struct ServerConfig : TestingServerConfiguration
    {
        ServerConfig(mt::CrossProcessAction const& verify_apply_config_expectations,
                     mt::CrossProcessAction const& verify_focus_change_expectations)
            : verify_apply_config_expectations{verify_apply_config_expectations},
              verify_focus_change_expectations{verify_focus_change_expectations}
        {
        }

        std::shared_ptr<mg::Platform> the_graphics_platform() override
        {
            using namespace testing;

            if (!platform)
            {
                platform = std::make_shared<StubPlatform>();
                EXPECT_CALL(platform->mock_display, configure(_)).Times(1);
            }

            return platform;
        }

        void exec() override
        {
            t = std::thread([this](){
                verify_apply_config_expectations.exec([&]
                {
                    wait_for_server_actions_to_finish(*the_server_action_queue());
                    testing::Mock::VerifyAndClearExpectations(&platform->mock_display);
                    EXPECT_CALL(platform->mock_display, configure(testing::_)).Times(1);
                });

                verify_focus_change_expectations.exec([&]
                {
                    wait_for_server_actions_to_finish(*the_server_action_queue());
                    testing::Mock::VerifyAndClearExpectations(&platform->mock_display);
                });
            });
        }

        void on_exit() override
        {
            t.join();
        }

        std::shared_ptr<StubPlatform> platform;
        std::thread t;
        mt::CrossProcessAction verify_apply_config_expectations;
        mt::CrossProcessAction verify_focus_change_expectations;
    } server_config{verify_apply_config_expectations,
                    verify_focus_change_expectations};

    launch_server_process(server_config);

    DisplayClient display_client_config{display_client_connect,
                                        display_client_apply_config,
                                        display_client_disconnect};

    SimpleClient simple_client_config{simple_client_connect,
                                      simple_client_disconnect};

    launch_client_process(display_client_config);
    launch_client_process(simple_client_config);

    run_in_test_process([&]
    {
        /* Connect the simple client. */
        simple_client_connect();

        /* Connect the display config client and apply a display config. */
        display_client_connect();
        display_client_apply_config();
        verify_apply_config_expectations();

        /*
         * Shut down the display client. After this the focus should have changed to the
         * simple client and the base configuration should have been applied.
         */
        display_client_disconnect();
        verify_focus_change_expectations();

        simple_client_disconnect();
    });
}

TEST_F(DisplayConfigurationTest, hw_display_change_doesnt_apply_base_config_if_per_session_config_is_active)
{
    mt::CrossProcessAction display_client_connect;
    mt::CrossProcessAction display_client_apply_config;
    mt::CrossProcessAction display_client_disconnect;
    mt::CrossProcessAction verify_hw_config_change_expectations;

    struct ServerConfig : TestingServerConfiguration
    {
        ServerConfig(mt::CrossProcessAction const& verify_hw_config_change_expectations)
            : verify_hw_config_change_expectations{verify_hw_config_change_expectations}
        {
        }

        std::shared_ptr<mg::Platform> the_graphics_platform() override
        {

            if (!platform)
                platform = std::make_shared<StubPlatform>();

            return platform;
        }

        void exec() override
        {
            t = std::thread([this](){
                verify_hw_config_change_expectations.exec([&]
                {
                    using namespace testing;

                    wait_for_server_actions_to_finish(*the_server_action_queue());
                    Mock::VerifyAndClearExpectations(&platform->mock_display);
                    /*
                     * A client with a per-session config is active, the base configuration
                     * shouldn't be applied.
                     */
                    EXPECT_CALL(platform->mock_display, configure(_)).Times(0);
                    platform->mock_display.emit_configuration_change_event(
                        mt::fake_shared(changed_stub_display_config));
                    platform->mock_display.wait_for_configuration_change_handler();
                    wait_for_server_actions_to_finish(*the_server_action_queue());
                    Mock::VerifyAndClearExpectations(&platform->mock_display);
                });
            });
        }

        void on_exit() override
        {
            t.join();
        }

        std::shared_ptr<StubPlatform> platform;
        std::thread t;
        mt::CrossProcessAction verify_hw_config_change_expectations;
    } server_config{verify_hw_config_change_expectations};

    DisplayClient display_client_config{display_client_connect,
                                        display_client_apply_config,
                                        display_client_disconnect};

    launch_server_process(server_config);
    launch_client_process(display_client_config);

    run_in_test_process([&]
    {
        display_client_connect();
        display_client_apply_config();

        verify_hw_config_change_expectations();

        display_client_disconnect();
    });
}
#endif
