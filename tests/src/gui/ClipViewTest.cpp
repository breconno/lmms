/*
 * ClipViewTest.cpp
 *
 * This file is part of LMMS - https://lmms.io
 */

#include <QObject>
#include <QtTest>

#include "ClipView.h"

using lmms::gui::ClipView;

class ClipViewTest : public QObject
{
	Q_OBJECT

private slots:
	void CyclesThroughOverlappingClips()
	{
		QCOMPARE(ClipView::nextOverlappingClipIndex(0, 3), 1);
		QCOMPARE(ClipView::nextOverlappingClipIndex(1, 3), 2);
		QCOMPARE(ClipView::nextOverlappingClipIndex(2, 3), 0);
	}

	void EmptyOverlapStackIsStable()
	{
		QCOMPARE(ClipView::nextOverlappingClipIndex(0, 0), 0);
		QCOMPARE(ClipView::nextOverlappingClipIndex(7, -1), 0);
	}
};

QTEST_MAIN(ClipViewTest)
#include "ClipViewTest.moc"
