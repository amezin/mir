/*
 * Copyright © 2013 Canonical Ltd.
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
 */

#include "mir/asio_main_loop.h"
#include "mir/time/high_resolution_clock.h"
#include "mir_test/pipe.h"
#include "mir_test/auto_unblock_thread.h"
#include "mir_test/signal.h"

#include <gtest/gtest.h>

#include <thread>
#include <atomic>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <array>
#include <boost/throw_exception.hpp>

#include <sys/types.h>
#include <unistd.h>

namespace mt = mir::test;

TEST(AsioMainLoopTest, signal_handled)
{
    int const signum{SIGUSR1};
    int handled_signum{0};

    mir::AsioMainLoop ml;

    ml.register_signal_handler(
        {signum},
        [&handled_signum, &ml](int sig)
        {
           handled_signum = sig;
           ml.stop();
        });

    kill(getpid(), signum);

    ml.run();

    ASSERT_EQ(signum, handled_signum);
}


TEST(AsioMainLoopTest, multiple_signals_handled)
{
    std::vector<int> const signals{SIGUSR1, SIGUSR2};
    size_t const num_signals_to_send{10};
    std::vector<int> handled_signals;
    std::atomic<unsigned int> num_handled_signals{0};

    mir::AsioMainLoop ml;

    ml.register_signal_handler(
        {signals[0], signals[1]},
        [&handled_signals, &num_handled_signals](int sig)
        {
           handled_signals.push_back(sig);
           ++num_handled_signals;
        });


    std::thread signal_sending_thread(
        [&ml, num_signals_to_send, &signals, &num_handled_signals]
        {
            for (size_t i = 0; i < num_signals_to_send; i++)
            {
                kill(getpid(), signals[i % signals.size()]);
                while (num_handled_signals <= i) std::this_thread::yield();
            }
            ml.stop();
        });

    ml.run();

    signal_sending_thread.join();

    ASSERT_EQ(num_signals_to_send, handled_signals.size());

    for (size_t i = 0; i < num_signals_to_send; i++)
        ASSERT_EQ(signals[i % signals.size()], handled_signals[i]) << " index " << i;
}

TEST(AsioMainLoopTest, all_registered_handlers_are_called)
{
    int const signum{SIGUSR1};
    std::vector<int> handled_signum{0,0,0};

    mir::AsioMainLoop ml;

    ml.register_signal_handler(
        {signum},
        [&handled_signum, &ml](int sig)
        {
            handled_signum[0] = sig;
            if (handled_signum[0] != 0 &&
                handled_signum[1] != 0 &&
                handled_signum[2] != 0)
            {
                ml.stop();
            }
        });

    ml.register_signal_handler(
        {signum},
        [&handled_signum, &ml](int sig)
        {
            handled_signum[1] = sig;
            if (handled_signum[0] != 0 &&
                handled_signum[1] != 0 &&
                handled_signum[2] != 0)
            {
                ml.stop();
            }
        });

    ml.register_signal_handler(
        {signum},
        [&handled_signum, &ml](int sig)
        {
            handled_signum[2] = sig;
            if (handled_signum[0] != 0 &&
                handled_signum[1] != 0 &&
                handled_signum[2] != 0)
            {
                ml.stop();
            }
        });

    kill(getpid(), signum);

    ml.run();

    ASSERT_EQ(signum, handled_signum[0]);
    ASSERT_EQ(signum, handled_signum[1]);
    ASSERT_EQ(signum, handled_signum[2]);
}

TEST(AsioMainLoopTest, fd_data_handled)
{
    mt::Pipe p;
    char const data_to_write{'a'};
    int handled_fd{0};
    char data_read{0};

    mir::AsioMainLoop ml;

    ml.register_fd_handler(
        {p.read_fd()},
        [&handled_fd, &data_read, &ml](int fd)
        {
            handled_fd = fd;
            EXPECT_EQ(1, read(fd, &data_read, 1));
            ml.stop();
        });

    EXPECT_EQ(1, write(p.write_fd(), &data_to_write, 1));

    ml.run();

    EXPECT_EQ(data_to_write, data_read);
}

