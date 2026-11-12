#include "Telemetry.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#include <process.h>
#define LMMS_GETPID _getpid
#else
#include <unistd.h>
#define LMMS_GETPID getpid
#endif

namespace lmms
{
namespace
{

constexpr std::size_t QueueSize = 256;
constexpr std::size_t TextSize = 192;
constexpr std::uintmax_t MaxLogBytes = 4 * 1024 * 1024;
constexpr int MaxRotatedLogs = 2;

struct Event
{
	std::uint64_t sequence = 0;
	std::int64_t timestampMicros = 0;
	char kind[TextSize]{};
	char status[32]{};
	char message[TextSize]{};
	char data[TextSize]{};
};

std::array<Event, QueueSize> queue;
std::array<std::atomic<bool>, QueueSize> ready{};
std::atomic<std::uint64_t> writeIndex{0};
std::atomic<std::uint64_t> readIndex{0};
std::atomic<std::uint64_t> sequence{0};
std::atomic<std::uint64_t> dropped{0};
std::atomic<bool> running{false};
std::atomic<bool> configured{false};
std::atomic<bool> writeFailed{false};
std::thread writer;
std::ofstream output;
std::string runId;
std::string outputPath;
std::uintmax_t outputBytes = 0;
bool lifecycleEnabled = true;
bool notesEnabled = false;
bool diagnosticPayloadEnabled = false;
std::atomic<bool> failure{false};
std::atomic<const char *> failureMessage{nullptr};

void copyText(char *destination, const char *source, std::size_t size)
{
	if (!source) { destination[0] = '\0'; return; }
	std::strncpy(destination, source, size - 1);
	destination[size - 1] = '\0';
}

std::string escaped(const char *text)
{
	std::string result;
	if (!text) { return result; }
	for (const auto *cursor = text; *cursor; ++cursor)
	{
		switch (*cursor)
		{
		case '"': result += "\\\""; break;
		case '\\': result += "\\\\"; break;
		case '\n': result += "\\n"; break;
		case '\r': result += "\\r"; break;
		case '\t': result += "\\t"; break;
		default: result += *cursor; break;
		}
	}
	return result;
}

std::string timestamp(std::int64_t microsSinceEpoch)
{
	const auto seconds = static_cast<std::time_t>(microsSinceEpoch / 1000000);
	const auto micros = microsSinceEpoch % 1000000;
	std::tm utc{};
#if defined(_WIN32)
	gmtime_s(&utc, &seconds);
#else
	gmtime_r(&seconds, &utc);
#endif
	char buffer[64];
	std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%06lld+00:00",
		utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min,
		utc.tm_sec, static_cast<long long>(micros));
	return buffer;
}

void reportWriteFailure(const char *operation)
{
	failure.store(true, std::memory_order_release);
	failureMessage.store(operation, std::memory_order_release);
	if (!writeFailed.exchange(true))
	{
		std::fprintf(stderr,
			"LMMS telemetry: %s failed; events may be unavailable\n", operation);
	}
}

void rotateLogs()
{
	if (output.is_open()) { output.close(); }
	std::error_code error;
	for (int index = MaxRotatedLogs; index >= 1; --index)
	{
		const auto from = outputPath + "." + std::to_string(index);
		const auto to = outputPath + "." + std::to_string(index + 1);
		std::filesystem::remove(to, error);
		std::filesystem::rename(from, to, error);
	}
	std::filesystem::rename(outputPath, outputPath + ".1", error);
	output.clear();
	output.open(outputPath, std::ios::out | std::ios::trunc);
	outputBytes = 0;
	if (!output.is_open()) { reportWriteFailure("log rotation"); }
}

void writeEvent(const Event &event)
{
	if (!output.is_open()) { reportWriteFailure("open"); return; }
	std::ostringstream line;
	const auto time = timestamp(event.timestampMicros);
	line << "{\"schema\":\"lmms.orchestration.event\",\"schema_version\":\"0.1\","
		<< "\"event_id\":\"lmms-" << LMMS_GETPID() << "-" << event.sequence << "\","
		<< "\"sequence\":" << event.sequence << ",\"timestamp\":\"" << time
		<< "\",\"timestamp_utc\":\"" << time << "\",\"run_id\":\""
		<< escaped(runId.c_str()) << "\",\"source\":\"lmms\",\"target\":\"lmms\","
		<< "\"host\":\"local\",\"stage\":\"runtime\",\"action\":\""
		<< escaped(event.kind) << "\",\"event\":\"" << escaped(event.kind)
		<< "\",\"kind\":\"app.event\",\"status\":\""
		<< escaped(event.status) << "\",\"message\":\"" << escaped(event.message)
		<< "\",\"data\":" << (event.data[0] ? event.data : "{}") << "}\n";
	const auto serialized = line.str();
	if (outputBytes + serialized.size() > MaxLogBytes)
	{
		rotateLogs();
		if (!output.is_open()) { return; }
	}
	output << serialized;
	output.flush();
	if (!output.good()) { reportWriteFailure("write"); }
	else { outputBytes += serialized.size(); }
}

void drain()
{
	while (running.load(std::memory_order_acquire) ||
		readIndex.load(std::memory_order_relaxed) < writeIndex.load(std::memory_order_acquire))
	{
		const auto read = readIndex.load(std::memory_order_relaxed);
		const auto write = writeIndex.load(std::memory_order_acquire);
		if (read == write)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			continue;
		}
		if (!ready[read % QueueSize].load(std::memory_order_acquire))
		{
			std::this_thread::yield();
			continue;
		}
		writeEvent(queue[read % QueueSize]);
		ready[read % QueueSize].store(false, std::memory_order_release);
		readIndex.store(read + 1, std::memory_order_release);
	}
}

} // namespace

