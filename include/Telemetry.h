#pragma once

#include <cstdint>
#include <string>

namespace lmms
{

// Process-wide, opt-in telemetry for diagnostics. Producers must use the
// non-blocking record() path; serialization and file I/O happen elsewhere.
class Telemetry final
{
public:
	static void initialize();
	static void shutdown();
	static bool enabled();
	static bool initializationFailed();
	static const char *failureReason();
	static void record(const char *kind, const char *status, const char *message = nullptr,
					  const char *dataJson = nullptr);
	static std::uint64_t droppedEvents();
};

} // namespace lmms
