#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#include "PiSubmarine/PWM/Linux/ErrorCode.h"
#include "PiSubmarine/PWM/Linux/Driver.h"

namespace PiSubmarine::PWM::Linux
{
	namespace
	{
		class TemporaryDirectory final
		{
		public:
			TemporaryDirectory()
				: m_Path(
					std::filesystem::temp_directory_path()
					/ ("PiSubmarine.PWM.Linux.DriverTest."
						+ std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
			{
				std::filesystem::create_directories(m_Path);
			}

			~TemporaryDirectory()
			{
				std::error_code errorCode;
				std::filesystem::remove_all(m_Path, errorCode);
			}

			[[nodiscard]] const std::filesystem::path& Path() const noexcept
			{
				return m_Path;
			}

		private:
			std::filesystem::path m_Path;
		};

		[[nodiscard]] bool HasExportRequest(const std::filesystem::path& exportPath,
		                                    const std::string_view expectedChannelIndex)
		{
			std::ifstream exportFile(exportPath);
			std::string exportRequest;
			exportFile >> exportRequest;
			return exportRequest == expectedChannelIndex;
		}
	}

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

	TEST(DriverTest, SetEnabledWaitsForAllSysfsNodesAfterExport)
	{
		TemporaryDirectory temporaryDirectory;
		const auto chipPath = temporaryDirectory.Path() / "pwmchip0";
		const auto pwmChannelPath = chipPath / "pwm0";
		const auto exportPath = chipPath / "export";

		std::filesystem::create_directories(chipPath);
		std::ofstream(exportPath).close();

		std::jthread exportThread(
			[chipPath, pwmChannelPath, exportPath]
			{
				for (int attempt = 0; attempt < 100; ++attempt)
				{
					if (!HasExportRequest(exportPath, "0"))
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(10));
						continue;
					}

					std::filesystem::create_directories(pwmChannelPath);
					{
						std::ofstream enableFile(pwmChannelPath / "enable");
						enableFile << 0;
					}

					std::this_thread::sleep_for(std::chrono::milliseconds(150));

					{
						std::ofstream periodFile(pwmChannelPath / "period");
						periodFile << 20'000'000;
					}

					{
						std::ofstream dutyCycleFile(pwmChannelPath / "duty_cycle");
						dutyCycleFile << 0;
					}

					return;
				}
			});

		Driver driver(pwmChannelPath, std::chrono::milliseconds(10), 100U);

		const auto stageResult = driver.SetFrequencyAndDuty(Hertz(50.0), NormalizedFraction(0.5));
		ASSERT_TRUE(stageResult.has_value()) << stageResult.error().Cause.message();

		const auto enableResult = driver.SetEnabled(true);
		ASSERT_TRUE(enableResult.has_value()) << enableResult.error().Cause.message();

		std::ifstream enableFile(pwmChannelPath / "enable");
		std::uint64_t enableValue = 0;
		enableFile >> enableValue;
		EXPECT_EQ(enableValue, 1U);
	}
}
