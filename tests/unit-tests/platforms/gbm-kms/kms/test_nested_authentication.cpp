/*
 * Copyright © 2014 Canonical Ltd.
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
 *
 * Authored by: Kevin DuBois <kevin.dubois@canonical.com>
 */

#include "mir/graphics/platform_authentication.h"
#include "src/platforms/gbm-kms/server/nested_authentication.h"
#include "mir/test/doubles/mock_drm.h"
#include "mir/test/doubles/mock_platform_authentication.h"
#include "mir/test/doubles/mock_mesa_auth_extensions.h"
#include "mir/test/fake_shared.h"
#include <gtest/gtest.h>

#include <system_error>
#include <cstring>

namespace mg = mir::graphics;
namespace mgg = mir::graphics::gbm;
namespace mt = mir::test;
namespace mtd = mir::test::doubles;
using namespace testing;

namespace
{
struct NestedAuthentication : ::testing::Test
{
    ::testing::NiceMock<mtd::MockDRM> mock_drm;
    mtd::MockPlatformAuthentication mock_platform_authentication;
    std::shared_ptr<mtd::MockMesaExt> mock_ext = std::make_shared<mtd::MockMesaExt>();
    unsigned int const magic{332211};
};
}

TEST_F(NestedAuthentication, uses_platform_authentication_for_auth_magic)
{
    int const success_response{0};

    EXPECT_CALL(mock_platform_authentication, auth_extension())
        .WillOnce(Return(mir::optional_value<std::shared_ptr<mg::MesaAuthExtension>>{mock_ext}));
    EXPECT_CALL(*mock_ext, auth_magic(magic))
        .WillOnce(Return(success_response));

    mgg::NestedAuthentication auth{mt::fake_shared(mock_platform_authentication)};
    auth.auth_magic(magic);
}

TEST_F(NestedAuthentication, reports_error_because_of_no_extension)
{
    EXPECT_CALL(mock_platform_authentication, auth_extension())
        .WillOnce(Return(mir::optional_value<std::shared_ptr<mg::MesaAuthExtension>>{}));
    EXPECT_THROW({
        mgg::NestedAuthentication auth{mt::fake_shared(mock_platform_authentication)};
    }, std::runtime_error);
}

TEST_F(NestedAuthentication, reports_errors_during_auth_magic)
{
    int const error_response{-1};
    EXPECT_CALL(mock_platform_authentication, auth_extension())
        .WillOnce(Return(mir::optional_value<std::shared_ptr<mg::MesaAuthExtension>>{mock_ext}));
    EXPECT_CALL(*mock_ext, auth_magic(magic))
        .WillOnce(Return(error_response));

    mgg::NestedAuthentication auth{mt::fake_shared(mock_platform_authentication)};
    EXPECT_THROW({
        auth.auth_magic(magic);
    }, std::system_error);
}

TEST_F(NestedAuthentication, uses_platform_authentication_for_auth_fd)
{
    int const auth_fd{13};
    EXPECT_CALL(mock_platform_authentication, auth_extension())
        .WillOnce(Return(mir::optional_value<std::shared_ptr<mg::MesaAuthExtension>>{mock_ext}));
    EXPECT_CALL(*mock_ext, auth_fd())
        .WillOnce(Return(mir::Fd{mir::IntOwnedFd{auth_fd}}));
    mgg::NestedAuthentication auth{mt::fake_shared(mock_platform_authentication)};
    EXPECT_THAT(auth.authenticated_fd(), Eq(auth_fd));
}
