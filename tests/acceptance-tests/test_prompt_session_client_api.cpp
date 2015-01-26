/*
 * Copyright © 2014 Canonical Ltd.
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
 * Authored by: Nick Dedekind <nick.dedekind@canonical.com>
 */

#include "mir_toolkit/mir_prompt_session.h"
#include "mir/scene/prompt_session_listener.h"
#include "mir/scene/prompt_session.h"
#include "mir/scene/prompt_session_manager.h"
#include "mir/scene/session.h"
#include "mir/shell/session_coordinator_wrapper.h"
#include "mir/frontend/session_credentials.h"
#include "mir/cached_ptr.h"
#include "mir/fd.h"

#include "mir_test_doubles/stub_session_authorizer.h"
#include "mir_test_doubles/mock_prompt_session_listener.h"
#include "mir_test_framework/headless_in_process_server.h"
#include "mir_test_framework/using_stub_client_platform.h"
#include "mir_test/popen.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <condition_variable>
#include <mutex>
#include <atomic>

namespace mtd = mir::test::doubles;
namespace mtf = mir_test_framework;
namespace ms = mir::scene;
namespace msh = mir::shell;
namespace mf = mir::frontend;

using namespace testing;

namespace
{
struct MockSessionAuthorizer : public mtd::StubSessionAuthorizer
{
    MockSessionAuthorizer()
    {
        ON_CALL(*this, connection_is_allowed(_)).WillByDefault(Return(true));
        ON_CALL(*this, prompt_session_is_allowed(_)).WillByDefault(Return(true));
    }

    MOCK_METHOD1(connection_is_allowed, bool(mf::SessionCredentials const&));
    MOCK_METHOD1(prompt_session_is_allowed, bool(mf::SessionCredentials const&));
};

// We need to fake any client_pids used to identify sessions
class PidFakingSessionCoordinator : public msh::SessionCoordinatorWrapper
{
public:
    PidFakingSessionCoordinator(
        std::shared_ptr<ms::SessionCoordinator> const& wrapped,
        std::vector<pid_t> const& pids) :
            msh::SessionCoordinatorWrapper(wrapped),
        pids(pids)
    {
    }

    auto open_session(
        pid_t client_pid,
        std::string const& name,
        std::shared_ptr<mf::EventSink> const& sink)
            -> std::shared_ptr<ms::Session> override
    {
        auto const override_pid = (next != pids.end()) ? *next++ : client_pid;

        return wrapped->open_session(override_pid, name, sink);
    }

private:
    std::vector<pid_t> const pids;
    std::vector<pid_t>::const_iterator next{pids.begin()};
};

struct PromptSessionClientAPI : mtf::HeadlessInProcessServer
{
    MirConnection* connection = nullptr;

    static constexpr pid_t application_session_pid = __LINE__;
    std::shared_ptr<mf::Session> application_session;
    MirConnection* application_connection{nullptr};

    std::shared_ptr<ms::PromptSession> server_prompt_session;
    mtf::UsingStubClientPlatform using_stub_client_platform;

    mir::CachedPtr<mtd::MockPromptSessionListener> mock_prompt_session_listener;
    mir::CachedPtr<MockSessionAuthorizer> mock_prompt_session_authorizer;

    std::atomic<bool> prompt_session_state_change_callback_called;

    std::shared_ptr<MockSessionAuthorizer> the_mock_session_authorizer()
    {
        return mock_prompt_session_authorizer([this]
            {
                return std::make_shared<NiceMock<MockSessionAuthorizer>>();
            });
    }

    std::shared_ptr<mtd::MockPromptSessionListener> the_mock_prompt_session_listener()
    {
        return mock_prompt_session_listener([]
            {
                return std::make_shared<NiceMock<mtd::MockPromptSessionListener>>();
            });
    }

    auto new_prompt_connection() -> std::string
    {
        auto const prompt_fd = server.open_prompt_socket();
        return HeadlessInProcessServer::connection(prompt_fd);
    }

    void start_application_session()
    {
        std::mutex application_session_mutex;
        std::condition_variable application_session_cv;

        auto connect_handler = [&](std::shared_ptr<mf::Session> const& session)
            {
                std::lock_guard<std::mutex> lock(application_session_mutex);
                application_session = session;
                application_session_cv.notify_one();
            };

        auto const fd = server.open_client_socket(connect_handler);

        application_connection = mir_connect_sync(HeadlessInProcessServer::connection(fd).c_str(), __PRETTY_FUNCTION__);

        std::unique_lock<std::mutex> lock(application_session_mutex);
        application_session_cv.wait(lock, [&] { return !!application_session; });
    }

