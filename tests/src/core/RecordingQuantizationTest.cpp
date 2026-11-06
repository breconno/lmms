/*
 * RecordingQuantizationTest.cpp
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

#include "RecordingQuantization.h"
#include "TimePos.h"

namespace
{

// Piano Roll grid choices after "Note lock" (include/Editor.h).
constexpr int Quantizations[] = {1, 2, 4, 8, 16, 32, 64, 3, 6, 12, 24, 48, 96, 192};

// Piano Roll note length choices after "Last note" (PianoRoll.cpp).
std::vector<int> noteLengthDivisors()
{
	auto divisors = std::vector<int>{};
	for (int i = 0; i < 6; ++i) { divisors.push_back(1 << i); }
	for (int i = 0; i < 5; ++i) { divisors.push_back((1 << i) * 3); }
	return divisors;
}

// The formula PianoRoll::quantization() used before it was moved to core.
int oldQuantization(int quantizeIndex, int noteLenIndex)
{
	using lmms::DefaultTicksPerBar;
	if (quantizeIndex == 0)
	{
		if (noteLenIndex > 0) { return DefaultTicksPerBar / noteLengthDivisors()[noteLenIndex - 1]; }
		return DefaultTicksPerBar / 16;
	}
	return DefaultTicksPerBar / Quantizations[quantizeIndex - 1];
}

} // namespace

class RecordingQuantizationTest : public QObject
{
	Q_OBJECT
private slots:
	void GridWins()
	{
		using namespace lmms;
		QCOMPARE(resolveQuantization(DefaultTicksPerBar / 8, DefaultTicksPerBar / 4), DefaultTicksPerBar / 8);
	}

	void NoteLockUsesNoteLength()
	{
		using namespace lmms;
		QCOMPARE(resolveQuantization(0, DefaultTicksPerBar / 4), DefaultTicksPerBar / 4);
	}

	void NoteLockWithLastNoteIsSixteenth()
	{
		using namespace lmms;
		QCOMPARE(resolveQuantization(0, 0), DefaultTicksPerBar / 16);
	}

	void MatchesOldPianoRollFormula()
	{
		using namespace lmms;
		const auto divisors = noteLengthDivisors();
		for (int q = 0; q <= static_cast<int>(std::size(Quantizations)); ++q)
		{
			for (int l = 0; l <= static_cast<int>(divisors.size()); ++l)
			{
				const tick_t grid = q == 0 ? 0 : DefaultTicksPerBar / Quantizations[q - 1];
				const tick_t noteLen = l == 0 ? 0 : DefaultTicksPerBar / divisors[l - 1];
				QCOMPARE(resolveQuantization(grid, noteLen), oldQuantization(q, l));
			}
		}
	}
};

QTEST_GUILESS_MAIN(RecordingQuantizationTest)

#include "RecordingQuantizationTest.moc"
