#pragma once

#include <system_error>

namespace PiSubmarine::PWM::Linux
{
	enum class ErrorCode
	{
		InvalidChannelPath = 1,
		ChannelIndexParseFailed,
		ExportNodeOpenFailed,
		ExportWriteFailed,
		ExportTimedOut,
		NodeOpenFailed,
		NodeWriteFailed,
		InvalidNodeValue,
		NonPositiveFrequency,
		UnrepresentableFrequency,
		ZeroPeriod,
		DutyCycleExceedsPeriod
	};

	[[nodiscard]] std::error_code make_error_code(ErrorCode errorCode) noexcept;
}

namespace std
{
	template<>
	struct is_error_code_enum<PiSubmarine::PWM::Linux::ErrorCode> : true_type
	{
	};
}
