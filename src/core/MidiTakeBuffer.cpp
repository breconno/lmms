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

namespace lmms
{

void MidiTakeBuffer::setCommitFunction(CommitFunction commit)
{
	m_commit = std::move(commit);
}




void MidiTakeBuffer::begin(TimePos, bool, tick_t)
{
}




void MidiTakeBuffer::finish(TimePos)
{
}




void MidiTakeBuffer::abort()
{
}




bool MidiTakeBuffer::noteOn(const Note&)
{
	return false;
}




bool MidiTakeBuffer::noteOff(const Note&)
{
	return false;
}




void MidiTakeBuffer::commit(TimePos, TimePos, const Note&)
{
}

} // namespace lmms
