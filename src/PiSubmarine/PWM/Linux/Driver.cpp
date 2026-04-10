#include "PiSubmarine/PWM/Linux/Driver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string_view>
#include <thread>
#include <utility>

namespace PiSubmarine::PWM::Linux
{
    namespace
    {
        constexpr double OneSecondNs = 1'000'000'000.0;

        std::filesystem::path BuildNodePath(const std::filesystem::path& pwmChannelPath, const char* nodeName)
        {
            return pwmChannelPath / nodeName;
        }

        std::expected<std::uint32_t, Api::IDriver::Error> ParseChannelIndex(const std::filesystem::path& pwmChannelPath)
        {
            const auto channelName = pwmChannelPath.filename().string();
            constexpr std::string_view Prefix = "pwm";
            if (channelName.size() <= Prefix.size() || channelName.rfind(Prefix.data(), 0) != 0)
            {
                return std::unexpected(Api::IDriver::Error::InvalidArgument);
            }

            try
            {
                const auto index = std::stoul(channelName.substr(Prefix.size()));
                return static_cast<std::uint32_t>(index);
            }
            catch (...)
            {
                return std::unexpected(Api::IDriver::Error::InvalidArgument);
            }
        }

        Api::IDriver::Error EnsureChannelExported(const std::filesystem::path& pwmChannelPath)
        {
            if (std::filesystem::exists(BuildNodePath(pwmChannelPath, "enable")))
            {
                return Api::IDriver::Error::Ok;
            }

            const auto chipPath = pwmChannelPath.parent_path();
            const auto channelIndexExpected = ParseChannelIndex(pwmChannelPath);
            if (!channelIndexExpected.has_value())
            {
                return channelIndexExpected.error();
            }

            std::ofstream exportFile(chipPath / "export");
            if (!exportFile.is_open())
            {
                return Api::IDriver::Error::IoFailure;
            }

            exportFile << channelIndexExpected.value();
            if (exportFile.fail())
            {
                if (std::filesystem::exists(BuildNodePath(pwmChannelPath, "enable")))
                {
                    return Api::IDriver::Error::Ok;
                }

                return Api::IDriver::Error::IoFailure;
            }

            for (int attempt = 0; attempt < 50; ++attempt)
            {
                if (std::filesystem::exists(BuildNodePath(pwmChannelPath, "enable")))
                {
                    return Api::IDriver::Error::Ok;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            return Api::IDriver::Error::Busy;
        }

        Api::IDriver::Error ReadUint64(const std::filesystem::path& path, std::uint64_t& value)
        {
            std::ifstream input(path);
            if (!input.is_open())
            {
                return Api::IDriver::Error::IoFailure;
            }

            input >> value;
            if (input.fail())
            {
                return Api::IDriver::Error::ProtocolError;
            }

            return Api::IDriver::Error::Ok;
        }

        Api::IDriver::Error WriteUint64(const std::filesystem::path& path, std::uint64_t value)
        {
            std::ofstream output(path);
            if (!output.is_open())
            {
                return Api::IDriver::Error::IoFailure;
            }

            output << value;
            if (output.fail())
            {
                return Api::IDriver::Error::IoFailure;
            }

            return Api::IDriver::Error::Ok;
        }

        Api::IDriver::Error WriteBool(const std::filesystem::path& path, bool value)
        {
            return WriteUint64(path, value ? 1U : 0U);
        }

        std::expected<std::uint64_t, Api::IDriver::Error> FrequencyToPeriodNs(Hertz frequency)
        {
            if (!std::isfinite(frequency.Value) || frequency.Value <= 0.0)
            {
                return std::unexpected(Api::IDriver::Error::InvalidArgument);
            }

            const auto periodNs = static_cast<std::uint64_t>(std::llround(OneSecondNs / frequency.Value));
            if (periodNs == 0U)
            {
                return std::unexpected(Api::IDriver::Error::InvalidArgument);
            }

            return periodNs;
        }

        std::uint64_t DutyToDutyCycleNs(const std::uint64_t periodNs, const NormalizedFraction dutyCycle)
        {
            const auto duty = static_cast<double>(dutyCycle);
            return static_cast<std::uint64_t>(std::llround(static_cast<double>(periodNs) * duty));
        }

        Api::IDriver::Error ApplySignal(
            const std::filesystem::path& pwmChannelPath,
            const std::uint64_t periodNs,
            const std::uint64_t dutyCycleNs,
            const bool enableAfterApply)
        {
            auto error = WriteBool(BuildNodePath(pwmChannelPath, "enable"), false);
            if (error != Api::IDriver::Error::Ok)
            {
                return error;
            }

            // Ensure period can always be set even if previous duty was larger.
            error = WriteUint64(BuildNodePath(pwmChannelPath, "duty_cycle"), 0U);
            if (error != Api::IDriver::Error::Ok)
            {
                return error;
            }

            error = WriteUint64(BuildNodePath(pwmChannelPath, "period"), periodNs);
            if (error != Api::IDriver::Error::Ok)
            {
                return error;
            }

            error = WriteUint64(BuildNodePath(pwmChannelPath, "duty_cycle"), dutyCycleNs);
            if (error != Api::IDriver::Error::Ok)
            {
                return error;
            }

            if (!enableAfterApply)
            {
                return Api::IDriver::Error::Ok;
            }

            return WriteBool(BuildNodePath(pwmChannelPath, "enable"), true);
        }
    }

    Driver::Driver(std::filesystem::path pwmChannelPath)
        : m_PwmChannelPath(std::move(pwmChannelPath))
    {
    }

    Api::IDriver::Error Driver::SetEnabled(const bool enabled)
    {
        const auto exportError = EnsureChannelExported(m_PwmChannelPath);
        if (exportError != Error::Ok)
        {
            return exportError;
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
                const auto readError = ReadUint64(BuildNodePath(m_PwmChannelPath, "period"), periodNs);
                if (readError != Error::Ok)
                {
                    return readError;
                }
            }

            std::uint64_t dutyCycleNs = 0U;
            if (m_StagedDutyCycleNs.has_value())
            {
                dutyCycleNs = m_StagedDutyCycleNs.value();
            }
            else
            {
                const auto readError = ReadUint64(BuildNodePath(m_PwmChannelPath, "duty_cycle"), dutyCycleNs);
                if (readError != Error::Ok)
                {
                    return readError;
                }
            }

            if (dutyCycleNs > periodNs)
            {
                return Error::InvalidArgument;
            }

            const auto error = ApplySignal(m_PwmChannelPath, periodNs, dutyCycleNs, true);
            if (error == Error::Ok)
            {
                m_StagedPeriodNs.reset();
                m_StagedDutyCycleNs.reset();
            }
            return error;
        }

        return WriteBool(BuildNodePath(m_PwmChannelPath, "enable"), false);
    }

