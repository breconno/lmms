/*
 * MidiTakeBufferTest.cpp
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
#include <QtTest>
#include <vector>

#include "MidiTakeBuffer.h"

namespace
{

using lmms::DefaultTicksPerBar;
using lmms::MidiTakeBuffer;
using lmms::Note;
using lmms::TimePos;

constexpr int Bar = DefaultTicksPerBar;
constexpr int Sixteenth = DefaultTicksPerBar / 16;

//! A note as delivered by InstrumentTrack: song position and, at note-off, length
Note liveNote(int key, int songPos, int length = 0, lmms::volume_t volume = 90)
{
	return Note(TimePos(length), TimePos(songPos), key, volume);
}

struct Recorded
{
	std::vector<Note> notes;

	void attach(MidiTakeBuffer& buffer)
	{
		buffer.setCommitFunction([this](const Note& note) { notes.push_back(note); });
	}
};

} // namespace

class MidiTakeBufferTest : public QObject
{
	Q_OBJECT
private slots:
	// U1
	void AcceptsFromRecordStartOnly()
	{
		auto buffer = MidiTakeBuffer{};
		auto recorded = Recorded{};
		recorded.attach(buffer);
		buffer.begin(2 * Bar, false, Sixteenth);

		QVERIFY(!buffer.noteOn(liveNote(60, 2 * Bar - 1)));
		QVERIFY(buffer.noteOn(liveNote(62, 2 * Bar)));
		QCOMPARE(buffer.pendingCount(), std::size_t{1});

		QVERIFY(!buffer.noteOff(liveNote(60, 2 * Bar - 1, 10)));
		QVERIFY(buffer.noteOff(liveNote(62, 2 * Bar, 10)));
		QCOMPARE(recorded.notes.size(), std::size_t{1});
		QCOMPARE(recorded.notes[0].key(), 62);
	}

	// U2
	void DropsPendingNoteAtRecordStop()
	{
		auto buffer = MidiTakeBuffer{};
		auto recorded = Recorded{};
		recorded.attach(buffer);
		buffer.begin(0, false, Sixteenth);

		QVERIFY(buffer.noteOn(liveNote(60, Bar)));
		buffer.finish(Bar);
		QVERIFY(recorded.notes.empty());
		QVERIFY(!buffer.noteOn(liveNote(61, Bar + 1)));
	}

	// U3
	void MatchesNoteOffByKeyAndStart()
	{
		auto buffer = MidiTakeBuffer{};
		auto recorded = Recorded{};
		recorded.attach(buffer);
		buffer.begin(0, false, Sixteenth);

		// Same key struck twice while the sustain pedal is held
		QVERIFY(buffer.noteOn(liveNote(60, 10)));
		QVERIFY(buffer.noteOn(liveNote(60, 50)));
		// Pedal release delivers the later note first
		QVERIFY(buffer.noteOff(liveNote(60, 50, 30)));
		QVERIFY(buffer.noteOff(liveNote(60, 10, 70)));

		QCOMPARE(recorded.notes.size(), std::size_t{2});
		QCOMPARE(recorded.notes[0].pos().getTicks(), 50);
		QCOMPARE(recorded.notes[0].length().getTicks(), 30);
		QCOMPARE(recorded.notes[1].pos().getTicks(), 10);
		QCOMPARE(recorded.notes[1].length().getTicks(), 70);
	}

	// U4
	void IgnoresUnmatchedNoteOff()
	{
		auto buffer = MidiTakeBuffer{};
		auto recorded = Recorded{};
		recorded.attach(buffer);
		buffer.begin(0, false, Sixteenth);

		QVERIFY(buffer.noteOn(liveNote(60, 10)));
		QVERIFY(!buffer.noteOff(liveNote(61, 10, 5)));
		QVERIFY(!buffer.noteOff(liveNote(60, 11, 5)));
		QVERIFY(recorded.notes.empty());
		QCOMPARE(buffer.pendingCount(), std::size_t{1});
	}

	// U5
	void ClampsHeldNoteToStop()
	{
		auto buffer = MidiTakeBuffer{};
		auto recorded = Recorded{};
		recorded.attach(buffer);
		buffer.begin(0, false, Sixteenth);

		QVERIFY(buffer.noteOn(liveNote(60, 100, 0, 77)));
		QVERIFY(buffer.noteOn(liveNote(62, 149)));
		buffer.finish(150);

		QCOMPARE(recorded.notes.size(), std::size_t{2});
		QCOMPARE(recorded.notes[0].pos().getTicks(), 100);
		QCOMPARE(recorded.notes[0].length().getTicks(), 50);
		QCOMPARE(static_cast<int>(recorded.notes[0].getVolume()), 77);
		QCOMPARE(recorded.notes[1].pos().getTicks(), 149);
		QCOMPARE(recorded.notes[1].length().getTicks(), 1);
	}

	// U6
	void QuantizedStartAtStopMovesBeforeStop()
	{
		auto buffer = MidiTakeBuffer{};
		auto recorded = Recorded{};
		recorded.attach(buffer);
		buffer.begin(0, true, Sixteenth);

		// Rounds up to 2 * Sixteenth, which is the stop position
		QVERIFY(buffer.noteOn(liveNote(60, 2 * Sixteenth - 2)));
		buffer.finish(2 * Sixteenth);

		QCOMPARE(recorded.notes.size(), std::size_t{1});
		QCOMPARE(recorded.notes[0].pos().getTicks(), 2 * Sixteenth - 1);
		QCOMPARE(recorded.notes[0].length().getTicks(), 1);
	}

	// U7
	void CommitsOnlyOnce()
	{
		auto buffer = MidiTakeBuffer{};
		auto recorded = Recorded{};
		recorded.attach(buffer);
		buffer.begin(0, false, Sixteenth);

		QVERIFY(buffer.noteOn(liveNote(60, 10)));
		buffer.finish(100);
		buffer.finish(200);
		QVERIFY(!buffer.noteOff(liveNote(60, 10, 150)));

		QCOMPARE(recorded.notes.size(), std::size_t{1});
		QCOMPARE(buffer.pendingCount(), std::size_t{0});
		QVERIFY(!buffer.isOpen());

		// A new take starts with nothing pending
		buffer.begin(Bar, false, Sixteenth);
		QCOMPARE(buffer.pendingCount(), std::size_t{0});
	}

	// U8
	void QuantizesWhenEnabled()
	{
		auto buffer = MidiTakeBuffer{};
		auto recorded = Recorded{};
		recorded.attach(buffer);
		buffer.begin(0, true, Sixteenth);

		QVERIFY(buffer.noteOn(liveNote(60, Sixteenth + 2)));
		QVERIFY(buffer.noteOff(liveNote(60, Sixteenth + 2, Sixteenth - 3)));

		QCOMPARE(recorded.notes.size(), std::size_t{1});
		QCOMPARE(recorded.notes[0].pos().getTicks(), Sixteenth);
		QCOMPARE(recorded.notes[0].length().getTicks(), Sixteenth);
	}

	// U9
	void KeepsTimingWhenQuantizeDisabled()
	{
		auto buffer = MidiTakeBuffer{};
		auto recorded = Recorded{};
		recorded.attach(buffer);
		buffer.begin(0, false, Sixteenth);

		QVERIFY(buffer.noteOn(liveNote(60, Sixteenth + 2)));
		QVERIFY(buffer.noteOff(liveNote(60, Sixteenth + 2, Sixteenth - 3)));

		QCOMPARE(recorded.notes.size(), std::size_t{1});
		QCOMPARE(recorded.notes[0].pos().getTicks(), Sixteenth + 2);
		QCOMPARE(recorded.notes[0].length().getTicks(), Sixteenth - 3);
	}

	// U10
	void ClipStartsOnBarAndQuantizesOnSongGrid()
	{
		auto buffer = MidiTakeBuffer{};
		auto recorded = Recorded{};
		recorded.attach(buffer);
		buffer.begin(2 * Bar + 37, true, Sixteenth);

		QCOMPARE(buffer.clipStart().getTicks(), 2 * Bar);
		QCOMPARE(buffer.recordStart().getTicks(), 2 * Bar + 37);

		QVERIFY(buffer.noteOn(liveNote(60, 2 * Bar + 37)));
		QVERIFY(buffer.noteOff(liveNote(60, 2 * Bar + 37, Sixteenth)));

		QCOMPARE(recorded.notes.size(), std::size_t{1});
		const int songPos = buffer.clipStart().getTicks() + recorded.notes[0].pos().getTicks();
		QCOMPARE(songPos % Sixteenth, 0);
		QCOMPARE(songPos, 2 * Bar + 3 * Sixteenth);
	}

	// U11
	void CommitsClipRelativePositions()
	{
		auto buffer = MidiTakeBuffer{};
		auto recorded = Recorded{};
		recorded.attach(buffer);
		buffer.begin(3 * Bar + 5, false, Sixteenth);

		QVERIFY(buffer.noteOn(liveNote(64, 3 * Bar + 40)));
		QVERIFY(buffer.noteOff(liveNote(64, 3 * Bar + 40, 20)));

		QCOMPARE(recorded.notes.size(), std::size_t{1});
		QCOMPARE(recorded.notes[0].pos().getTicks(), 40);
		QCOMPARE(recorded.notes[0].key(), 64);
	}

	void AbortDropsPendingNotes()
	{
		auto buffer = MidiTakeBuffer{};
		auto recorded = Recorded{};
		recorded.attach(buffer);
		buffer.begin(0, false, Sixteenth);

		QVERIFY(buffer.noteOn(liveNote(60, 10)));
		buffer.abort();
		QVERIFY(!buffer.isOpen());
		QCOMPARE(buffer.pendingCount(), std::size_t{0});
		QVERIFY(recorded.notes.empty());
	}
};

QTEST_GUILESS_MAIN(MidiTakeBufferTest)

#include "MidiTakeBufferTest.moc"
