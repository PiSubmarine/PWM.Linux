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

        [[nodiscard]] Error SetEnabled(bool enabled) override;
        [[nodiscard]] bool IsEnabled() const override;
        [[nodiscard]] std::expected<Hertz, Error> GetFrequency() const override;
        [[nodiscard]] Error SetFrequency(Hertz frequency) override;
        [[nodiscard]] std::expected<NormalizedFraction, Error> GetDutyCycle() const override;
        [[nodiscard]] Error SetDutyCycle(NormalizedFraction duty) override;
        [[nodiscard]] Error SetFrequencyAndDuty(Hertz frequency, NormalizedFraction duty) override;

    private:
        std::filesystem::path m_PwmChannelPath;
        std::optional<std::uint64_t> m_StagedPeriodNs;
        std::optional<std::uint64_t> m_StagedDutyCycleNs;
    };
}