TEST(AsioMainLoopTest, multiple_fds_with_single_handler_handled)
{
    std::vector<mt::Pipe> const pipes(2);
    size_t const num_elems_to_send{10};
    std::vector<int> handled_fds;
    std::vector<size_t> elems_read;
    std::atomic<unsigned int> num_handled_fds{0};

    mir::AsioMainLoop ml;

    ml.register_fd_handler(
        {pipes[0].read_fd(), pipes[1].read_fd()},
        [&handled_fds, &elems_read, &num_handled_fds](int fd)
        {
            handled_fds.push_back(fd);

            size_t i;
            EXPECT_EQ(static_cast<ssize_t>(sizeof(i)),
                      read(fd, &i, sizeof(i)));
            elems_read.push_back(i);

            ++num_handled_fds;
        });

    std::thread fd_writing_thread{
        [&ml, num_elems_to_send, &pipes, &num_handled_fds]
        {
            for (size_t i = 0; i < num_elems_to_send; i++)
            {
                EXPECT_EQ(static_cast<ssize_t>(sizeof(i)),
                          write(pipes[i % pipes.size()].write_fd(), &i, sizeof(i)));
                while (num_handled_fds <= i) std::this_thread::yield();
            }
            ml.stop();
        }};

    ml.run();

    fd_writing_thread.join();

    ASSERT_EQ(num_elems_to_send, handled_fds.size());
    ASSERT_EQ(num_elems_to_send, elems_read.size());

    for (size_t i = 0; i < num_elems_to_send; i++)
    {
        EXPECT_EQ(pipes[i % pipes.size()].read_fd(), handled_fds[i]) << " index " << i;
        EXPECT_EQ(i, elems_read[i]) << " index " << i;
    }
}

TEST(AsioMainLoopTest, multiple_fd_handlers_are_called)
{
    std::vector<mt::Pipe> const pipes(3);
    std::vector<int> const elems_to_send{10,11,12};
    std::vector<int> handled_fds{0,0,0};
    std::vector<int> elems_read{0,0,0};

    mir::AsioMainLoop ml;

    ml.register_fd_handler(
        {pipes[0].read_fd()},
        [&handled_fds, &elems_read, &ml](int fd)
        {
            EXPECT_EQ(static_cast<ssize_t>(sizeof(elems_read[0])),
                      read(fd, &elems_read[0], sizeof(elems_read[0])));
            handled_fds[0] = fd;
            if (handled_fds[0] != 0 &&
                handled_fds[1] != 0 &&
                handled_fds[2] != 0)
            {
                ml.stop();
            }
        });

    ml.register_fd_handler(
        {pipes[1].read_fd()},
        [&handled_fds, &elems_read, &ml](int fd)
        {
            EXPECT_EQ(static_cast<ssize_t>(sizeof(elems_read[1])),
                      read(fd, &elems_read[1], sizeof(elems_read[1])));
            handled_fds[1] = fd;
            if (handled_fds[0] != 0 &&
                handled_fds[1] != 0 &&
                handled_fds[2] != 0)
            {
                ml.stop();
            }
        });

    ml.register_fd_handler(
        {pipes[2].read_fd()},
        [&handled_fds, &elems_read, &ml](int fd)
        {
            EXPECT_EQ(static_cast<ssize_t>(sizeof(elems_read[2])),
                      read(fd, &elems_read[2], sizeof(elems_read[2])));
            handled_fds[2] = fd;
            if (handled_fds[0] != 0 &&
                handled_fds[1] != 0 &&
                handled_fds[2] != 0)
            {
                ml.stop();
            }
        });

    EXPECT_EQ(static_cast<ssize_t>(sizeof(elems_to_send[0])),
              write(pipes[0].write_fd(), &elems_to_send[0], sizeof(elems_to_send[0])));
    EXPECT_EQ(static_cast<ssize_t>(sizeof(elems_to_send[1])),
              write(pipes[1].write_fd(), &elems_to_send[1], sizeof(elems_to_send[1])));
    EXPECT_EQ(static_cast<ssize_t>(sizeof(elems_to_send[2])),
              write(pipes[2].write_fd(), &elems_to_send[2], sizeof(elems_to_send[2])));

    ml.run();

    EXPECT_EQ(pipes[0].read_fd(), handled_fds[0]);
    EXPECT_EQ(pipes[1].read_fd(), handled_fds[1]);
    EXPECT_EQ(pipes[2].read_fd(), handled_fds[2]);

    EXPECT_EQ(elems_to_send[0], elems_read[0]);
    EXPECT_EQ(elems_to_send[1], elems_read[1]);
    EXPECT_EQ(elems_to_send[2], elems_read[2]);
}

