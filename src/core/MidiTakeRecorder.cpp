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

#include "Song.h"


namespace lmms
{

MidiTakeRecorder::MidiTakeRecorder(Song* song) :
	QObject(song),
	m_song(song)
{
}




MidiTakeRecorder::~MidiTakeRecorder() = default;




MidiTakeRecorder::StartResult MidiTakeRecorder::start(const TakeParams&)
{
	return StartResult::AlreadyActive;
}




MidiTakeRecorder::StartResult MidiTakeRecorder::startAt(const TakeParams&, TimePos)
{
	return StartResult::AlreadyActive;
}




void MidiTakeRecorder::abort()
{
}




void MidiTakeRecorder::stopAt(TimePos)
{
}




void MidiTakeRecorder::onNoteOn(InstrumentTrack*, const Note&)
{
}




void MidiTakeRecorder::onNoteOff(InstrumentTrack*, const Note&)
{
}

} // namespace lmms
