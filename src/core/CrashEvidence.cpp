#include "CrashEvidence.h"

#include <csignal>
#include <cstdlib>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <initializer_list>

#if !defined(_WIN32)
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace lmms
{
namespace
{

#if !defined(_WIN32)
constexpr int BreadcrumbCount = 32;
constexpr int BreadcrumbSize = 128;
struct Breadcrumb
{
	volatile std::sig_atomic_t bytes[BreadcrumbSize]{};
	volatile std::sig_atomic_t committed = 0;
};

Breadcrumb breadcrumbs[BreadcrumbCount]{};
std::atomic<unsigned> nextBreadcrumb{0};
std::atomic<unsigned> breadcrumbSequence{0};
const char *evidencePath = nullptr;
int evidenceFd = -1;
bool ownsEvidenceFile = false;
volatile std::sig_atomic_t crashInProgress = 0;
struct sigaction previousActions[4]{};
bool handlersInstalled = false;

const int fatalSignals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGILL};

int signalIndex(int signalNumber)
{
	for (int index = 0; index < 4; ++index)
	{
		if (fatalSignals[index] == signalNumber) { return index; }
	}
	return -1;
}

void writeText(int fd, const char *text, std::size_t limit)
{
	if (!text) { return; }
	std::size_t length = 0;
	while (text[length] && length < limit) { ++length; }
	if (length > 0) { (void)write(fd, text, length); }
}

void crashHandler(int signalNumber)
{
	if (crashInProgress) { _exit(128 + signalNumber); }
	crashInProgress = 1;
	if (evidenceFd >= 0)
	{
		writeText(evidenceFd, "LMMS fatal signal; recent breadcrumbs:\n", 128);
		for (int index = 0; index < BreadcrumbCount; ++index)
		{
			const auto committed = breadcrumbs[index].committed;
			if (!committed) { continue; }
			char snapshot[BreadcrumbSize]{};
			for (int byte = 0; byte < BreadcrumbSize; ++byte)
			{
				snapshot[byte] = static_cast<char>(breadcrumbs[index].bytes[byte]);
			}
			if (committed == breadcrumbs[index].committed)
			{
				writeText(evidenceFd, snapshot, BreadcrumbSize);
				writeText(evidenceFd, "\n", 2);
			}
		}
	}
	const auto index = signalIndex(signalNumber);
	if (index >= 0) { sigaction(signalNumber, &previousActions[index], nullptr); }
	raise(signalNumber);
	_exit(128 + signalNumber);
}
#endif

} // namespace

void CrashEvidence::initialize()
{
#if !defined(_WIN32)
	evidencePath = std::getenv("LMMS_CRASH_PATH");
	if (!evidencePath || !*evidencePath) { return; }
	// Open once, before a crash. O_NONBLOCK means a FIFO never makes the
	// handler wait, and rejecting non-regular files avoids unsafe destinations.
	evidenceFd = open(evidencePath, O_WRONLY | O_CREAT | O_EXCL | O_APPEND |
		O_NONBLOCK | O_CLOEXEC, 0600);
	if (evidenceFd < 0) { return; }
	struct stat status{};
	if (fstat(evidenceFd, &status) != 0 || !S_ISREG(status.st_mode))
	{
		close(evidenceFd);
		evidenceFd = -1;
		return;
	}
	ownsEvidenceFile = true;
	struct sigaction action{};
	action.sa_handler = crashHandler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = SA_RESETHAND;
	for (int index = 0; index < 4; ++index)
	{
		sigaction(fatalSignals[index], &action, &previousActions[index]);
	}
	handlersInstalled = true;
#endif
}

void CrashEvidence::shutdown()
{
#if !defined(_WIN32)
	if (handlersInstalled)
	{
		for (int index = 0; index < 4; ++index)
		{
			sigaction(fatalSignals[index], &previousActions[index], nullptr);
		}
		handlersInstalled = false;
	}
	if (evidenceFd >= 0) { close(evidenceFd); evidenceFd = -1; }
	if (ownsEvidenceFile && evidencePath) { unlink(evidencePath); }
	ownsEvidenceFile = false;
#endif
}

void CrashEvidence::breadcrumb(const char *text)
{
#if !defined(_WIN32)
	const auto index = nextBreadcrumb.fetch_add(1, std::memory_order_relaxed) % BreadcrumbCount;
	const auto sequence = breadcrumbSequence.fetch_add(1, std::memory_order_relaxed) + 1;
	breadcrumbs[index].committed = 0;
	for (int byte = 0; byte < BreadcrumbSize; ++byte)
	{
		const char value = text && text[byte] ? text[byte] : '\0';
		breadcrumbs[index].bytes[byte] = static_cast<unsigned char>(value);
		if (!value) { break; }
	}
	breadcrumbs[index].committed = static_cast<std::sig_atomic_t>(sequence);
#else
	(void)text;
#endif
}

} // namespace lmms
