/*
 * RecordingQuantization.h - resolve the quantization grid used when recording notes
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

#ifndef LMMS_RECORDING_QUANTIZATION_H
#define LMMS_RECORDING_QUANTIZATION_H

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

//! Returns the quantization grid in ticks.
//! \param gridTicks the selected grid, or 0 for "Note lock"
//! \param noteLenTicks the selected note length, or 0 for "Last note"
LMMS_EXPORT tick_t resolveQuantization(tick_t gridTicks, tick_t noteLenTicks);

} // namespace lmms

#endif // LMMS_RECORDING_QUANTIZATION_H
