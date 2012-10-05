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

#include "mir_client/mir_client_library.h"
#include "android/android_client_buffer_factory.h"
#include "mir_test/mock_android_registrar.h"

#include <gtest/gtest.h>
//#include <gmock/gmock.h>

namespace mcl=mir::client;
namespace mt=mir::test;

TEST(MirAndroidClientBufferFactory, factory)
{
    using namespace testing;

    auto mock_registrar = std::make_shared<mt::MockAndroidRegistrar>();
    MirBufferPackage package;

    mcl::AndroidClientBufferFactory factory(mock_registrar);
   
    EXPECT_CALL(*mock_registrar, register_buffer(_))
        .Times(1); 
    auto buffer = factory.create_buffer_from_ipc_message(package);
 
}
