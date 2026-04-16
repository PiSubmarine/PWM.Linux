#include "PiSubmarine/PWM/Linux/Driver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include "PiSubmarine/Error/Api/MakeError.h"

namespace PiSubmarine::PWM::Linux
{
    namespace
    {
        using ErrorCondition = PiSubmarine::Error::Api::ErrorCondition;

        template<typename T>
        using Result = PiSubmarine::Error::Api::Result<T>;

        constexpr double OneSecondNs = 1'000'000'000.0;

        [[nodiscard]] Result<void> MakeContractError() noexcept
        {
            return std::unexpected(PiSubmarine::Error::Api::MakeError(ErrorCondition::ContractError));
        }

        [[nodiscard]] Result<void> MakeCommunicationError() noexcept
        {
            return std::unexpected(PiSubmarine::Error::Api::MakeError(ErrorCondition::CommunicationError));
        }

        [[nodiscard]] Result<void> MakeCommunicationError(const std::error_code cause) noexcept
        {
            return std::unexpected(PiSubmarine::Error::Api::MakeError(ErrorCondition::CommunicationError, cause));
        }

        [[nodiscard]] Result<void> MakeDeviceError() noexcept
        {
            return std::unexpected(PiSubmarine::Error::Api::MakeError(ErrorCondition::DeviceError));
        }

        [[nodiscard]] std::filesystem::path BuildNodePath(const std::filesystem::path& pwmChannelPath, const char* const nodeName)
        {
            return pwmChannelPath / nodeName;
        }

        [[nodiscard]] Result<std::uint32_t> ParseChannelIndex(const std::filesystem::path& pwmChannelPath)
        {
            const auto channelName = pwmChannelPath.filename().string();
            constexpr std::string_view Prefix = "pwm";
            if (channelName.size() <= Prefix.size() || channelName.rfind(Prefix.data(), 0) != 0)
            {
                return std::unexpected(PiSubmarine::Error::Api::MakeError(ErrorCondition::ContractError));
            }

            try
            {
                const auto index = std::stoul(channelName.substr(Prefix.size()));
                return static_cast<std::uint32_t>(index);
            }
            catch (...)
            {
                return std::unexpected(PiSubmarine::Error::Api::MakeError(ErrorCondition::ContractError));
            }
        }

