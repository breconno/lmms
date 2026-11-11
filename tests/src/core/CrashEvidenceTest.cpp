#include <QTest>

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/wait.h>
#include <unistd.h>

#include "CrashEvidence.h"

using namespace lmms;

class CrashEvidenceTest : public QObject
{
	Q_OBJECT
	private slots:
	void crashEvidenceIsBoundedAndCorrelated();
};

void CrashEvidenceTest::crashEvidenceIsBoundedAndCorrelated()
{
	const auto path = std::filesystem::temp_directory_path() / ("lmms-crash-" + std::to_string(getpid()));
	std::filesystem::remove(path);
	const auto pid = fork();
	QVERIFY(pid >= 0);
	if (pid == 0)
	{
		setenv("LMMS_CRASH_PATH", path.c_str(), 1);
		setenv("LMMS_RUN_ID", "test-run-6", 1);
		CrashEvidence::initialize();
		CrashEvidence::breadcrumb("midi.note_on");
		CrashEvidence::breadcrumb("take.stop");
		raise(SIGSEGV);
		_exit(99);
	}
	int status = 0;
	QVERIFY(waitpid(pid, &status, 0) == pid);
	QVERIFY(WIFSIGNALED(status));
	QCOMPARE(WTERMSIG(status), SIGSEGV);
	std::ifstream input(path);
	const std::string evidence((std::istreambuf_iterator<char>(input)), {});
	QVERIFY(evidence.find("LMMS fatal signal=11") != std::string::npos);
	QVERIFY(evidence.find("run_id=test-run-6") != std::string::npos);
	QVERIFY(evidence.find("telemetry_event_id_prefix=lmms-") != std::string::npos);
	QVERIFY(evidence.find("midi.note_on") != std::string::npos);
	QVERIFY(evidence.size() <= 32 * 128 + 512);
	std::filesystem::remove(path);
}

QTEST_GUILESS_MAIN(CrashEvidenceTest)
#include "CrashEvidenceTest.moc"
