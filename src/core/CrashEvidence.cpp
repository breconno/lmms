#include "CrashEvidence.h"

#include <csignal>
#include <cstdlib>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <initializer_list>
#include <cstdio>
#include <cstdint>

#if !defined(_WIN32)
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <ucontext.h>
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
int evidenceFd = -1;
bool ownsEvidenceFile = false;
volatile std::sig_atomic_t crashInProgress = 0;
struct sigaction previousActions[4]{};
bool handlersInstalled = false;
stack_t alternateStack{};
// SIGSTKSZ is runtime-sized on some libc versions, so use a conservative
// fixed stack that is large enough for the bounded handler below.
alignas(16) unsigned char alternateStackMemory[64 * 1024]{};
const char *runId = nullptr;

const int fatalSignals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGILL};

void writeText(int fd, const char *text, std::size_t limit)
{
	if (!text) { return; }
	std::size_t length = 0;
	while (text[length] && length < limit) { ++length; }
	if (length > 0)
	{
		const auto result = write(fd, text, length);
		(void)result;
	}
}

void writeUnsigned(int fd, const char *label, unsigned long long value, int base)
{
	char digits[32];
	char output[64];
	std::size_t count = 0;
	do { digits[count++] = "0123456789abcdef"[value % base]; value /= base; } while (value && count < sizeof(digits));
	std::size_t out = 0;
	for (const char *cursor = label; cursor && *cursor && out < sizeof(output); ++cursor) { output[out++] = *cursor; }
	while (count && out < sizeof(output)) { output[out++] = digits[--count]; }
	if (out)
	{
		const auto result = write(fd, output, out);
		(void)result;
	}
}

void writeSignalAddress(int fd, const siginfo_t *info)
{
	writeText(fd, " address=0x", 32);
	writeUnsigned(fd, nullptr, reinterpret_cast<std::uintptr_t>(info ? info->si_addr : nullptr), 16);
	writeText(fd, "\n", 2);
}

void crashHandler(int signalNumber, siginfo_t *info, void *)
{
	if (crashInProgress) { _exit(128 + signalNumber); }
	crashInProgress = 1;
	if (evidenceFd >= 0)
	{
		writeText(evidenceFd, "LMMS fatal signal=", 64);
		writeUnsigned(evidenceFd, nullptr, static_cast<unsigned long long>(signalNumber), 10);
		writeSignalAddress(evidenceFd, info);
		writeUnsigned(evidenceFd, "pid=", static_cast<unsigned long long>(getpid()), 10);
#ifdef SYS_gettid
		writeUnsigned(evidenceFd, " tid=", static_cast<unsigned long long>(syscall(SYS_gettid)), 10);
#endif
		if (runId && *runId)
		{
			writeText(evidenceFd, " run_id=", 32);
			writeText(evidenceFd, runId, 128);
		}
		writeText(evidenceFd, "\ntelemetry_event_id_prefix=lmms-", 64);
		writeUnsigned(evidenceFd, nullptr, static_cast<unsigned long long>(getpid()), 10);
		writeText(evidenceFd, "-\nrecent breadcrumbs:\n", 32);
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
	// Do not invoke a prior application/Qt handler here: it may return and turn
	// a fatal signal into a normal exit. Restore the default disposition before
	// re-raising so the kernel records the real terminating signal and core.
	struct sigaction defaultAction{};
	defaultAction.sa_handler = SIG_DFL;
	sigemptyset(&defaultAction.sa_mask);
	(void)sigaction(signalNumber, &defaultAction, nullptr);
	sigset_t unblock;
	sigemptyset(&unblock);
	sigaddset(&unblock, signalNumber);
	(void)sigprocmask(SIG_UNBLOCK, &unblock, nullptr);
	(void)kill(getpid(), signalNumber);
	_exit(128 + signalNumber);
}
#endif

} // namespace

void CrashEvidence::initialize()
{
#if !defined(_WIN32)
	const char *evidencePath = std::getenv("LMMS_CRASH_PATH");
	if (!evidencePath || !*evidencePath) { return; }
	runId = std::getenv("LMMS_RUN_ID");
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
	alternateStack.ss_sp = alternateStackMemory;
	alternateStack.ss_size = sizeof(alternateStackMemory);
	alternateStack.ss_flags = 0;
	if (sigaltstack(&alternateStack, nullptr) != 0)
	{
		close(evidenceFd); evidenceFd = -1; return;
	}
	struct sigaction action{};
	action.sa_sigaction = crashHandler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
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
	// Keep crash evidence after a normal shutdown only when it was not created
	// by this process. A clean run must not leave a false-positive artifact.
	if (ownsEvidenceFile)
	{
		const char *path = std::getenv("LMMS_CRASH_PATH");
		if (path) { unlink(path); }
	}
	ownsEvidenceFile = false;
#endif
}

bool CrashEvidence::enabled()
{
#if !defined(_WIN32)
	return evidenceFd >= 0;
#else
	return false;
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