    void SetUp() override
    {
        auto session_coordinator_wrapper = [&](std::shared_ptr<ms::SessionCoordinator> const& wrapped)
            -> std::shared_ptr<ms::SessionCoordinator>
            {
                std::vector<pid_t> fake_pids;
                fake_pids.push_back(application_session_pid);

                return std::make_shared<PidFakingSessionCoordinator>(wrapped, fake_pids);
            };

        server.override_the_session_authorizer([this]()
            ->std::shared_ptr<mf::SessionAuthorizer>
            {
                return the_mock_session_authorizer();
            });

        server.override_the_prompt_session_listener([this]
            {
                 return the_mock_prompt_session_listener();
            });

        server.wrap_session_coordinator(session_coordinator_wrapper);

        mtf::HeadlessInProcessServer::SetUp();

        start_application_session();
    }

    void capture_server_prompt_session()
    {
        EXPECT_CALL(*the_mock_prompt_session_listener(), starting(_)).
            WillOnce(SaveArg<0>(&server_prompt_session));
    }

    void wait_for_state_change_callback()
    {
        for (int i = 0; !prompt_session_state_change_callback_called.load() && i < 5000; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            std::this_thread::yield();
        }

        prompt_session_state_change_callback_called.store(false);
    }

    void TearDown() override
    {
        application_session.reset();
        if (application_connection) mir_connection_release(application_connection);
        if (connection) mir_connection_release(connection);
        mtf::HeadlessInProcessServer::TearDown();
    }

    std::shared_ptr<ms::PromptSessionManager> the_prompt_session_manager()
    {
        return server.the_prompt_session_manager();
    }

    MOCK_METHOD2(prompt_session_state_change,
        void(MirPromptSession* prompt_provider, MirPromptSessionState state));

    std::mutex mutex;
    std::vector<mir::Fd> actual_fds;
    std::condition_variable cv;
    bool called_back = false;

    bool wait_for_callback(std::chrono::milliseconds timeout)
    {
        std::unique_lock<decltype(mutex)> lock(mutex);
        return cv.wait_for(lock, timeout, [this]{ return called_back; });
    }

    char const* fd_connect_string(int fd)
    {
        static char client_connect_string[32] = {0};

        sprintf(client_connect_string, "fd://%d", fd);
        return client_connect_string;
    }

    MOCK_METHOD1(process_line, void(std::string const&));

    std::vector<std::shared_ptr<mf::Session>> list_providers_for(
        std::shared_ptr<ms::PromptSession> const& prompt_session)
    {
        std::vector<std::shared_ptr<mf::Session>> results;
        auto providers_fn = [&results](std::weak_ptr<ms::Session> const& session)
        {
            results.push_back(session.lock());
        };

        the_prompt_session_manager()->for_each_provider_in(prompt_session, providers_fn);

        return results;
    }

    static int const no_of_prompt_providers{2};
    static constexpr char const* const provider_name[no_of_prompt_providers]
    {
        "child_provider0",
        "child_provider1"
    };
};

constexpr pid_t PromptSessionClientAPI::application_session_pid;

mir_prompt_session_state_change_callback const null_state_change_callback{nullptr};
constexpr char const* const PromptSessionClientAPI::provider_name[];

extern "C" void prompt_session_state_change_callback(
    MirPromptSession* prompt_provider,
    MirPromptSessionState state,
    void* context)
{
    PromptSessionClientAPI* self = static_cast<PromptSessionClientAPI*>(context);
    self->prompt_session_state_change(prompt_provider, state);
    self->prompt_session_state_change_callback_called.store(true);
}

void client_fd_callback(MirPromptSession*, size_t count, int const* fds, void* context)
{
    auto const self = static_cast<PromptSessionClientAPI*>(context);

    std::unique_lock<decltype(self->mutex)> lock(self->mutex);

    self->actual_fds.clear();
    for (size_t i = 0; i != count; ++i)
        self->actual_fds.push_back(mir::Fd{fds[i]});

    self->called_back = true;
    self->cv.notify_one();
}

struct DummyPromptProvider
{
    DummyPromptProvider(char const* connect_string, char const* app_name) :
        connection{mir_connect_sync(connect_string, app_name)}
    {
        EXPECT_THAT(connection, NotNull());
    }

    ~DummyPromptProvider() noexcept
    {
        mir_connection_release(connection);
    }

