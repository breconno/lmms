/*
 * MidiTakeBuffer.h - pending notes of one track during a multi-track MIDI take
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

#ifndef LMMS_MIDI_TAKE_BUFFER_H
#define LMMS_MIDI_TAKE_BUFFER_H

#include <functional>
#include <vector>

#include "Note.h"
#include "TimePos.h"
#include "lmms_export.h"

namespace lmms
{

//! Collects the live notes of one track during a take and turns them into
//! clip-relative notes. Positions passed in are song positions, captured when
//! the MIDI event was processed. Has no Engine or GUI dependencies.
class LMMS_EXPORT MidiTakeBuffer
{
public:
	//! Receives each finished note, positioned relative to clipStart()
	using CommitFunction = std::function<void(const Note&)>;

	void setCommitFunction(CommitFunction commit);

	//! Opens the take. Notes captured before recordStart are ignored.
	void begin(TimePos recordStart, bool autoQuantize, tick_t quantTicks);

	//! Commits pending notes, ending them at recordStop, and closes the take
	void finish(TimePos recordStop);

	//! Drops pending notes without committing and closes the take
	void abort();

	bool isOpen() const { return m_open; }
	TimePos recordStart() const { return m_recordStart; }

	//! The bar line at or before recordStart(): where the take clip starts
	TimePos clipStart() const { return m_clipStart; }

	//! Returns true if the note was accepted as pending
	bool noteOn(const Note& note);

	//! Commits the pending note with the same key and start position.
	//! Returns true if a note was committed.
	bool noteOff(const Note& note);

	std::size_t pendingCount() const { return m_pending.size(); }

private:
	void commit(TimePos start, TimePos length, const Note& source);

	CommitFunction m_commit;
	std::vector<Note> m_pending;
	TimePos m_recordStart = 0;
	TimePos m_clipStart = 0;
	tick_t m_quantTicks = 0;
	bool m_autoQuantize = false;
	bool m_open = false;
};

} // namespace lmms

#endif // LMMS_MIDI_TAKE_BUFFER_H
