#include <gtest/gtest.h>

#include <system_error>

#include "PiSubmarine/PWM/Linux/ErrorCode.h"
#include "PiSubmarine/PWM/Linux/Driver.h"

namespace PiSubmarine::PWM::Linux
{
	TEST(DriverTest, ErrorCategoryProvidesReadableMessages)
	{
		const auto invalidChannelPath = make_error_code(ErrorCode::InvalidChannelPath);
		EXPECT_STREQ(invalidChannelPath.category().name(), "PiSubmarine.PWM.Linux");
		EXPECT_EQ(invalidChannelPath.message(), "invalid PWM channel path");

		const auto exportTimedOut = make_error_code(ErrorCode::ExportTimedOut);
		EXPECT_EQ(exportTimedOut.message(), "timed out while waiting for PWM channel export");

		const auto invalidNodeValue = make_error_code(ErrorCode::InvalidNodeValue);
		EXPECT_EQ(invalidNodeValue.message(), "PWM sysfs node contains an invalid value");
	}

    TEST(DriverTest, CanBeConstructedWithSysfsPath)
    {
        Driver driver("/sys/class/pwm/pwmchip0/pwm0");
        (void)driver;
        SUCCEED();
    }
}