    MirConnection* const connection;
};

MATCHER_P(IsSessionWithPid, pid, "")
{
    return arg->process_id() == pid;
}

MATCHER_P(SessionWithName, name, "")
{
    return arg->name() == name;
}
}

TEST_F(PromptSessionClientAPI, can_start_and_stop_a_prompt_session)
{
    connection = mir_connect_sync(new_connection().c_str(), __PRETTY_FUNCTION__);

    {
        InSequence server_seq;
        EXPECT_CALL(*the_mock_prompt_session_listener(), starting(_));
        EXPECT_CALL(*the_mock_prompt_session_listener(), stopping(_));
    }

    MirPromptSession* prompt_session = mir_connection_create_prompt_session_sync(
        connection, application_session_pid, null_state_change_callback, this);
    ASSERT_THAT(prompt_session, Ne(nullptr));
    EXPECT_THAT(mir_prompt_session_is_valid(prompt_session), Eq(true));
    EXPECT_THAT(mir_prompt_session_error_message(prompt_session), StrEq(""));

    mir_prompt_session_release_sync(prompt_session);
}

TEST_F(PromptSessionClientAPI, notifies_start_and_stop)
{
    connection = mir_connect_sync(new_connection().c_str(), __PRETTY_FUNCTION__);

    InSequence seq;
    EXPECT_CALL(*this, prompt_session_state_change(_, mir_prompt_session_state_started));
    EXPECT_CALL(*this, prompt_session_state_change(_, mir_prompt_session_state_stopped));

    MirPromptSession* prompt_session = mir_connection_create_prompt_session_sync(
        connection, application_session_pid, prompt_session_state_change_callback, this);

    mir_prompt_session_release_sync(prompt_session);
}

TEST_F(PromptSessionClientAPI, can_get_fds_for_prompt_providers)
{
    connection = mir_connect_sync(new_connection().c_str(), __PRETTY_FUNCTION__);

    MirPromptSession* prompt_session = mir_connection_create_prompt_session_sync(
        connection, application_session_pid, null_state_change_callback, this);

    static std::size_t const arbritary_fd_request_count = 3;

    mir_prompt_session_new_fds_for_prompt_providers(
        prompt_session, arbritary_fd_request_count, &client_fd_callback, this);

    EXPECT_TRUE(wait_for_callback(std::chrono::milliseconds(500)));
    EXPECT_THAT(actual_fds.size(), Eq(arbritary_fd_request_count));

    mir_prompt_session_release_sync(prompt_session);
}

TEST_F(PromptSessionClientAPI,
    when_prompt_provider_connects_over_fd_prompt_provider_added_with_right_pid)
{
    connection = mir_connect_sync(new_connection().c_str(), __PRETTY_FUNCTION__);

    MirPromptSession* prompt_session = mir_connection_create_prompt_session_sync(
        connection, application_session_pid, null_state_change_callback, this);

    mir_prompt_session_new_fds_for_prompt_providers(
        prompt_session, 1, &client_fd_callback, this);

    ASSERT_TRUE(wait_for_callback(std::chrono::milliseconds(500)));

    auto const expected_pid = getpid();

    EXPECT_CALL(*the_mock_prompt_session_listener(),
        prompt_provider_added(_, IsSessionWithPid(expected_pid)));

    DummyPromptProvider{fd_connect_string(actual_fds[0]), __PRETTY_FUNCTION__};

    mir_prompt_session_release_sync(prompt_session);
}

// TODO we need a nice way to run this (and similar tests that require a
// TODO separate client process) in CI. Disabled as we can't be sure the
// TODO mir_demo_client_basic executable is about.
TEST_F(PromptSessionClientAPI, DISABLED_client_pid_is_associated_with_session)
{
    connection = mir_connect_sync(new_connection().c_str(), __PRETTY_FUNCTION__);

    auto const server_pid = getpid();

    MirPromptSession* prompt_session = mir_connection_create_prompt_session_sync(
        connection, application_session_pid, null_state_change_callback, this);

    mir_prompt_session_new_fds_for_prompt_providers(prompt_session, 1, &client_fd_callback, this);
    wait_for_callback(std::chrono::milliseconds(500));

    EXPECT_CALL(*the_mock_prompt_session_listener(),
        prompt_provider_added(_, Not(IsSessionWithPid(server_pid))));

    InSequence seq;
    EXPECT_CALL(*this, process_line(StrEq("Starting")));
    EXPECT_CALL(*this, process_line(StrEq("Connected")));
    EXPECT_CALL(*this, process_line(StrEq("Surface created")));
    EXPECT_CALL(*this, process_line(StrEq("Surface released")));
    EXPECT_CALL(*this, process_line(StrEq("Connection released")));

    mir::test::Popen output(std::string("bin/mir_demo_client_basic -m ") +
        fd_connect_string(actual_fds[0]));

    std::string line;
    while (output.get_line(line)) process_line(line);

    mir_prompt_session_release_sync(prompt_session);
}

