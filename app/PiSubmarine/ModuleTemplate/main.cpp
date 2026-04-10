#include <exception>
#include <iostream>
#include <string>

#include <boost/program_options.hpp>
#include <spdlog/spdlog.h>

#include "PiSubmarine/PWM/Linux/Driver.h"

namespace po = boost::program_options;

namespace
{
    const char* ToString(const PiSubmarine::PWM::Api::IDriver::Error error)
    {
        using Error = PiSubmarine::PWM::Api::IDriver::Error;
        switch (error)
        {
        case Error::Ok:
            return "Ok";
        case Error::Disabled:
            return "Disabled";
        case Error::InvalidArgument:
            return "InvalidArgument";
        case Error::ProtocolError:
            return "ProtocolError";
        case Error::UnsupportedParameter:
            return "UnsupportedParameter";
        case Error::Busy:
            return "Busy";
        case Error::IoFailure:
            return "IoFailure";
        case Error::UnknownError:
            return "UnknownError";
        }

        return "UnknownError";
    }
}

int main(int argc, char* argv[])
{
    try
    {
        std::string pwmPath;
        int enableValue = 0;
        double frequencyHzValue = 0.0;
        double dutyValue = 0.0;

        po::options_description options("PiSubmarine PWM Linux CLI options");
        options.add_options()
            ("help,h", "Show help")
            ("pwm-path,p", po::value<std::string>(&pwmPath)->required(), "Path to PWM channel sysfs dir (e.g. /sys/class/pwm/pwmchip0/pwm0)")
            ("enable,e", po::value<int>(&enableValue), "Enable state: 1 to enable, 0 to disable")
            ("frequency-hz,f", po::value<double>(&frequencyHzValue), "Requested PWM frequency in Hz")
            ("duty,d", po::value<double>(&dutyValue), "Requested duty cycle in [0.0, 1.0]");

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, options), vm);

        if (vm.contains("help"))
        {
            std::cout << options << '\n';
            return 0;
        }

        po::notify(vm);

        const bool hasEnable = vm.contains("enable");
        const bool hasFrequency = vm.contains("frequency-hz");
        const bool hasDuty = vm.contains("duty");

        if (!hasEnable && !hasFrequency && !hasDuty)
        {
            spdlog::error("No action requested. Provide at least one of: --enable, --frequency-hz, --duty.");
            return 2;
        }

        if (hasEnable && enableValue != 0 && enableValue != 1)
        {
            spdlog::error("--enable must be 0 or 1, got {}", enableValue);
            return 2;
        }

        PiSubmarine::PWM::Linux::Driver driver(pwmPath);

        if (hasFrequency && hasDuty)
        {
            const auto error = driver.SetFrequencyAndDuty(
                PiSubmarine::Hertz(frequencyHzValue),
                PiSubmarine::NormalizedFraction(dutyValue));
            spdlog::info("SetFrequencyAndDuty({}, {}) -> {}", frequencyHzValue, dutyValue, ToString(error));
        }
        else
        {
            if (hasFrequency)
            {
                const auto error = driver.SetFrequency(PiSubmarine::Hertz(frequencyHzValue));
                spdlog::info("SetFrequency({}) -> {}", frequencyHzValue, ToString(error));
            }

            if (hasDuty)
            {
                const auto error = driver.SetDutyCycle(PiSubmarine::NormalizedFraction(dutyValue));
                spdlog::info("SetDutyCycle({}) -> {}", dutyValue, ToString(error));
            }
        }

        if (hasEnable)
        {
            const auto error = driver.SetEnabled(enableValue != 0);
            spdlog::info("SetEnabled({}) -> {}", enableValue, ToString(error));
        }

        const auto frequency = driver.GetFrequency();
        if (frequency.has_value())
        {
            spdlog::info("Effective frequency: {} Hz", frequency->Value);
        }
        else
        {
            spdlog::warn("GetFrequency failed: {}", ToString(frequency.error()));
        }

        const auto dutyCycle = driver.GetDutyCycle();
        if (dutyCycle.has_value())
        {
            spdlog::info("Effective duty: {}", static_cast<double>(dutyCycle.value()));
        }
        else
        {
            spdlog::warn("GetDutyCycle failed: {}", ToString(dutyCycle.error()));
        }

        spdlog::info("Enabled: {}", driver.IsEnabled() ? "true" : "false");
        return 0;
    }
    catch (const po::error& ex)
    {
        spdlog::error("Argument error: {}", ex.what());
        return 2;
    }
    catch (const std::exception& ex)
    {
        spdlog::error("Unhandled exception: {}", ex.what());
        return 1;
    }
}