void Telemetry::initialize()
{
	if (configured.exchange(true)) { return; }
	const char *path = std::getenv("LMMS_TELEMETRY_PATH");
	if (!path || !*path) { return; }
	const char *categories = std::getenv("LMMS_TELEMETRY_CATEGORIES");
	const bool explicitCategories = categories && *categories;
	const char *privacy = std::getenv("LMMS_TELEMETRY_PRIVACY");
	diagnosticPayloadEnabled = privacy && std::strcmp(privacy, "full") == 0;
	lifecycleEnabled = !explicitCategories;
	notesEnabled = false;
	if (explicitCategories)
	{
		for (const char *cursor = categories; *cursor; )
		{
			const char *end = std::strchr(cursor, ',');
			const std::size_t length = end ? static_cast<std::size_t>(end - cursor) : std::strlen(cursor);
			if (length == 9 && std::strncmp(cursor, "lifecycle", length) == 0) { lifecycleEnabled = true; }
			if (length == 4 && std::strncmp(cursor, "midi", length) == 0) { notesEnabled = true; }
			if (!end) { break; }
			cursor = end + 1;
		}
	}
	// Keep the legacy switches as compatibility aliases, but never enable
	// high-volume MIDI data unless it was explicitly requested.
	if (!explicitCategories)
	{
		if (const char *lifecycle = std::getenv("LMMS_TELEMETRY_LIFECYCLE"))
		{
			lifecycleEnabled = std::strcmp(lifecycle, "0") != 0;
		}
	}
	if (!explicitCategories)
	{
		if (const char *notes = std::getenv("LMMS_TELEMETRY_NOTES"))
		{
			notesEnabled = std::strcmp(notes, "0") != 0;
		}
	}
	const char *configuredRun = std::getenv("LMMS_RUN_ID");
	runId = configuredRun && *configuredRun ? configuredRun :
		("lmms-" + std::to_string(LMMS_GETPID()));
	outputPath = path;
	std::error_code error;
	if (std::filesystem::exists(outputPath, error) &&
		std::filesystem::file_size(outputPath, error) >= MaxLogBytes)
	{
		rotateLogs();
	}
	output.open(outputPath, std::ios::out | std::ios::app);
	if (!output.is_open()) { reportWriteFailure("open"); return; }
	outputBytes = std::filesystem::file_size(outputPath, error);
	running.store(true, std::memory_order_release);
	writer = std::thread(drain);
	record("process.started", "running", "LMMS telemetry enabled");
}

void Telemetry::shutdown()
{
	if (!running.load(std::memory_order_acquire)) { return; }
	char summary[64];
	std::snprintf(summary, sizeof(summary), "{\"dropped_events\":%llu}",
		static_cast<unsigned long long>(droppedEvents()));
	record("process.stopped", "success", "LMMS telemetry stopped", summary);
	running.store(false, std::memory_order_release);
	if (writer.joinable()) { writer.join(); }
	if (output.is_open()) { output.flush(); output.close(); }
}

bool Telemetry::enabled()
{
	return running.load(std::memory_order_acquire);
}

bool Telemetry::initializationFailed()
{
	return failure.load(std::memory_order_acquire);
}

const char *Telemetry::failureReason()
{
	const auto message = failureMessage.load(std::memory_order_acquire);
	return message ? message : "";
}

void Telemetry::record(const char *kind, const char *status, const char *message,
	const char *dataJson)
{
	if (!enabled()) { return; }
	const bool isNote = kind && std::strncmp(kind, "midi.", 5) == 0;
	if ((isNote && !notesEnabled) || (!isNote && !lifecycleEnabled)) { return; }
	auto write = writeIndex.load(std::memory_order_relaxed);
	for (int attempts = 0; attempts < 4; ++attempts)
	{
		const auto read = readIndex.load(std::memory_order_acquire);
		if (write - read >= QueueSize)
		{
			dropped.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (writeIndex.compare_exchange_weak(write, write + 1,
			std::memory_order_acq_rel, std::memory_order_relaxed)) { break; }
		if (attempts == 3)
		{
			dropped.fetch_add(1, std::memory_order_relaxed);
			return;
		}
	}
	if (writeIndex.load(std::memory_order_relaxed) <= write) { return; }
	Event &event = queue[write % QueueSize];
	event.sequence = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
	event.timestampMicros = std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	copyText(event.kind, kind, sizeof(event.kind));
	copyText(event.status, status, sizeof(event.status));
	copyText(event.message, diagnosticPayloadEnabled ? message : nullptr, sizeof(event.message));
	copyText(event.data, diagnosticPayloadEnabled ? dataJson : nullptr, sizeof(event.data));
	ready[write % QueueSize].store(true, std::memory_order_release);
}

std::uint64_t Telemetry::droppedEvents()
{
	return dropped.load(std::memory_order_relaxed);
}

} // namespace lmms