TEST_F(PromptSessionClientAPI, notifies_when_server_closes_prompt_session)
{
    connection = mir_connect_sync(new_connection().c_str(), __PRETTY_FUNCTION__);

    {
        InSequence seq;
        EXPECT_CALL(*this, prompt_session_state_change(_, mir_prompt_session_state_started));
        EXPECT_CALL(*this, prompt_session_state_change(_, mir_prompt_session_state_stopped));
    }

    capture_server_prompt_session();

    MirPromptSession* prompt_session = mir_connection_create_prompt_session_sync(
        connection, application_session_pid, prompt_session_state_change_callback, this);
    wait_for_state_change_callback();

    the_prompt_session_manager()->stop_prompt_session(server_prompt_session);
    wait_for_state_change_callback();

    // Verify we have got the "stopped" notification before we go on and release the session
    Mock::VerifyAndClearExpectations(this);

    mir_prompt_session_release_sync(prompt_session);
}

TEST_F(PromptSessionClientAPI, notifies_when_server_suspends_prompt_session)
{
    connection = mir_connect_sync(new_connection().c_str(), __PRETTY_FUNCTION__);
    {
        InSequence seq;
        EXPECT_CALL(*this, prompt_session_state_change(_, mir_prompt_session_state_started));
        EXPECT_CALL(*this, prompt_session_state_change(_, mir_prompt_session_state_suspended));
    }

    capture_server_prompt_session();

    MirPromptSession* prompt_session = mir_connection_create_prompt_session_sync(
        connection, application_session_pid, prompt_session_state_change_callback, this);
    wait_for_state_change_callback();

    the_prompt_session_manager()->suspend_prompt_session(server_prompt_session);
    wait_for_state_change_callback();

    // Verify we have got the "suspend" notification before we go on and release the session
    Mock::VerifyAndClearExpectations(this);

    EXPECT_CALL(*this, prompt_session_state_change(_, mir_prompt_session_state_stopped));
    mir_prompt_session_release_sync(prompt_session);
}

TEST_F(PromptSessionClientAPI, after_server_closes_prompt_session_api_isnt_broken)
{
    connection = mir_connect_sync(new_connection().c_str(), __PRETTY_FUNCTION__);

    capture_server_prompt_session();

    MirPromptSession* prompt_session = mir_connection_create_prompt_session_sync(
        connection, application_session_pid, null_state_change_callback, this);

    the_prompt_session_manager()->stop_prompt_session(server_prompt_session);

    mir_wait_for(mir_prompt_session_new_fds_for_prompt_providers(
        prompt_session, no_of_prompt_providers, &client_fd_callback, this));

    mir_prompt_session_release_sync(prompt_session);
}

TEST_F(PromptSessionClientAPI, server_retrieves_application_session)
{
    connection = mir_connect_sync(new_connection().c_str(), __PRETTY_FUNCTION__);
    capture_server_prompt_session();

    MirPromptSession* prompt_session = mir_connection_create_prompt_session_sync(
        connection, application_session_pid, null_state_change_callback, this);

    EXPECT_THAT(the_prompt_session_manager()->application_for(server_prompt_session),
        Eq(application_session));

    mir_prompt_session_release_sync(prompt_session);
}

TEST_F(PromptSessionClientAPI, server_retrieves_helper_session)
{
    connection = mir_connect_sync(new_connection().c_str(), __PRETTY_FUNCTION__);

    capture_server_prompt_session();

    MirPromptSession* prompt_session = mir_connection_create_prompt_session_sync(
        connection, application_session_pid, null_state_change_callback, this);

    // can get the helper session. but it will be the current pid.
    EXPECT_THAT(
        the_prompt_session_manager()->helper_for(server_prompt_session),
        IsSessionWithPid(getpid()));

    mir_prompt_session_release_sync(prompt_session);
}

