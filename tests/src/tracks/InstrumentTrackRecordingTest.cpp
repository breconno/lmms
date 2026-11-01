/*
 * InstrumentTrackRecordingTest.cpp
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
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include <QObject>
#include <QSignalSpy>
#include <QtTest>

#include "Engine.h"
#include "InstrumentTrack.h"
#include "NotePlayHandle.h"
#include "Song.h"

namespace
{

using namespace lmms;

constexpr int Bar = DefaultTicksPerBar;
constexpr int Sixteenth = DefaultTicksPerBar / 16;

Song* song() { return Engine::getSong(); }

InstrumentTrack* addTrack(bool armed)
{
	auto track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song()));
	track->setRecordArmed(armed);
	return track;
}

} // namespace

class InstrumentTrackRecordingTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		// main() does this in the application
		NotePlayHandleManager::init();
		Engine::init(true);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void init()
	{
		song()->clearProject();
	}

	// R1
	void ExternalNotesAreSignalledInternalAreNot()
	{
		auto track = addTrack(false);
		QSignalSpy noteOn(track, &InstrumentTrack::externalNoteOn);
		QSignalSpy noteOff(track, &InstrumentTrack::externalNoteOff);
		auto keys = std::vector<int>{};
		connect(track, &InstrumentTrack::externalNoteOn, this, [&keys](const Note& note) { keys.push_back(note.key()); });

		track->processInEvent(MidiEvent(MidiNoteOn, 0, 60, 100));
		track->processInEvent(MidiEvent(MidiNoteOff, 0, 60, 0));
		QCOMPARE(noteOn.count(), 1);
		QCOMPARE(noteOff.count(), 1);
		QCOMPARE(keys, std::vector<int>{60});

		track->processInEvent(MidiEvent(MidiNoteOn, 0, 62, 100, nullptr, MidiEvent::Source::Internal));
		track->processInEvent(MidiEvent(MidiNoteOff, 0, 62, 0, nullptr, MidiEvent::Source::Internal));
		QCOMPARE(noteOn.count(), 1);
		QCOMPARE(noteOff.count(), 1);

		// Notes played back from clips are not live input
		NotePlayHandle playback(track, 0, 1000, Note(), nullptr, -1, NotePlayHandle::Origin::MidiClip);
		playback.noteOff(0);
		QCOMPARE(noteOn.count(), 1);
		QCOMPARE(noteOff.count(), 1);
	}

	void ExternalNoteOffAfterSustainRelease()
	{
		auto track = addTrack(false);
		QSignalSpy noteOff(track, &InstrumentTrack::externalNoteOff);

		track->processInEvent(MidiEvent(MidiControlChange, 0, MidiControllerSustain, 127));
		track->processInEvent(MidiEvent(MidiNoteOn, 0, 60, 100));
		track->processInEvent(MidiEvent(MidiNoteOff, 0, 60, 0));
		QCOMPARE(noteOff.count(), 0);
		track->processInEvent(MidiEvent(MidiControlChange, 0, MidiControllerSustain, 0));
		QCOMPARE(noteOff.count(), 1);
	}

	void AboutToStopCarriesSongPosition()
	{
		auto positions = std::vector<int>{};
		const auto connection = connect(song(), &Song::aboutToStop, this,
			[&positions](TimePos stopPos) { positions.push_back(stopPos.getTicks()); });
		song()->getTimeline(Song::PlayMode::Song).setTicks(3 * Bar + 5);
		song()->playSong();
		song()->getTimeline(Song::PlayMode::Song).setTicks(4 * Bar);
		song()->stop();
		disconnect(connection);
		// The audio thread may advance the playhead a little before stop() reads it
		QCOMPARE(positions.size(), std::size_t{1});
		QVERIFY(positions[0] >= 4 * Bar);
		QVERIFY(positions[0] < 4 * Bar + Sixteenth);
	}

	// R12
	void ArmingSignalsOnlyOnChange()
	{
		auto track = addTrack(false);
		QSignalSpy armChanged(track, &InstrumentTrack::recordArmChanged);
		track->setRecordArmed(true);
		track->setRecordArmed(true);
		QVERIFY(track->isRecordArmed());
		track->setRecordArmed(false);
		QVERIFY(!track->isRecordArmed());
		QCOMPARE(armChanged.count(), 2);
	}
};

QTEST_GUILESS_MAIN(InstrumentTrackRecordingTest)

#include "InstrumentTrackRecordingTest.moc"
