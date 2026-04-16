#include "PiSubmarine/PWM/Linux/ErrorCode.h"

#include <string_view>

namespace PiSubmarine::PWM::Linux
{
	namespace
	{
		class ErrorCategory final : public std::error_category
		{
		public:
			[[nodiscard]] const char* name() const noexcept override
			{
				return "PiSubmarine.PWM.Linux";
			}

			[[nodiscard]] std::string message(const int condition) const override
			{
				switch (static_cast<ErrorCode>(condition))
				{
				case ErrorCode::InvalidChannelPath:
					return "invalid PWM channel path";

				case ErrorCode::ChannelIndexParseFailed:
					return "failed to parse PWM channel index";

				case ErrorCode::ExportNodeOpenFailed:
					return "failed to open PWM export node";

				case ErrorCode::ExportWriteFailed:
					return "failed to write PWM export request";

				case ErrorCode::ExportTimedOut:
					return "timed out while waiting for PWM channel export";

				case ErrorCode::NodeOpenFailed:
					return "failed to open PWM sysfs node";

				case ErrorCode::NodeWriteFailed:
					return "failed to write PWM sysfs node";

				case ErrorCode::InvalidNodeValue:
					return "PWM sysfs node contains an invalid value";

				case ErrorCode::NonPositiveFrequency:
					return "PWM frequency must be positive";

				case ErrorCode::UnrepresentableFrequency:
					return "PWM frequency cannot be represented as a non-zero period";

				case ErrorCode::ZeroPeriod:
					return "PWM period cannot be zero";

				case ErrorCode::DutyCycleExceedsPeriod:
					return "PWM duty cycle exceeds the configured period";
				}

				return "unknown PWM Linux error";
			}
		};

		[[nodiscard]] const std::error_category& GetErrorCategory() noexcept
		{
			static const ErrorCategory Category;
			return Category;
		}
	}

	[[nodiscard]] std::error_code make_error_code(const ErrorCode errorCode) noexcept
	{
		return {static_cast<int>(errorCode), GetErrorCategory()};
	}
}