TEST_F(PromptSessionClientAPI, server_retrieves_child_provider_sessions)
{
    connection = mir_connect_sync(new_connection().c_str(), __PRETTY_FUNCTION__);

    capture_server_prompt_session();

    MirPromptSession* prompt_session = mir_connection_create_prompt_session_sync(
        connection, application_session_pid, null_state_change_callback, this);

    mir_wait_for(mir_prompt_session_new_fds_for_prompt_providers(
        prompt_session, no_of_prompt_providers, &client_fd_callback, this));

    DummyPromptProvider provider1{fd_connect_string(actual_fds[0]), provider_name[0]};
    DummyPromptProvider provider2{fd_connect_string(actual_fds[1]), provider_name[1]};

    EXPECT_THAT(list_providers_for(server_prompt_session), ElementsAre(
        SessionWithName(provider_name[0]), SessionWithName(provider_name[1])));

    mir_prompt_session_release_sync(prompt_session);
}

TEST_F(PromptSessionClientAPI, cannot_start_a_prompt_session_without_authorization)
{
    EXPECT_CALL(*the_mock_session_authorizer(), prompt_session_is_allowed(_))
        .WillOnce(Return(false));

    connection = mir_connect_sync(new_connection().c_str(), __PRETTY_FUNCTION__);

    EXPECT_CALL(*the_mock_prompt_session_listener(), starting(_)).Times(0);

    MirPromptSession* prompt_session = mir_connection_create_prompt_session_sync(
        connection, application_session_pid, null_state_change_callback, this);

    EXPECT_THAT(mir_prompt_session_is_valid(prompt_session), Eq(false));
    EXPECT_THAT(mir_prompt_session_error_message(prompt_session),
        HasSubstr("Prompt sessions disabled"));

    mir_prompt_session_release_sync(prompt_session);
}

TEST_F(PromptSessionClientAPI,
    can_start_a_prompt_session_without_authorization_on_prompt_connection)
{
    ON_CALL(*the_mock_session_authorizer(), prompt_session_is_allowed(_))
        .WillByDefault(Return(false));
    EXPECT_CALL(*the_mock_session_authorizer(), prompt_session_is_allowed(_)).Times(0);

    connection = mir_connect_sync(new_prompt_connection().c_str(), __PRETTY_FUNCTION__);

    {
        InSequence server_seq;
        EXPECT_CALL(*the_mock_prompt_session_listener(), starting(_));
        EXPECT_CALL(*the_mock_prompt_session_listener(), stopping(_));
    }

    MirPromptSession* prompt_session = mir_connection_create_prompt_session_sync(
        connection, application_session_pid, null_state_change_callback, this);

    EXPECT_THAT(mir_prompt_session_is_valid(prompt_session), Eq(true));
    EXPECT_THAT(mir_prompt_session_error_message(prompt_session), StrEq(""));

    mir_prompt_session_release_sync(prompt_session);
}

TEST_F(PromptSessionClientAPI,
    prompt_providers_started_via_trusted_socket_are_not_authorized_by_shell)
{
    connection = mir_connect_sync(new_prompt_connection().c_str(), __PRETTY_FUNCTION__);

    EXPECT_CALL(*the_mock_session_authorizer(), connection_is_allowed(_)).Times(0);

    MirPromptSession* prompt_session = mir_connection_create_prompt_session_sync(
        connection, application_session_pid, null_state_change_callback, this);

    mir_wait_for(mir_prompt_session_new_fds_for_prompt_providers(
        prompt_session, no_of_prompt_providers, &client_fd_callback, this));

    DummyPromptProvider provider1{fd_connect_string(actual_fds[0]), provider_name[0]};
    DummyPromptProvider provider2{fd_connect_string(actual_fds[1]), provider_name[1]};

    mir_prompt_session_release_sync(prompt_session);
}

// lp:1377968
TEST_F(PromptSessionClientAPI, when_application_pid_is_invalid_starting_a_prompt_session_fails)
{
    connection = mir_connect_sync(new_prompt_connection().c_str(), __PRETTY_FUNCTION__);

    EXPECT_CALL(*the_mock_prompt_session_listener(), starting(_)).Times(0);

    MirPromptSession* prompt_session = mir_connection_create_prompt_session_sync(
        connection, ~application_session_pid, null_state_change_callback, this);

    EXPECT_THAT(mir_prompt_session_is_valid(prompt_session), Eq(false));
    EXPECT_THAT(mir_prompt_session_error_message(prompt_session),
        HasSubstr("Could not identify application"));

    mir_prompt_session_release_sync(prompt_session);
}
