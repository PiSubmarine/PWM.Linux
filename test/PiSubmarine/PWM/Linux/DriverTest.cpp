#include <gtest/gtest.h>
#include "PiSubmarine/PWM/Linux/Driver.h"

namespace PiSubmarine::PWM::Linux
{
    TEST(DriverTest, CanBeConstructedWithSysfsPath)
    {
        Driver driver("/sys/class/pwm/pwmchip0/pwm0");
        (void)driver;
        SUCCEED();
    }
}