    bool Driver::IsEnabled() const
    {
        const auto exportError = EnsureChannelExported(m_PwmChannelPath);
        if (exportError != Error::Ok)
        {
            return false;
        }

        std::uint64_t enabled = 0U;
        return ReadUint64(BuildNodePath(m_PwmChannelPath, "enable"), enabled) == Error::Ok && enabled != 0U;
    }

    std::expected<Hertz, Api::IDriver::Error> Driver::GetFrequency() const
    {
        std::uint64_t periodNs = 0U;
        if (m_StagedPeriodNs.has_value())
        {
            periodNs = m_StagedPeriodNs.value();
        }
        else
        {
            const auto exportError = EnsureChannelExported(m_PwmChannelPath);
            if (exportError != Error::Ok)
            {
                return std::unexpected(exportError);
            }

            const auto error = ReadUint64(BuildNodePath(m_PwmChannelPath, "period"), periodNs);
            if (error != Error::Ok)
            {
                return std::unexpected(error);
            }
        }

        if (periodNs == 0U)
        {
            return std::unexpected(Error::ProtocolError);
        }

        return Hertz(OneSecondNs / static_cast<double>(periodNs));
    }

    Api::IDriver::Error Driver::SetFrequency(const Hertz frequency)
    {
        auto periodNsExpected = FrequencyToPeriodNs(frequency);
        if (!periodNsExpected.has_value())
        {
            return periodNsExpected.error();
        }

        const auto periodNs = periodNsExpected.value();
        if (!IsEnabled())
        {
            if (m_StagedDutyCycleNs.has_value() && m_StagedDutyCycleNs.value() > periodNs)
            {
                return Error::InvalidArgument;
            }

            m_StagedPeriodNs = periodNs;
            return Error::Disabled;
        }

        auto dutyExpected = GetDutyCycle();
        if (!dutyExpected.has_value())
        {
            return dutyExpected.error();
        }

        return SetFrequencyAndDuty(frequency, dutyExpected.value());
    }

