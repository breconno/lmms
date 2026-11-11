#include <QTest>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>
#include <unistd.h>

#include "Telemetry.h"

using namespace lmms;

class TelemetryTest : public QObject
{
	Q_OBJECT
	private slots:
	void concurrentWritersStayBoundedAndCategorized();
};

void TelemetryTest::concurrentWritersStayBoundedAndCategorized()
{
	const auto directory = std::filesystem::temp_directory_path() / ("lmms-telemetry-" + std::to_string(getpid()));
	std::filesystem::create_directories(directory);
	const auto path = directory / "events.jsonl";
	setenv("LMMS_TELEMETRY_PATH", path.c_str(), 1);
	setenv("LMMS_TELEMETRY_CATEGORIES", "lifecycle", 1);
	setenv("LMMS_TELEMETRY_PRIVACY", "redacted", 1);
	Telemetry::initialize();
	QVERIFY(Telemetry::enabled());

	std::vector<std::thread> writers;
	for (int thread = 0; thread < 8; ++thread)
	{
		writers.emplace_back([] {
			for (int event = 0; event < 1000; ++event)
			{
				Telemetry::record("midi.note_on", "running");
				Telemetry::record("track.lifecycle", "running", "bounded");
			}
		});
	}
	for (auto &writer : writers) { writer.join(); }
	Telemetry::shutdown();

	QVERIFY(Telemetry::droppedEvents() > 0);
	QVERIFY(std::filesystem::file_size(path) <= 4 * 1024 * 1024);
	for (const auto &entry : std::filesystem::directory_iterator(directory))
	{
		if (entry.path().filename().string().find("events.jsonl") == 0)
		{
			QVERIFY(entry.file_size() <= 4 * 1024 * 1024);
			std::ifstream input(entry.path());
			for (std::string line; std::getline(input, line);) {
				QVERIFY(line.find("midi.note_on") == std::string::npos);
				QVERIFY(line.find("bounded") == std::string::npos);
			}
		}
	}
	std::filesystem::remove_all(directory);
}

QTEST_GUILESS_MAIN(TelemetryTest)
#include "TelemetryTest.moc"
