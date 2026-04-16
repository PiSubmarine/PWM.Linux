#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

#include "PiSubmarine/PWM/Api/IDriver.h"

namespace PiSubmarine::PWM::Linux
{
    class Driver : public Api::IDriver
    {
    public:
        explicit Driver(std::filesystem::path pwmChannelPath = "/sys/class/pwm/pwmchip0/pwm0");

        [[nodiscard]] PiSubmarine::Error::Api::Result<void> SetEnabled(bool enabled) override;
        [[nodiscard]] PiSubmarine::Error::Api::Result<bool> IsEnabled() const override;
        [[nodiscard]] PiSubmarine::Error::Api::Result<Hertz> GetFrequency() const override;
        [[nodiscard]] PiSubmarine::Error::Api::Result<void> SetFrequency(Hertz frequency) override;
        [[nodiscard]] PiSubmarine::Error::Api::Result<NormalizedFraction> GetDutyCycle() const override;
        [[nodiscard]] PiSubmarine::Error::Api::Result<void> SetDutyCycle(NormalizedFraction duty) override;
        [[nodiscard]] PiSubmarine::Error::Api::Result<void> SetFrequencyAndDuty(Hertz frequency, NormalizedFraction duty) override;

    private:
        std::filesystem::path m_PwmChannelPath;
        std::optional<std::uint64_t> m_StagedPeriodNs;
        std::optional<std::uint64_t> m_StagedDutyCycleNs;
    };
}
