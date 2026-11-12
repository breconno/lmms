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
#include "CrashEvidence.h"
#include "MidiClip.h"
#include "Song.h"
#include "Timeline.h"
#include "Telemetry.h"

namespace lmms
{

MidiTakeRecorder::MidiTakeRecorder(Song* song) :
	QObject(song),
	m_song(song)
{
	// Song::stop() can run on the render thread, so the stop is always queued.
	// The take id is read here, when the stop happens, so that a stop from
	// before a take started can't end that take.
	connect(m_song, &Song::aboutToStop, this, [this](TimePos stopPos) {
		const int id = m_takeId;
		QMetaObject::invokeMethod(this, [this, id, stopPos] {
			if (id == m_takeId) { stopAt(stopPos); }
		}, Qt::QueuedConnection);
	}, Qt::DirectConnection);
}




MidiTakeRecorder::~MidiTakeRecorder()
{
	for (const auto& connection : m_connections) { disconnect(connection); }
}




MidiTakeRecorder::StartResult MidiTakeRecorder::checkStart(const TakeParams& params) const
{
	if (m_active) { return StartResult::AlreadyActive; }
	if (params.editorRecording) { return StartResult::EditorRecording; }

	const auto& tracks = m_song->tracks();
	const bool anyArmed = std::any_of(tracks.begin(), tracks.end(), [](Track* track) {
		auto instrumentTrack = dynamic_cast<InstrumentTrack*>(track);
		return instrumentTrack && instrumentTrack->isRecordArmed();
	});
	return anyArmed ? StartResult::Started : StartResult::NoArmedTracks;
}




MidiTakeRecorder::StartResult MidiTakeRecorder::start(const TakeParams& params)
{
	CrashEvidence::breadcrumb("take.start");
	const auto result = checkStart(params);
	if (result != StartResult::Started)
	{
		Telemetry::record("take.start.refused", "failure", nullptr,
			"{\"reason\":\"start-rejected\"}");
		return result;
	}

	if (m_song->isPlaying() || m_song->isPaused()) { m_song->stop(); }

	const auto& timeline = m_song->getTimeline(Song::PlayMode::Song);
	const auto current = m_song->getPlayPos(Song::PlayMode::Song);
	const auto recordStart = timeline.loopEnabled() &&
		(current < timeline.loopBegin() || current >= timeline.loopEnd())
		? timeline.loopBegin() : current;
	if (recordStart != current) { m_song->setPlayPos(recordStart.getTicks()); }
	startAt(params, recordStart);
	m_song->playSong();
	return StartResult::Started;
}




MidiTakeRecorder::StartResult MidiTakeRecorder::startAt(const TakeParams& params, TimePos recordStart)
{
	const auto result = checkStart(params);
	if (result != StartResult::Started)
	{
		Telemetry::record("take.start.refused", "failure", nullptr,
			"{\"reason\":\"start-at-rejected\"}");
		return result;
	}

	const int id = ++m_takeId;
	const auto& timeline = m_song->getTimeline(Song::PlayMode::Song);
	m_looping = timeline.loopEnabled();
	m_loopBegin = timeline.loopBegin();
	m_loopEnd = timeline.loopEnd();
	for (Track* track : m_song->tracks())
	{
		auto instrumentTrack = dynamic_cast<InstrumentTrack*>(track);
		if (!instrumentTrack || !instrumentTrack->isRecordArmed()) { continue; }

		auto take = std::make_unique<TrackTake>();
		take->track = instrumentTrack;
		take->autoQuantize = params.autoQuantize;
		take->quantTicks = params.quantTicks;
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
		m_connections.push_back(connect(instrumentTrack, &Track::destroyedTrack, this,
			[this, instrumentTrack] { trackDestroyed(instrumentTrack); }, Qt::DirectConnection));
	}
	if (m_looping)
	{
		m_connections.push_back(connect(&timeline, &Timeline::positionJumped, this,
			[this, id] {
				if (id != m_takeId || !m_active ||
					m_song->getPlayPos(Song::PlayMode::Song) != m_loopBegin) { return; }
				QMetaObject::invokeMethod(this, [this, id] {
					if (id == m_takeId) { finishLoopPass(); }
				}, Qt::QueuedConnection);
			}, Qt::DirectConnection));
	}

	m_active = true;
	Telemetry::record("take.started", "running", nullptr,
		("{\"take_id\":" + std::to_string(id) + ",\"tracks\":" +
		 std::to_string(m_takes.size()) + "}").c_str());
	emit activeChanged(true);
	return StartResult::Started;
}




void MidiTakeRecorder::abort()
{
	CrashEvidence::breadcrumb("take.abort");
	if (!m_active) { return; }
	Telemetry::record("take.aborted", "success");
	for (auto& take : m_takes) { take->buffer.abort(); }
	close();
}




void MidiTakeRecorder::stopAt(TimePos stopPos)
{
	CrashEvidence::breadcrumb("take.stop");
	if (!m_active) { return; }
	Telemetry::record("take.stop.requested", "running");
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
	m_looping = false;
	m_active = false;
	Telemetry::record("take.closed", "success");
	emit activeChanged(false);
}



void MidiTakeRecorder::finishLoopPass()
{
	if (!m_active || !m_looping) { return; }
	for (auto& take : m_takes)
	{
		take->buffer.finish(m_loopEnd);
		take->clip = nullptr;
		take->clipCreated = false;
		take->buffer.begin(m_loopBegin, take->autoQuantize, take->quantTicks);
	}
}




void MidiTakeRecorder::onNoteOn(InstrumentTrack* track, const Note& note)
{
	CrashEvidence::breadcrumb("midi.note_on");
	if (!m_active) { return; }
	Telemetry::record("midi.note_on", "running");
	if (auto take = find(track)) { take->buffer.noteOn(note); }
}




void MidiTakeRecorder::onNoteOff(InstrumentTrack* track, const Note& note)
{
	CrashEvidence::breadcrumb("midi.note_off");
	if (!m_active) { return; }
	Telemetry::record("midi.note_off", "running");
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




void MidiTakeRecorder::trackDestroyed(InstrumentTrack* track)
{
	Telemetry::record("track.destroyed", "success");
	m_takes.erase(std::remove_if(m_takes.begin(), m_takes.end(),
		[track](const auto& take) { return take->track == track; }), m_takes.end());
}

} // namespace lmms