        [[nodiscard]] Result<void> EnsureChannelExported(const std::filesystem::path& pwmChannelPath)
        {
            if (std::filesystem::exists(BuildNodePath(pwmChannelPath, "enable")))
            {
                return {};
            }

            const auto chipPath = pwmChannelPath.parent_path();
            const auto channelIndexExpected = ParseChannelIndex(pwmChannelPath);
            if (!channelIndexExpected.has_value())
            {
                return std::unexpected(channelIndexExpected.error());
            }

            std::ofstream exportFile(chipPath / "export");
            if (!exportFile.is_open())
            {
                return MakeCommunicationError(std::make_error_code(std::errc::io_error));
            }

            exportFile << channelIndexExpected.value();
            if (exportFile.fail())
            {
                if (std::filesystem::exists(BuildNodePath(pwmChannelPath, "enable")))
                {
                    return {};
                }

                return MakeCommunicationError(std::make_error_code(std::errc::io_error));
            }

            for (int attempt = 0; attempt < 50; ++attempt)
            {
                if (std::filesystem::exists(BuildNodePath(pwmChannelPath, "enable")))
                {
                    return {};
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            return MakeCommunicationError(std::make_error_code(std::errc::timed_out));
        }

        [[nodiscard]] Result<std::uint64_t> ReadUint64(const std::filesystem::path& path)
        {
            std::ifstream input(path);
            if (!input.is_open())
            {
                return std::unexpected(PiSubmarine::Error::Api::MakeError(
                    ErrorCondition::CommunicationError,
                    std::make_error_code(std::errc::io_error)));
            }

            std::uint64_t value = 0;
            input >> value;
            if (input.fail())
            {
                return std::unexpected(PiSubmarine::Error::Api::MakeError(ErrorCondition::DeviceError));
            }

            return value;
        }

        [[nodiscard]] Result<void> WriteUint64(const std::filesystem::path& path, const std::uint64_t value)
        {
            std::ofstream output(path);
            if (!output.is_open())
            {
                return MakeCommunicationError(std::make_error_code(std::errc::io_error));
            }

            output << value;
            if (output.fail())
            {
                return MakeCommunicationError(std::make_error_code(std::errc::io_error));
            }

            return {};
        }

        [[nodiscard]] Result<void> WriteBool(const std::filesystem::path& path, const bool value)
        {
            return WriteUint64(path, value ? 1U : 0U);
        }

        [[nodiscard]] Result<std::uint64_t> FrequencyToPeriodNs(const Hertz frequency)
        {
            if (!std::isfinite(frequency.Value) || frequency.Value <= 0.0)
            {
                return std::unexpected(PiSubmarine::Error::Api::MakeError(ErrorCondition::ContractError));
            }

            const auto periodNs = static_cast<std::uint64_t>(std::llround(OneSecondNs / frequency.Value));
            if (periodNs == 0U)
            {
                return std::unexpected(PiSubmarine::Error::Api::MakeError(ErrorCondition::ContractError));
            }

            return periodNs;
        }

        [[nodiscard]] std::uint64_t DutyToDutyCycleNs(const std::uint64_t periodNs, const NormalizedFraction dutyCycle)
        {
            const auto duty = static_cast<double>(dutyCycle);
            return static_cast<std::uint64_t>(std::llround(static_cast<double>(periodNs) * duty));
        }

        [[nodiscard]] Result<void> ApplySignal(
            const std::filesystem::path& pwmChannelPath,
            const std::uint64_t periodNs,
            const std::uint64_t dutyCycleNs,
            const bool enableAfterApply)
        {
            if (auto error = WriteBool(BuildNodePath(pwmChannelPath, "enable"), false); !error.has_value())
            {
                return error;
            }

            if (auto error = WriteUint64(BuildNodePath(pwmChannelPath, "duty_cycle"), 0U); !error.has_value())
            {
                return error;
            }

            if (auto error = WriteUint64(BuildNodePath(pwmChannelPath, "period"), periodNs); !error.has_value())
            {
                return error;
            }

            if (auto error = WriteUint64(BuildNodePath(pwmChannelPath, "duty_cycle"), dutyCycleNs); !error.has_value())
            {
                return error;
            }

            if (!enableAfterApply)
            {
                return {};
            }

            return WriteBool(BuildNodePath(pwmChannelPath, "enable"), true);
        }
    }

    Driver::Driver(std::filesystem::path pwmChannelPath)
        : m_PwmChannelPath(std::move(pwmChannelPath))
    {
    }

    Result<void> Driver::SetEnabled(const bool enabled)
    {
        if (const auto exportResult = EnsureChannelExported(m_PwmChannelPath); !exportResult.has_value())
        {
            return exportResult;
        }

        if (enabled)
        {
            if (!m_StagedPeriodNs.has_value() && !m_StagedDutyCycleNs.has_value())
            {
                return WriteBool(BuildNodePath(m_PwmChannelPath, "enable"), true);
            }

            std::uint64_t periodNs = 0U;
            if (m_StagedPeriodNs.has_value())
            {
                periodNs = m_StagedPeriodNs.value();
            }
            else
            {
                const auto periodResult = ReadUint64(BuildNodePath(m_PwmChannelPath, "period"));
                if (!periodResult.has_value())
                {
                    return std::unexpected(periodResult.error());
                }

                periodNs = periodResult.value();
            }

            std::uint64_t dutyCycleNs = 0U;
            if (m_StagedDutyCycleNs.has_value())
            {
                dutyCycleNs = m_StagedDutyCycleNs.value();
            }
            else
            {
                const auto dutyCycleResult = ReadUint64(BuildNodePath(m_PwmChannelPath, "duty_cycle"));
                if (!dutyCycleResult.has_value())
                {
                    return std::unexpected(dutyCycleResult.error());
                }

                dutyCycleNs = dutyCycleResult.value();
            }

            if (dutyCycleNs > periodNs)
            {
                return MakeContractError();
            }

            const auto applyResult = ApplySignal(m_PwmChannelPath, periodNs, dutyCycleNs, true);
            if (applyResult.has_value())
            {
                m_StagedPeriodNs.reset();
                m_StagedDutyCycleNs.reset();
            }

            return applyResult;
        }

        return WriteBool(BuildNodePath(m_PwmChannelPath, "enable"), false);
    }

    Result<bool> Driver::IsEnabled() const
    {
        if (const auto exportResult = EnsureChannelExported(m_PwmChannelPath); !exportResult.has_value())
        {
            return std::unexpected(exportResult.error());
        }

        const auto enabledResult = ReadUint64(BuildNodePath(m_PwmChannelPath, "enable"));
        if (!enabledResult.has_value())
        {
            return std::unexpected(enabledResult.error());
        }

        return enabledResult.value() != 0U;
    }

    Result<Hertz> Driver::GetFrequency() const
    {
        std::uint64_t periodNs = 0U;
        if (m_StagedPeriodNs.has_value())
        {
            periodNs = m_StagedPeriodNs.value();
        }
        else
        {
            if (const auto exportResult = EnsureChannelExported(m_PwmChannelPath); !exportResult.has_value())
            {
                return std::unexpected(exportResult.error());
            }

            const auto periodResult = ReadUint64(BuildNodePath(m_PwmChannelPath, "period"));
            if (!periodResult.has_value())
            {
                return std::unexpected(periodResult.error());
            }

            periodNs = periodResult.value();
        }

        if (periodNs == 0U)
        {
            return std::unexpected(PiSubmarine::Error::Api::MakeError(ErrorCondition::DeviceError));
        }

        return Hertz(OneSecondNs / static_cast<double>(periodNs));
    }

    Result<void> Driver::SetFrequency(const Hertz frequency)
    {
        const auto periodNsExpected = FrequencyToPeriodNs(frequency);
        if (!periodNsExpected.has_value())
        {
            return std::unexpected(periodNsExpected.error());
        }

        const auto periodNs = periodNsExpected.value();
        const auto enabledResult = IsEnabled();
        if (!enabledResult.has_value())
        {
            return std::unexpected(enabledResult.error());
        }

        if (!enabledResult.value())
        {
            if (m_StagedDutyCycleNs.has_value() && m_StagedDutyCycleNs.value() > periodNs)
            {
                return MakeContractError();
            }

            m_StagedPeriodNs = periodNs;
            return {};
        }

        const auto dutyExpected = GetDutyCycle();
        if (!dutyExpected.has_value())
        {
            return std::unexpected(dutyExpected.error());
        }

        return SetFrequencyAndDuty(frequency, dutyExpected.value());
    }

    Result<NormalizedFraction> Driver::GetDutyCycle() const
    {
        std::uint64_t periodNs = 0U;
        if (m_StagedPeriodNs.has_value())
        {
            periodNs = m_StagedPeriodNs.value();
        }
        else
        {
            if (const auto exportResult = EnsureChannelExported(m_PwmChannelPath); !exportResult.has_value())
            {
                return std::unexpected(exportResult.error());
            }

            const auto periodResult = ReadUint64(BuildNodePath(m_PwmChannelPath, "period"));
            if (!periodResult.has_value())
            {
                return std::unexpected(periodResult.error());
            }

            periodNs = periodResult.value();
        }

        if (periodNs == 0U)
        {
            return std::unexpected(PiSubmarine::Error::Api::MakeError(ErrorCondition::DeviceError));
        }

        std::uint64_t dutyCycleNs = 0U;
        if (m_StagedDutyCycleNs.has_value())
        {
            dutyCycleNs = m_StagedDutyCycleNs.value();
        }
        else
        {
            const auto dutyCycleResult = ReadUint64(BuildNodePath(m_PwmChannelPath, "duty_cycle"));
            if (!dutyCycleResult.has_value())
            {
                return std::unexpected(dutyCycleResult.error());
            }

            dutyCycleNs = dutyCycleResult.value();
        }

        const auto rawDuty = static_cast<double>(dutyCycleNs) / static_cast<double>(periodNs);
        const auto clampedDuty = std::clamp(rawDuty, 0.0, 1.0);
        return NormalizedFraction(clampedDuty);
    }

    Result<void> Driver::SetDutyCycle(const NormalizedFraction duty)
    {
        std::uint64_t periodNs = 0U;
        if (m_StagedPeriodNs.has_value())
        {
            periodNs = m_StagedPeriodNs.value();
        }
        else
        {
            if (const auto exportResult = EnsureChannelExported(m_PwmChannelPath); !exportResult.has_value())
            {
                return exportResult;
            }

            const auto periodResult = ReadUint64(BuildNodePath(m_PwmChannelPath, "period"));
            if (!periodResult.has_value())
            {
                return std::unexpected(periodResult.error());
            }

            periodNs = periodResult.value();
        }

        if (periodNs == 0U)
        {
            return MakeDeviceError();
        }

        const auto dutyCycleNs = DutyToDutyCycleNs(periodNs, duty);
        const auto enabledResult = IsEnabled();
        if (!enabledResult.has_value())
        {
            return std::unexpected(enabledResult.error());
        }

        if (!enabledResult.value())
        {
            m_StagedDutyCycleNs = dutyCycleNs;
            return {};
        }

        return WriteUint64(BuildNodePath(m_PwmChannelPath, "duty_cycle"), dutyCycleNs);
    }

    Result<void> Driver::SetFrequencyAndDuty(const Hertz frequency, const NormalizedFraction duty)
    {
        const auto periodNsExpected = FrequencyToPeriodNs(frequency);
        if (!periodNsExpected.has_value())
        {
            return std::unexpected(periodNsExpected.error());
        }

        const auto periodNs = periodNsExpected.value();
        const auto dutyCycleNs = DutyToDutyCycleNs(periodNs, duty);
        if (dutyCycleNs > periodNs)
        {
            return MakeContractError();
        }

        const auto enabledResult = IsEnabled();
        if (!enabledResult.has_value())
        {
            return std::unexpected(enabledResult.error());
        }

        if (!enabledResult.value())
        {
            m_StagedPeriodNs = periodNs;
            m_StagedDutyCycleNs = dutyCycleNs;
            return {};
        }

        if (const auto exportResult = EnsureChannelExported(m_PwmChannelPath); !exportResult.has_value())
        {
            return exportResult;
        }

        return ApplySignal(m_PwmChannelPath, periodNs, dutyCycleNs, true);
    }
}