TEST(AsioMainLoopTest, main_loop_runs_until_stop_called)
{
    mir::AsioMainLoop ml;


    auto mainloop_started = std::make_shared<mt::Signal>();

    auto fire_on_mainloop_start = ml.notify_in(std::chrono::milliseconds{0},
                                               [mainloop_started]()
    {
        mainloop_started->raise();
    });

    mt::AutoUnblockThread runner([&ml]() { ml.stop(); },
                                 [&ml]() { ml.run(); });

    ASSERT_TRUE(mainloop_started->wait_for(std::chrono::milliseconds{10}));

    auto timer_fired = std::make_shared<mt::Signal>();
    auto alarm = ml.notify_in(std::chrono::milliseconds{10}, [timer_fired]
    {
        timer_fired->raise();
    });

    EXPECT_TRUE(timer_fired->wait_for(std::chrono::milliseconds{100}));

    ml.stop();
    // Main loop should be stopped now

    timer_fired = std::make_shared<mt::Signal>();
    auto should_not_fire =  ml.notify_in(std::chrono::milliseconds{0},
                                         [timer_fired]()
    {
        timer_fired->raise();
    });

    EXPECT_FALSE(timer_fired->wait_for(std::chrono::milliseconds{100}));
}

TEST(AsioMainLoopTest, alarm_fires_with_correct_delay)
{
    mir::AsioMainLoop ml;

    mt::AutoUnblockThread runner([&ml]() { ml.stop(); },
                                 [&ml]() { ml.run(); });

    auto timer_fired = std::make_shared<mt::Signal>();
    auto alarm = ml.notify_in(std::chrono::milliseconds{100}, [timer_fired]()
    {
        timer_fired->raise();
    });

    // Shouldn't fire before timeout
    EXPECT_FALSE(timer_fired->wait_for(std::chrono::milliseconds{50}));

    // Give a nice, long wait for our slow ARM valgrind friends
    EXPECT_TRUE(timer_fired->wait_for(std::chrono::milliseconds{200}));
}

TEST(AsioMainLoopTest, multiple_alarms_fire)
{
    mir::AsioMainLoop ml;

    mt::AutoUnblockThread runner([&ml]() { ml.stop(); },
                                 [&ml]() { ml.run(); });

    int const alarm_count{10};
    std::atomic<int> call_count{0};
    std::array<std::unique_ptr<mir::time::Alarm>, alarm_count> alarms;
    auto alarms_fired = std::make_shared<mt::Signal>();

    for (auto& alarm : alarms)
    {
        alarm = ml.notify_in(std::chrono::milliseconds{5}, [alarms_fired, &call_count]()
        {
            call_count++;
            if (call_count == alarm_count)
                alarms_fired->raise();
        });
    }

    EXPECT_TRUE(alarms_fired->wait_for(std::chrono::milliseconds{100}));
    for (auto& alarm : alarms)
        EXPECT_EQ(mir::time::Alarm::Triggered, alarm->state());
}


TEST(AsioMainLoopTest, alarm_changes_to_triggered_state)
{
    mir::AsioMainLoop ml;

    mt::AutoUnblockThread runner([&ml]() { ml.stop(); },
                                 [&ml]() { ml.run(); });

    auto alarm_fired = std::make_shared<mt::Signal>();
    auto alarm = ml.notify_in(std::chrono::milliseconds{5}, [alarm_fired]()
    {
        alarm_fired->raise();
    });

    ASSERT_TRUE(alarm_fired->wait_for(std::chrono::milliseconds{100}));

    EXPECT_EQ(mir::time::Alarm::Triggered, alarm->state());
}

TEST(AsioMainLoopTest, alarm_starts_in_pending_state)
{
    mir::AsioMainLoop ml;

    mt::AutoUnblockThread runner([&ml]() { ml.stop(); },
                                 [&ml]() { ml.run(); });

    auto alarm = ml.notify_in(std::chrono::milliseconds{5000}, [](){});

    EXPECT_EQ(mir::time::Alarm::Pending, alarm->state());
}

