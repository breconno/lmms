/*
 * MidiTakeRecorder.cpp - records live MIDI into new clips on every armed instrument track
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

#include "MidiTakeRecorder.h"

#include <algorithm>

#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Song.h"

namespace lmms
{

MidiTakeRecorder::MidiTakeRecorder(Song* song) :
	QObject(song),
	m_song(song)
{
}




MidiTakeRecorder::~MidiTakeRecorder()
{
	for (const auto& connection : m_connections) { disconnect(connection); }
}




MidiTakeRecorder::StartResult MidiTakeRecorder::checkStart(const TakeParams&) const
{
	if (m_active) { return StartResult::AlreadyActive; }
	return StartResult::Started;
}




MidiTakeRecorder::StartResult MidiTakeRecorder::start(const TakeParams&)
{
	return StartResult::AlreadyActive;
}




MidiTakeRecorder::StartResult MidiTakeRecorder::startAt(const TakeParams& params, TimePos recordStart)
{
	const auto result = checkStart(params);
	if (result != StartResult::Started) { return result; }

	const int id = ++m_takeId;
	for (Track* track : m_song->tracks())
	{
		auto instrumentTrack = dynamic_cast<InstrumentTrack*>(track);
		if (!instrumentTrack || !instrumentTrack->isRecordArmed()) { continue; }

		auto take = std::make_unique<TrackTake>();
		take->track = instrumentTrack;
		TrackTake* takePtr = take.get();
		take->buffer.setCommitFunction([this, takePtr](const Note& note) { commit(*takePtr, note); });
		take->buffer.begin(recordStart, params.autoQuantize, params.quantTicks);
		m_takes.push_back(std::move(take));

		m_connections.push_back(connect(instrumentTrack, &InstrumentTrack::externalNoteOn, this,
			[this, instrumentTrack, id](const Note& note) {
				if (id == m_takeId) { onNoteOn(instrumentTrack, note); }
			}, Qt::QueuedConnection));
		m_connections.push_back(connect(instrumentTrack, &InstrumentTrack::externalNoteOff, this,
			[this, instrumentTrack, id](const Note& note) {
				if (id == m_takeId) { onNoteOff(instrumentTrack, note); }
			}, Qt::QueuedConnection));
	}

	m_active = true;
	emit activeChanged(true);
	return StartResult::Started;
}




void MidiTakeRecorder::abort()
{
	if (!m_active) { return; }
	for (auto& take : m_takes) { take->buffer.abort(); }
	close();
}




void MidiTakeRecorder::stopAt(TimePos stopPos)
{
	if (!m_active) { return; }
	for (auto& take : m_takes) { take->buffer.finish(stopPos); }
	close();
}




void MidiTakeRecorder::close()
{
	for (const auto& connection : m_connections) { disconnect(connection); }
	m_connections.clear();
	m_takes.clear();
	// Invalidate anything still queued for this take
	++m_takeId;
	m_active = false;
	emit activeChanged(false);
}




void MidiTakeRecorder::onNoteOn(InstrumentTrack* track, const Note& note)
{
	if (!m_active) { return; }
	if (auto take = find(track)) { take->buffer.noteOn(note); }
}




void MidiTakeRecorder::onNoteOff(InstrumentTrack* track, const Note& note)
{
	if (!m_active) { return; }
	if (auto take = find(track)) { take->buffer.noteOff(note); }
}




MidiTakeRecorder::TrackTake* MidiTakeRecorder::find(InstrumentTrack* track)
{
	const auto it = std::find_if(m_takes.begin(), m_takes.end(),
		[track](const auto& take) { return take->track == track; });
	return it != m_takes.end() ? it->get() : nullptr;
}




void MidiTakeRecorder::commit(TrackTake& take, const Note& note)
{
	if (!take.clipCreated)
	{
		// The first committed note creates the clip, so a silent track gets
		// neither a clip nor an undo step. The checkpoint holds the empty clip.
		take.clipCreated = true;
		take.clip = dynamic_cast<MidiClip*>(take.track->createClip(take.buffer.clipStart()));
		if (take.clip) { take.clip->addJournalCheckPoint(); }
	}
	// If the user deleted the clip during the take, the rest of the take is dropped
	if (take.clip) { take.clip->addNote(note, false); }
}


} // namespace lmms
