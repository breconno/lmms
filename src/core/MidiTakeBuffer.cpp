/*
 * MidiTakeBuffer.cpp - pending notes of one track during a multi-track MIDI take
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

#include "MidiTakeBuffer.h"

#include <algorithm>

namespace lmms
{

void MidiTakeBuffer::setCommitFunction(CommitFunction commit)
{
	m_commit = std::move(commit);
}




void MidiTakeBuffer::begin(TimePos recordStart, bool autoQuantize, tick_t quantTicks)
{
	m_pending.clear();
	m_recordStart = recordStart;
	m_clipStart = recordStart.toAbsoluteBar();
	m_autoQuantize = autoQuantize;
	m_quantTicks = quantTicks;
	m_open = true;
}




void MidiTakeBuffer::finish(TimePos recordStop)
{
	if (!m_open) { return; }
	m_open = false;

	const auto pending = std::move(m_pending);
	m_pending.clear();
	for (const auto& note : pending)
	{
		if (note.pos() >= recordStop) { continue; }
		commit(note.pos(), recordStop - note.pos(), note, recordStop);
	}
}




void MidiTakeBuffer::abort()
{
	m_open = false;
	m_pending.clear();
}




bool MidiTakeBuffer::noteOn(const Note& note)
{
	if (!m_open || note.pos() < m_recordStart) { return false; }
	m_pending.push_back(note);
	return true;
}




bool MidiTakeBuffer::noteOff(const Note& note)
{
	if (!m_open) { return false; }

	// Match on the start too: the same key can be held twice under the sustain pedal
	const auto it = std::find_if(m_pending.begin(), m_pending.end(), [&note](const Note& pending) {
		return pending.key() == note.key() && pending.pos() == note.pos();
	});
	if (it == m_pending.end()) { return false; }

	const Note started = *it;
	m_pending.erase(it);
	commit(started.pos(), note.length(), started, std::nullopt);
	return true;
}




void MidiTakeBuffer::commit(TimePos start, TimePos length, const Note& source, std::optional<TimePos> recordStop)
{
	auto note = Note(length, start - m_clipStart, source.key(), source.getVolume(), source.getPanning());
	if (m_autoQuantize && m_quantTicks > 0)
	{
		note.quantizePos(m_quantTicks);
		note.quantizeLength(m_quantTicks);
	}

	if (recordStop)
	{
		// A note still held at stop ends at stop, even after quantizing
		const tick_t end = *recordStop - m_clipStart;
		if (note.pos() >= end) { note.setPos(end - 1); }
		if (note.endPos() > end) { note.setLength(end - note.pos()); }
	}
	note.setLength(std::max<tick_t>(note.length(), 1));

	if (m_commit) { m_commit(note); }
}

} // namespace lmms
