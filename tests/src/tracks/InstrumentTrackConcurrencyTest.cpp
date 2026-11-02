/*
 * InstrumentTrackConcurrencyTest.cpp
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <atomic>
#include <chrono>
#include <thread>

#include <QtTest>

#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiEvent.h"
#include "NotePlayHandle.h"
#include "Song.h"

class InstrumentTrackConcurrencyTest : public QObject
{
	Q_OBJECT
private:
	std::atomic<int> m_periods = 0;

	// Lets the render thread adopt queued play handles before the test uses or destroys them
	bool waitForPeriods(int count)
	{
		const int target = m_periods + count;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (m_periods < target)
		{
			if (std::chrono::steady_clock::now() > deadline) { return false; }
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return true;
	}

private slots:
	void initTestCase()
	{
		lmms::NotePlayHandleManager::init();
		lmms::Engine::init(true);
		connect(lmms::Engine::audioEngine(), &lmms::AudioEngine::nextAudioBuffer,
			this, [this] { ++m_periods; }, Qt::DirectConnection);
	}

	void cleanupTestCase()
	{
		lmms::Engine::destroy();
	}

	// A MIDI note-off that is waiting for the audio engine while the track resets
	// its notes must not use the note the reset released (crash1).
	void noteOffWaitingForResetDoesNotUseReleasedNote()
	{
		using namespace lmms;

		for (int iteration = 0; iteration < 20; ++iteration)
		{
			InstrumentTrack track(Engine::getSong());
			track.processInEvent(MidiEvent(MidiNoteOn, 0, 60, 100));
			QVERIFY(waitForPeriods(2));

			std::thread noteOffThread;
			{
				// Hold the model-change lock so the note-off parks on it, as the
				// ALSA thread does while the GUI thread loads an instrument
				const auto guard = Engine::audioEngine()->requestChangesGuard();
				noteOffThread = std::thread([&track] {
					track.processInEvent(MidiEvent(MidiNoteOff, 0, 60, 0));
				});
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				track.silenceAllNotes();
			}
			noteOffThread.join();

			QVERIFY(waitForPeriods(2));
		}
	}
};

QTEST_GUILESS_MAIN(InstrumentTrackConcurrencyTest)

#include "InstrumentTrackConcurrencyTest.moc"