    std::expected<NormalizedFraction, Api::IDriver::Error> Driver::GetDutyCycle() const
    {
        std::uint64_t periodNs = 0U;
        if (m_StagedPeriodNs.has_value())
        {
            periodNs = m_StagedPeriodNs.value();
        }
        else
        {
            const auto periodError = ReadUint64(BuildNodePath(m_PwmChannelPath, "period"), periodNs);
            if (periodError != Error::Ok)
            {
                return std::unexpected(periodError);
            }
        }

        if (periodNs == 0U)
        {
            return std::unexpected(Error::ProtocolError);
        }

        std::uint64_t dutyCycleNs = 0U;
        if (m_StagedDutyCycleNs.has_value())
        {
            dutyCycleNs = m_StagedDutyCycleNs.value();
        }
        else
        {
            const auto dutyError = ReadUint64(BuildNodePath(m_PwmChannelPath, "duty_cycle"), dutyCycleNs);
            if (dutyError != Error::Ok)
            {
                return std::unexpected(dutyError);
            }
        }

        const auto rawDuty = static_cast<double>(dutyCycleNs) / static_cast<double>(periodNs);
        const auto clampedDuty = std::clamp(rawDuty, 0.0, 1.0);
        return NormalizedFraction(clampedDuty);
    }

    Api::IDriver::Error Driver::SetDutyCycle(const NormalizedFraction duty)
    {
        std::uint64_t periodNs = 0U;
        if (m_StagedPeriodNs.has_value())
        {
            periodNs = m_StagedPeriodNs.value();
        }
        else
        {
            const auto exportError = EnsureChannelExported(m_PwmChannelPath);
            if (exportError != Error::Ok)
            {
                return exportError;
            }

            const auto periodError = ReadUint64(BuildNodePath(m_PwmChannelPath, "period"), periodNs);
            if (periodError != Error::Ok)
            {
                return periodError;
            }
        }

        if (periodNs == 0U)
        {
            return Error::ProtocolError;
        }

        const auto dutyCycleNs = DutyToDutyCycleNs(periodNs, duty);
        if (!IsEnabled())
        {
            m_StagedDutyCycleNs = dutyCycleNs;
            return Error::Disabled;
        }

        return WriteUint64(BuildNodePath(m_PwmChannelPath, "duty_cycle"), dutyCycleNs);
    }

    Api::IDriver::Error Driver::SetFrequencyAndDuty(const Hertz frequency, const NormalizedFraction duty)
    {
        auto periodNsExpected = FrequencyToPeriodNs(frequency);
        if (!periodNsExpected.has_value())
        {
            return periodNsExpected.error();
        }

        const auto periodNs = periodNsExpected.value();
        const auto dutyCycleNs = DutyToDutyCycleNs(periodNs, duty);
        if (dutyCycleNs > periodNs)
        {
            return Error::InvalidArgument;
        }

        if (!IsEnabled())
        {
            m_StagedPeriodNs = periodNs;
            m_StagedDutyCycleNs = dutyCycleNs;
            return Error::Disabled;
        }

        const auto exportError = EnsureChannelExported(m_PwmChannelPath);
        if (exportError != Error::Ok)
        {
            return exportError;
        }

        return ApplySignal(m_PwmChannelPath, periodNs, dutyCycleNs, true);
    }

}
