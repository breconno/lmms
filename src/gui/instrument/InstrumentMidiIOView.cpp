/*
 * InstrumentMidiIOView.cpp - MIDI-IO-View
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include <QPainter>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "InstrumentMidiIOView.h"
#include "MidiPortMenu.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "embed.h"
#include "FontHelper.h"
#include "GroupBox.h"
#include "LcdSpinBox.h"
#include "LedCheckBox.h"
#include "MidiClient.h"

namespace lmms::gui
{

namespace
{

class MidiThruControl final : public QWidget
{
public:
	MidiThruControl(LedCheckBox* button, const LcdSpinBox* reference, const QString& label, QWidget* parent) :
		QWidget(parent),
		m_reference(reference),
		m_label(label)
	{
		const auto font = adjustedToPixelSize(reference->font(), DEFAULT_FONT_SIZE);
		button->setParent(this);
		button->setFixedWidth(embed::getIconPixmap("led_off").width());
		setFixedSize(qMax(button->width(), QFontMetrics(font).horizontalAdvance(label) + 1),
			reference->height());
		button->move((width() - button->width()) / 2,
			(reference->cellHeight() + 2 - button->height()) / 2);
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		painter.setFont(adjustedToPixelSize(m_reference->font(), DEFAULT_FONT_SIZE));
		const int x = width() / 2 - painter.fontMetrics().horizontalAdvance(m_label) / 2;
		painter.setPen(m_reference->textShadowColor());
		painter.drawText(x + 1, height(), m_label);
		painter.setPen(m_reference->textColor());
		painter.drawText(x, height() - 1, m_label);
	}

private:
	const LcdSpinBox* m_reference;
	QString m_label;
};

}


InstrumentMidiIOView::InstrumentMidiIOView( QWidget* parent ) :
	QWidget( parent ),
	ModelView( nullptr, this ),
	m_rpBtn( nullptr ),
	m_wpBtn( nullptr )
{
	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(5, 5, 5, 5);
	m_midiInputGroupBox = new GroupBox( tr( "ENABLE MIDI INPUT" ) );
	layout->addWidget( m_midiInputGroupBox );

	auto midiInputLayout = new QHBoxLayout(m_midiInputGroupBox);
	midiInputLayout->setContentsMargins( 8, 18, 8, 8 );
	midiInputLayout->setSpacing( 4 );

	m_inputChannelSpinBox = new LcdSpinBox( 2, m_midiInputGroupBox );
	m_inputChannelSpinBox->addTextForValue( 0, "--" );
	/*: This string must be be short, its width must be less than
	 *  width of LCD spin-box of two digits */
	m_inputChannelSpinBox->setLabel( tr( "CHAN" ) );
	m_inputChannelSpinBox->setEnabled( false );
	midiInputLayout->addWidget( m_inputChannelSpinBox );

	m_fixedInputVelocitySpinBox = new LcdSpinBox( 3, m_midiInputGroupBox );
	m_fixedInputVelocitySpinBox->addTextForValue( -1, "---" );
	/*: This string must be be short, its width must be less than
	 *  width of LCD spin-box of three digits */
	m_fixedInputVelocitySpinBox->setLabel( tr( "VELOC" ) );
	m_fixedInputVelocitySpinBox->setEnabled( false );
	midiInputLayout->addWidget( m_fixedInputVelocitySpinBox );
	midiInputLayout->addStretch();

	connect( m_midiInputGroupBox->ledButton(), SIGNAL(toggled(bool)),
			m_inputChannelSpinBox, SLOT(setEnabled(bool)));
	connect( m_midiInputGroupBox->ledButton(), SIGNAL(toggled(bool)),
		m_fixedInputVelocitySpinBox, SLOT(setEnabled(bool)));



	m_midiOutputGroupBox = new GroupBox( tr( "ENABLE MIDI OUTPUT" ) );
	layout->addWidget( m_midiOutputGroupBox );

	auto midiOutputLayout = new QHBoxLayout(m_midiOutputGroupBox);
	midiOutputLayout->setContentsMargins( 8, 18, 8, 8 );
	midiOutputLayout->setSpacing( 4 );

	m_outputChannelSpinBox = new LcdSpinBox( 2, m_midiOutputGroupBox );
	m_outputChannelSpinBox->addTextForValue( 0, "--" );
	/*: This string must be be short, its width must be less than
	 *  width of LCD spin-box of two digits */
	m_outputChannelSpinBox->setLabel( tr( "CHAN" ) );
	midiOutputLayout->addWidget( m_outputChannelSpinBox );

	m_fixedOutputVelocitySpinBox = new LcdSpinBox( 3, m_midiOutputGroupBox );
	m_fixedOutputVelocitySpinBox->addTextForValue( -1, "---" );
	/*: This string must be be short, its width must be less than
	 *  width of LCD spin-box of three digits */
	m_fixedOutputVelocitySpinBox->setLabel( tr( "VELOC" ) );
	m_fixedOutputVelocitySpinBox->setEnabled( false );
	midiOutputLayout->addWidget( m_fixedOutputVelocitySpinBox );

	m_outputProgramSpinBox = new LcdSpinBox( 3, m_midiOutputGroupBox );
	/*: This string must be be short, its width must be less than the
	 *  width of LCD spin-box of three digits */
	m_outputProgramSpinBox->setLabel( tr( "PROG" ) );
	m_outputProgramSpinBox->setEnabled( false );
	midiOutputLayout->addWidget( m_outputProgramSpinBox );

	m_fixedOutputNoteSpinBox = new LcdSpinBox( 3, m_midiOutputGroupBox );
	m_fixedOutputNoteSpinBox->addTextForValue( -1, "---" );
	/*: This string must be be short, its width must be less than
	 *  width of LCD spin-box of three digits */
	m_fixedOutputNoteSpinBox->setLabel( tr( "NOTE" ) );
	m_fixedOutputNoteSpinBox->setEnabled( false );
	midiOutputLayout->addWidget( m_fixedOutputNoteSpinBox );

	m_midiThruCheckBox = new LedCheckBox(m_midiOutputGroupBox);
	m_midiThruCheckBox->setAccessibleName(tr("MIDI Thru"));
	m_midiThruCheckBox->setToolTip( tr( "Echo notes received from MIDI input to the MIDI output" ) );
	auto midiThruControl = new MidiThruControl(m_midiThruCheckBox, m_fixedOutputNoteSpinBox,
		tr("THR"), m_midiOutputGroupBox);
	midiThruControl->setEnabled(false);
	midiOutputLayout->addWidget(midiThruControl);
	midiOutputLayout->addStretch();

	connect( m_midiOutputGroupBox->ledButton(), SIGNAL(toggled(bool)),
		m_fixedOutputVelocitySpinBox, SLOT(setEnabled(bool)));
	connect( m_midiOutputGroupBox->ledButton(), SIGNAL(toggled(bool)),
			m_outputProgramSpinBox, SLOT(setEnabled(bool)));
	connect( m_midiOutputGroupBox->ledButton(), SIGNAL(toggled(bool)),
		m_fixedOutputNoteSpinBox, SLOT(setEnabled(bool)));
	connect( m_midiOutputGroupBox->ledButton(), SIGNAL(toggled(bool)),
		midiThruControl, SLOT(setEnabled(bool)));

	if( !Engine::audioEngine()->midiClient()->isRaw() )
	{
		m_rpBtn = new QToolButton;
		m_rpBtn->setMinimumSize( 32, 32 );
		m_rpBtn->setText( tr( "MIDI devices to receive MIDI events from" ) );
		m_rpBtn->setIcon( embed::getIconPixmap( "piano" ) );
		m_rpBtn->setPopupMode( QToolButton::InstantPopup );
		midiInputLayout->insertSpacing( 0, 4 );
		midiInputLayout->insertWidget( 0, m_rpBtn );

		m_wpBtn = new QToolButton;
		m_wpBtn->setMinimumSize( 32, 32 );
		m_wpBtn->setText( tr( "MIDI devices to send MIDI events to" ) );
		m_wpBtn->setIcon( embed::getIconPixmap( "piano" ) );
		m_wpBtn->setPopupMode( QToolButton::InstantPopup );
		midiOutputLayout->insertSpacing( 0, 4 );
		midiOutputLayout->insertWidget( 0, m_wpBtn );
	}

	auto baseVelocityGroupBox = new GroupBox(tr("VELOCITY MAPPING"));
	baseVelocityGroupBox->setLedButtonShown(false);
	layout->addWidget( baseVelocityGroupBox );

	auto baseVelocityLayout = new QVBoxLayout(baseVelocityGroupBox);
	baseVelocityLayout->setContentsMargins( 8, 18, 8, 8 );
	baseVelocityLayout->setSpacing( 6 );

	m_baseVelocitySpinBox = new LcdSpinBox( 3, baseVelocityGroupBox );
	m_baseVelocitySpinBox->setLabel(tr("MIDI VELOCITY"));
	m_baseVelocitySpinBox->setToolTip(tr("MIDI notes at this velocity correspond to 100% note velocity."));
	baseVelocityLayout->addWidget( m_baseVelocitySpinBox );

	layout->addStretch();
}





void InstrumentMidiIOView::modelChanged()
{
	auto mp = castModel<MidiPort>();

	m_midiInputGroupBox->setModel( &mp->m_readableModel );
	m_inputChannelSpinBox->setModel( &mp->m_inputChannelModel );
	m_fixedInputVelocitySpinBox->setModel( &mp->m_fixedInputVelocityModel );

	m_midiOutputGroupBox->setModel( &mp->m_writableModel );
	m_outputChannelSpinBox->setModel( &mp->m_outputChannelModel );
	m_fixedOutputVelocitySpinBox->setModel( &mp->m_fixedOutputVelocityModel );
	m_fixedOutputNoteSpinBox->setModel( &mp->m_fixedOutputNoteModel );
	m_outputProgramSpinBox->setModel( &mp->m_outputProgramModel );

	m_baseVelocitySpinBox->setModel( &mp->m_baseVelocityModel );

	if( m_rpBtn )
	{
		m_rpBtn->setMenu( mp->m_readablePortsMenu );
	}
	if( m_wpBtn )
	{
		m_wpBtn->setMenu( mp->m_writablePortsMenu );
	}
}


} // namespace lmms::gui
