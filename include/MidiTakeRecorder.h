/*
 * MidiTakeRecorder.h - records live MIDI into new clips on every armed instrument track
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

#ifndef LMMS_MIDI_TAKE_RECORDER_H
#define LMMS_MIDI_TAKE_RECORDER_H

#include <atomic>
#include <memory>
#include <vector>

#include <QObject>
#include <QPointer>

#include "MidiTakeBuffer.h"
#include "lmms_export.h"

namespace lmms
{

class InstrumentTrack;
class MidiClip;
class Song;

//! Records external MIDI notes on every armed instrument track into one new
//! clip per track and take. Lives on the GUI thread; note and stop events
//! from other threads arrive through the event queue.
class LMMS_EXPORT MidiTakeRecorder : public QObject
{
	Q_OBJECT
public:
	struct TakeParams
	{
		bool autoQuantize = false;
		tick_t quantTicks = DefaultTicksPerBar / 16;
		//! Set when the Piano Roll is live or step recording
		bool editorRecording = false;
	};

	enum class StartResult
	{
		Started,
		AlreadyActive,
		EditorRecording,
		NoArmedTracks,
		LoopEnabled
	};

	explicit MidiTakeRecorder(Song* song);
	~MidiTakeRecorder() override;

	//! Starts a take at the current song position and plays the song
	StartResult start(const TakeParams& params);

	//! Opens a take at recordStart without touching the transport
	StartResult startAt(const TakeParams& params, TimePos recordStart);

	//! Discards the active take: pending notes are dropped, nothing is committed
	void abort();

	bool isActive() const { return m_active; }

	//! Number of tracks taking part in the active take
	std::size_t trackCount() const { return m_takes.size(); }

public slots:
	//! A live note started; note.pos() is its captured song position
	void onNoteOn(lmms::InstrumentTrack* track, const lmms::Note& note);

	//! A live note ended; note.pos() is its start and note.length() its length
	void onNoteOff(lmms::InstrumentTrack* track, const lmms::Note& note);

	//! Commits held notes, ending them at stopPos, and closes the take
	void stopAt(lmms::TimePos stopPos);

signals:
	void activeChanged(bool active);

private:
	struct TrackTake
	{
		InstrumentTrack* track = nullptr;
		QPointer<MidiClip> clip;
		bool clipCreated = false;
		MidiTakeBuffer buffer;
	};

	StartResult checkStart(const TakeParams& params) const;
	TrackTake* find(InstrumentTrack* track);
	void commit(TrackTake& take, const Note& note);
	void close();

	Song* m_song;
	std::vector<std::unique_ptr<TrackTake>> m_takes;
	std::vector<QMetaObject::Connection> m_connections;
	bool m_active = false;

	//! Identifies the current take, so that queued events of an earlier
	//! take or from before this one started are ignored. Read from any thread.
	std::atomic<int> m_takeId = 0;
};

} // namespace lmms

#endif // LMMS_MIDI_TAKE_RECORDER_H