TEST(AsioMainLoopTest, cancelled_alarm_doesnt_fire)
{
    mir::AsioMainLoop ml;

    mt::AutoUnblockThread runner([&ml]() { ml.stop(); },
                                 [&ml]() { ml.run(); });
    auto alarm_fired = std::make_shared<mt::Signal>();

    auto alarm = ml.notify_in(std::chrono::milliseconds{100}, [alarm_fired]()
    {
        alarm_fired->raise();
    });

    EXPECT_TRUE(alarm->cancel());
    EXPECT_FALSE(alarm_fired->wait_for(std::chrono::milliseconds{300}));
    EXPECT_EQ(mir::time::Alarm::Cancelled, alarm->state());
}

TEST(AsioMainLoopTest, destroyed_alarm_doesnt_fire)
{
    mir::AsioMainLoop ml;

    mt::AutoUnblockThread runner([&ml]() { ml.stop(); },
                                 [&ml]() { ml.run(); });
    auto alarm_fired = std::make_shared<mt::Signal>();

    auto alarm = ml.notify_in(std::chrono::milliseconds{200}, [alarm_fired]()
    {
        alarm_fired->raise();
    });

    alarm.reset(nullptr);

    EXPECT_FALSE(alarm_fired->wait_for(std::chrono::milliseconds{300}));
}

TEST(AsioMainLoopTest, rescheduled_alarm_fires_again)
{
    mir::AsioMainLoop ml;

    mt::AutoUnblockThread runner([&ml]() { ml.stop(); },
                                 [&ml]() { ml.run(); });

    auto first_trigger = std::make_shared<mt::Signal>();
    auto second_trigger = std::make_shared<mt::Signal>();
    std::atomic<int> call_count{0};

    auto alarm = ml.notify_in(std::chrono::milliseconds{0}, [first_trigger, second_trigger, &call_count]()
    {
        auto prev_call_count = call_count++;
        if (prev_call_count == 0)
            first_trigger->raise();
        if (prev_call_count == 1)
            second_trigger->raise();
        if (prev_call_count > 1)
            FAIL() << "Alarm called too many times";
    });


    ASSERT_TRUE(first_trigger->wait_for(std::chrono::milliseconds{50}));

    ASSERT_EQ(mir::time::Alarm::Triggered, alarm->state());
    alarm->reschedule_in(std::chrono::milliseconds{100});
    EXPECT_EQ(mir::time::Alarm::Pending, alarm->state());

    EXPECT_TRUE(second_trigger->wait_for(std::chrono::milliseconds{500}));
    EXPECT_EQ(mir::time::Alarm::Triggered, alarm->state());
}

TEST(AsioMainLoopTest, rescheduled_alarm_cancels_previous_scheduling)
{
    mir::AsioMainLoop ml;

    mt::AutoUnblockThread runner([&ml]() { ml.stop(); },
                                 [&ml]() { ml.run(); });

    std::atomic<int> call_count{0};
    auto alarm_fired = std::make_shared<mt::Signal>();

    auto alarm = ml.notify_in(std::chrono::milliseconds{100}, [alarm_fired, &call_count]()
    {
        call_count++;
    });

    EXPECT_TRUE(alarm->reschedule_in(std::chrono::milliseconds{10}));
    EXPECT_EQ(mir::time::Alarm::Pending, alarm->state());

    std::this_thread::sleep_for(std::chrono::milliseconds{500});

    EXPECT_EQ(mir::time::Alarm::Triggered, alarm->state());
    EXPECT_EQ(1, call_count);
}

TEST(AsioMainLoopTest, alarm_fires_at_correct_time_point)
{
    mir::AsioMainLoop ml;
    mir::time::HighResolutionClock clock;

    mt::AutoUnblockThread runner([&ml]() { ml.stop(); },
                                 [&ml]() { ml.run(); });

    mir::time::Timestamp real_soon = clock.sample() + std::chrono::milliseconds{120};
    auto alarm_fired = std::make_shared<mt::Signal>();

    auto alarm = ml.notify_at(real_soon, [alarm_fired]()
    {
        alarm_fired->raise();
    });

    EXPECT_FALSE(alarm_fired->wait_until(real_soon - std::chrono::milliseconds{50}));
    EXPECT_TRUE(alarm_fired->wait_until(real_soon + std::chrono::milliseconds{50}));
}
