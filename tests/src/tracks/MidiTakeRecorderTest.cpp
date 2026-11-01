/*
 * MidiTakeRecorderTest.cpp
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

#include <algorithm>

#include <QDomDocument>
#include <QObject>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>

#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "MidiTakeRecorder.h"
#include "NotePlayHandle.h"
#include "ProjectJournal.h"
#include "Song.h"

namespace
{

using namespace lmms;

constexpr int Bar = DefaultTicksPerBar;
constexpr int Sixteenth = DefaultTicksPerBar / 16;

Song* song() { return Engine::getSong(); }
MidiTakeRecorder* recorder() { return song()->midiTakeRecorder(); }

//! Waits until the audio engine has picked up and finished the live notes of a
//! track. A note that is still queued as a new play handle survives deleting
//! its track, so tests wait for this before the next test clears the project.
bool liveNotesFinished(Track* track)
{
	QTest::qWait(20);
	auto engine = Engine::audioEngine();
	engine->requestChangeInModel();
	const auto& handles = engine->playHandles();
	const bool finished = std::none_of(handles.begin(), handles.end(),
		[track](PlayHandle* handle) { return handle->isFromTrack(track); });
	engine->doneChangeInModel();
	return finished;
}

InstrumentTrack* addTrack(bool armed)
{
	auto track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song()));
	track->setRecordArmed(armed);
	return track;
}

Note liveNote(int key, int songPos, int length = 0)
{
	return Note(TimePos(length), TimePos(songPos), key, 90);
}

std::vector<MidiClip*> midiClips(Track* track)
{
	auto clips = std::vector<MidiClip*>{};
	for (Clip* clip : track->getClips())
	{
		if (auto midiClip = dynamic_cast<MidiClip*>(clip)) { clips.push_back(midiClip); }
	}
	return clips;
}

MidiTakeRecorder::TakeParams params(bool autoQuantize = false)
{
	auto result = MidiTakeRecorder::TakeParams{};
	result.autoQuantize = autoQuantize;
	result.quantTicks = Sixteenth;
	return result;
}

} // namespace

class MidiTakeRecorderTest : public QObject
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
		recorder()->abort();
		song()->clearProject();
		song()->getTimeline(Song::PlayMode::Song).setLoopEnabled(false);
		Engine::projectJournal()->clearJournal();
		Engine::projectJournal()->setJournalling(true);
	}

	// R2
	void KeepsCapturedPositionWhenDeliveredLate()
	{
		auto track = addTrack(true);
		auto& timeline = song()->getTimeline(Song::PlayMode::None);
		timeline.setTicks(0);
		QCOMPARE(recorder()->startAt(params(), 0), MidiTakeRecorder::StartResult::Started);

		timeline.setTicks(Bar + 7);
		track->processInEvent(MidiEvent(MidiNoteOn, 0, 60, 100));
		track->processInEvent(MidiEvent(MidiNoteOff, 0, 60, 0));
		timeline.setTicks(5 * Bar);
		QCoreApplication::processEvents();

		recorder()->stopAt(6 * Bar);
		const auto clips = midiClips(track);
		QCOMPARE(clips.size(), std::size_t{1});
		QCOMPARE(clips[0]->notes().size(), std::size_t{1});
		QCOMPARE(clips[0]->startPosition().getTicks(), 0);
		QCOMPARE(clips[0]->notes()[0]->pos().getTicks(), Bar + 7);
		QTRY_VERIFY(liveNotesFinished(track));
	}

	// R3
	void RecordsEachArmedTrackIntoItsOwnClip()
	{
		auto a = addTrack(true);
		auto b = addTrack(true);
		auto c = addTrack(true);
		auto unarmed = addTrack(false);
		QCOMPARE(recorder()->startAt(params(), Bar), MidiTakeRecorder::StartResult::Started);
		QCOMPARE(recorder()->trackCount(), std::size_t{3});

		recorder()->onNoteOn(a, liveNote(60, Bar + 10));
		recorder()->onNoteOn(b, liveNote(64, Bar + 10));
		recorder()->onNoteOn(c, liveNote(67, Bar + 30));
		recorder()->onNoteOn(unarmed, liveNote(72, Bar + 10));
		recorder()->onNoteOff(a, liveNote(60, Bar + 10, 20));
		recorder()->onNoteOff(b, liveNote(64, Bar + 10, 20));
		recorder()->onNoteOff(c, liveNote(67, Bar + 30, 20));
		recorder()->onNoteOff(unarmed, liveNote(72, Bar + 10, 20));
		recorder()->stopAt(3 * Bar);

		for (auto [track, key] : {std::pair{a, 60}, std::pair{b, 64}, std::pair{c, 67}})
		{
			const auto clips = midiClips(track);
			QCOMPARE(clips.size(), std::size_t{1});
			QCOMPARE(clips[0]->startPosition().getTicks(), Bar);
			QCOMPARE(clips[0]->notes().size(), std::size_t{1});
			QCOMPARE(clips[0]->notes()[0]->key(), key);
		}
		QVERIFY(midiClips(unarmed).empty());
		QVERIFY(!recorder()->isActive());
	}

	// R4
	void MidBarStartQuantizesOnSongGrid()
	{
		auto track = addTrack(true);
		auto takeParams = params(true);
		QCOMPARE(recorder()->startAt(takeParams, 2 * Bar + 37), MidiTakeRecorder::StartResult::Started);
		// Changing the caller's settings after start has no effect on the take
		takeParams.quantTicks = Bar;

		recorder()->onNoteOn(track, liveNote(60, 2 * Bar + 40));
		recorder()->onNoteOff(track, liveNote(60, 2 * Bar + 40, Sixteenth - 1));
		recorder()->stopAt(4 * Bar);

		const auto clips = midiClips(track);
		QCOMPARE(clips.size(), std::size_t{1});
		QCOMPARE(clips[0]->startPosition().getTicks(), 2 * Bar);
		const auto* note = clips[0]->notes()[0];
		QCOMPARE((clips[0]->startPosition().getTicks() + note->pos().getTicks()) % Sixteenth, 0);
		QCOMPARE(note->pos().getTicks(), 3 * Sixteenth);
		QCOMPARE(note->length().getTicks(), Sixteenth);
	}

	// R5
	void HeldNotesEndAtStopAndDoNotLeak()
	{
		auto track = addTrack(true);
		QCOMPARE(recorder()->startAt(params(), 0), MidiTakeRecorder::StartResult::Started);
		recorder()->onNoteOn(track, liveNote(60, 100));
		recorder()->onNoteOn(track, liveNote(62, 120));
		recorder()->stopAt(Bar);

		// Note-offs arriving after stop, e.g. on pedal release
		recorder()->onNoteOff(track, liveNote(60, 100, 500));
		recorder()->onNoteOff(track, liveNote(62, 120, 500));

		auto clips = midiClips(track);
		QCOMPARE(clips.size(), std::size_t{1});
		QCOMPARE(clips[0]->notes().size(), std::size_t{2});
		for (const auto* note : clips[0]->notes())
		{
			QCOMPARE(note->endPos().getTicks(), Bar);
		}

		// The next take starts clean
		QCOMPARE(recorder()->startAt(params(), Bar), MidiTakeRecorder::StartResult::Started);
		recorder()->stopAt(2 * Bar);
		QCOMPARE(midiClips(track).size(), std::size_t{1});
	}

	// R6
	void SilentTakeCreatesNothing()
	{
		auto track = addTrack(true);
		// Setting up tracks is journalled; only the take matters here
		Engine::projectJournal()->clearJournal();
		QVERIFY(!Engine::projectJournal()->canUndo());
		QCOMPARE(recorder()->startAt(params(), 0), MidiTakeRecorder::StartResult::Started);
		recorder()->stopAt(4 * Bar);
		QVERIFY(midiClips(track).empty());
		QVERIFY(!Engine::projectJournal()->canUndo());
	}

	// R7
	void UndoEmptiesTakeClipsAndRedoRestoresThem()
	{
		auto tracks = std::vector<InstrumentTrack*>{addTrack(true), addTrack(true), addTrack(true)};

		// An earlier take on the first track, overlapping the next one
		QCOMPARE(recorder()->startAt(params(), 0), MidiTakeRecorder::StartResult::Started);
		recorder()->onNoteOn(tracks[0], liveNote(48, 10));
		recorder()->onNoteOff(tracks[0], liveNote(48, 10, 300));
		recorder()->stopAt(2 * Bar);
		Engine::projectJournal()->clearJournal();
		MidiClip* earlier = midiClips(tracks[0])[0];

		QCOMPARE(recorder()->startAt(params(), 0), MidiTakeRecorder::StartResult::Started);
		for (auto track : tracks)
		{
			recorder()->onNoteOn(track, liveNote(60, 20));
			recorder()->onNoteOff(track, liveNote(60, 20, 2 * Bar));
		}
		recorder()->stopAt(3 * Bar);

		auto takeClips = std::vector<MidiClip*>{};
		for (auto track : tracks)
		{
			for (auto clip : midiClips(track))
			{
				if (clip != earlier) { takeClips.push_back(clip); }
			}
		}
		QCOMPARE(takeClips.size(), std::size_t{3});
		const auto recordedLength = takeClips[0]->length();
		QCOMPARE(recordedLength.getTicks(), 3 * Bar);

		for (int i = 0; i < 3; ++i) { Engine::projectJournal()->undo(); }
		QVERIFY(!Engine::projectJournal()->canUndo());
		for (auto clip : takeClips) { QVERIFY(clip->notes().empty()); }

		for (int i = 0; i < 3; ++i) { Engine::projectJournal()->redo(); }
		for (auto clip : takeClips)
		{
			QCOMPARE(clip->notes().size(), std::size_t{1});
			QCOMPARE(clip->length(), recordedLength);
		}

		QCOMPARE(earlier->notes().size(), std::size_t{1});
		QCOMPARE(earlier->notes()[0]->key(), 48);
		QCOMPARE(earlier->notes()[0]->pos().getTicks(), 10);
		QCOMPARE(earlier->notes()[0]->length().getTicks(), 300);
	}

	// R8
	void NoteHeldBeforeStartIsIgnored()
	{
		auto track = addTrack(true);
		QCOMPARE(recorder()->startAt(params(), Bar), MidiTakeRecorder::StartResult::Started);
		recorder()->onNoteOn(track, liveNote(60, Bar - 5));
		recorder()->onNoteOff(track, liveNote(60, Bar - 5, 50));
		recorder()->stopAt(2 * Bar);
		QVERIFY(midiClips(track).empty());
	}
};

QTEST_GUILESS_MAIN(MidiTakeRecorderTest)

#include "MidiTakeRecorderTest.moc"
