/*
 * The Touchpad tab of the Mouse control module (see touchpadpage.h).
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "touchpadpage.h"

#include <qcheckbox.h>
#include <qcombobox.h>
#include <qgroupbox.h>
#include <qhbox.h>
#include <qlabel.h>
#include <qlayout.h>
#include <qradiobutton.h>
#include <qstringlist.h>
#include <qvbuttongroup.h>
#include <qvgroupbox.h>
#include <qwhatsthis.h>

#include <kapplication.h>
#include <kconfig.h>
#include <kdialog.h>
#include <klocale.h>
#include <knuminput.h>

using namespace KTouchpad;

TouchpadPage::TouchpadPage( QWidget *parent, const char *name )
    : QWidget( parent, name ), m_named( 0 ), m_features( 0 ), m_scrollMethods( 0 ), m_libinput( false )
{
    m_devices = touchpads( qt_xdisplay() );
    unsigned clickMethods = 0, accelProfiles = 0;
    QStringList names, others;
    bool first = true;
    for ( DeviceList::ConstIterator it = m_devices.begin(); it != m_devices.end(); ++it ) {
	if ( !( *it ).libinput ) {
	    others << ( *it ).name;
	    continue;
	}
	names << ( *it ).name;
	if ( first ) {
	    m_defaults = defaults( qt_xdisplay(), *it );
	    first = false;
	}
	m_libinput = true;
	m_features |= ( *it ).features;
	m_scrollMethods |= ( *it ).scrollMethods;
	clickMethods |= ( *it ).clickMethods;
	accelProfiles |= ( *it ).accelProfiles;
    }

    QVBoxLayout *top = new QVBoxLayout( this, KDialog::marginHint(), KDialog::spacingHint() );

    QLabel *status = new QLabel( this );
    status->setAlignment( Qt::WordBreak | Qt::AlignVCenter );
    top->addWidget( status );
    if ( m_devices.isEmpty() )
	status->setText( i18n( "No touchpad was found." ) );
    else if ( !m_libinput )
	status->setText( i18n( "The touchpad %1 is not driven by the libinput X driver "
			       "(xf86-input-libinput): its settings cannot be changed here." )
			 .arg( others.join( ", " ) ) );
    else
	status->setText( i18n( "Touchpad: %1" ).arg( names.join( ", " ) ) );

    QWidget *settings = new QWidget( this );
    top->addWidget( settings );
    top->addStretch( 1 );
    QGridLayout *grid = new QGridLayout( settings, 3, 2, 0, KDialog::spacingHint() );

    // tapping
    QVGroupBox *tapping = new QVGroupBox( i18n( "Tapping" ), settings );
    grid->addWidget( tapping, 0, 0 );
    m_tap = new QCheckBox( i18n( "T&ap to click" ), tapping );
    QWhatsThis::add( m_tap, i18n( "A short tap on the touchpad clicks, as the left button does." ) );
    m_tapDrag = new QCheckBox( i18n( "Tap and &drag" ), tapping );
    QWhatsThis::add( m_tapDrag, i18n( "Tap, then put the finger down again at once and move it: "
				      "this drags, as moving with the left button pressed does." ) );
    m_tapDragLock = new QCheckBox( i18n( "Drag &lock" ), tapping );
    QWhatsThis::add( m_tapDragLock, i18n( "While dragging with a tap, lifting the finger for a moment "
					  "does not end the drag." ) );
    QHBox *mapBox = new QHBox( tapping );
    mapBox->setSpacing( KDialog::spacingHint() );
    QLabel *mapLabel = new QLabel( i18n( "&Multi-finger tap:" ), mapBox );
    m_tapButtonMap = new QComboBox( false, mapBox );
    m_tapButtonMap->insertItem( i18n( "two fingers right button, three middle" ) );
    m_tapButtonMap->insertItem( i18n( "two fingers middle button, three right" ) );
    mapLabel->setBuddy( m_tapButtonMap );

    // scrolling
    QVButtonGroup *scrolling = new QVButtonGroup( i18n( "Scrolling" ), settings );
    grid->addWidget( scrolling, 0, 1 );
    m_scrollTwoFinger = new QRadioButton( i18n( "T&wo-finger scrolling" ), scrolling );
    QWhatsThis::add( m_scrollTwoFinger, i18n( "Move two fingers on the touchpad to scroll." ) );
    m_scrollEdge = new QRadioButton( i18n( "&Edge scrolling" ), scrolling );
    QWhatsThis::add( m_scrollEdge, i18n( "Move one finger along the right edge (or the bottom edge) "
					 "of the touchpad to scroll." ) );
    m_scrollNone = new QRadioButton( i18n( "N&o scrolling" ), scrolling );
    m_naturalScroll = new QCheckBox( i18n( "&Natural scrolling" ), scrolling );
    QWhatsThis::add( m_naturalScroll, i18n( "The content moves with the fingers, as on a touch screen, "
					    "instead of the scroll bar." ) );

    // pointer
    QVGroupBox *pointer = new QVGroupBox( i18n( "Pointer" ), settings );
    grid->addWidget( pointer, 1, 0 );
    m_accelSpeed = new KDoubleNumInput( -1.0, 1.0, 0.0, 0.05, 2, pointer );
    m_accelSpeed->setRange( -1.0, 1.0, 0.05, true );
    m_accelSpeed->setLabel( i18n( "&Speed:" ), AlignLeft | AlignVCenter );
    QWhatsThis::add( m_accelSpeed, i18n( "How fast the pointer moves with the touchpad, from -1 (slowest) "
					 "to 1 (fastest); the mouse has its own setting." ) );
    QHBox *profileBox = new QHBox( pointer );
    profileBox->setSpacing( KDialog::spacingHint() );
    QLabel *profileLabel = new QLabel( i18n( "Accele&ration:" ), profileBox );
    m_accelProfile = new QComboBox( false, profileBox );
    profileLabel->setBuddy( m_accelProfile );
    if ( accelProfiles & ( 1u << AccelAdaptive ) || !accelProfiles ) {
	m_accelProfile->insertItem( i18n( "adaptive (faster movements go farther)" ) );
	m_accelProfiles << AccelAdaptive;
    }
    if ( accelProfiles & ( 1u << AccelFlat ) ) {
	m_accelProfile->insertItem( i18n( "flat (the same at every speed)" ) );
	m_accelProfiles << AccelFlat;
    }

    // clicking
    QVGroupBox *clicking = new QVGroupBox( i18n( "Clicking" ), settings );
    grid->addWidget( clicking, 1, 1 );
    QHBox *clickBox = new QHBox( clicking );
    clickBox->setSpacing( KDialog::spacingHint() );
    QLabel *clickLabel = new QLabel( i18n( "&Buttons:" ), clickBox );
    m_clickMethod = new QComboBox( false, clickBox );
    clickLabel->setBuddy( m_clickMethod );
    QWhatsThis::add( m_clickMethod, i18n( "What pressing the touchpad down clicks. <b>Areas</b>: the "
					  "bottom right corner is the right button, the bottom middle the "
					  "middle button. <b>Fingers</b>: pressing with two fingers is the "
					  "right button, with three the middle button." ) );
    if ( clickMethods & ( 1u << ClickButtonAreas ) ) {
	m_clickMethod->insertItem( i18n( "areas at the bottom" ) );
	m_clickMethods << ClickButtonAreas;
    }
    if ( clickMethods & ( 1u << ClickFinger ) ) {
	m_clickMethod->insertItem( i18n( "number of fingers" ) );
	m_clickMethods << ClickFinger;
    }
    m_clickMethod->insertItem( i18n( "left button only" ) );
    m_clickMethods << ClickNone;
    m_middleEmulation = new QCheckBox( i18n( "Middle b&utton: press left and right together" ), clicking );

    // turning off
    QVGroupBox *off = new QVGroupBox( i18n( "Turning Off" ), settings );
    grid->addMultiCellWidget( off, 2, 2, 0, 1 );
    m_disableWhileTyping = new QCheckBox( i18n( "Turn off while t&yping" ), off );
    QWhatsThis::add( m_disableWhileTyping, i18n( "The touchpad ignores the palm while you type on the "
						 "laptop's keyboard, and for a moment after." ) );
    m_disableOnExternalMouse = new QCheckBox( i18n( "Turn off while an e&xternal mouse is connected" ), off );

    if ( !m_libinput )
	settings->hide();

    // the device's lacks
    m_tap->setEnabled( has( HasTap ) );
    m_tapButtonMap->setEnabled( has( HasTapButtonMap ) );
    mapLabel->setEnabled( has( HasTapButtonMap ) );
    scrolling->setEnabled( has( HasScrollMethod ) || has( HasNaturalScroll ) );
    m_scrollTwoFinger->setEnabled( m_scrollMethods & ( 1u << ScrollTwoFinger ) );
    m_scrollEdge->setEnabled( m_scrollMethods & ( 1u << ScrollEdge ) );
    m_scrollNone->setEnabled( has( HasScrollMethod ) );
    m_naturalScroll->setEnabled( has( HasNaturalScroll ) );
    m_accelSpeed->setEnabled( has( HasAccelSpeed ) );
    m_accelProfile->setEnabled( has( HasAccelProfile ) );
    profileLabel->setEnabled( has( HasAccelProfile ) );
    m_clickMethod->setEnabled( has( HasClickMethod ) );
    clickLabel->setEnabled( has( HasClickMethod ) );
    m_middleEmulation->setEnabled( has( HasMiddleEmulation ) );
    m_disableWhileTyping->setEnabled( has( HasDisableWhileTyping ) );
    m_disableOnExternalMouse->setEnabled( has( HasDisableOnExternalMouse ) );

    QCheckBox *boxes[] = { m_tap, m_tapDrag, m_tapDragLock, m_naturalScroll, m_middleEmulation,
			   m_disableWhileTyping, m_disableOnExternalMouse };
    for ( unsigned i = 0; i < sizeof( boxes ) / sizeof( boxes[0] ); ++i )
	connect( boxes[i], SIGNAL( clicked() ), SIGNAL( changed() ) );
    connect( m_tap, SIGNAL( toggled( bool ) ), SLOT( updateEnabled() ) );
    connect( m_tapDrag, SIGNAL( toggled( bool ) ), SLOT( updateEnabled() ) );
    connect( scrolling, SIGNAL( clicked( int ) ), SIGNAL( changed() ) );
    connect( m_tapButtonMap, SIGNAL( activated( int ) ), SIGNAL( changed() ) );
    connect( m_accelProfile, SIGNAL( activated( int ) ), SIGNAL( changed() ) );
    connect( m_clickMethod, SIGNAL( activated( int ) ), SIGNAL( changed() ) );
    connect( m_accelSpeed, SIGNAL( valueChanged( double ) ), SIGNAL( changed() ) );
}

void TouchpadPage::updateEnabled()
{
    const bool tap = m_tap->isChecked() || !has( HasTap );
    m_tapDrag->setEnabled( has( HasTapDrag ) && tap );
    m_tapDragLock->setEnabled( has( HasTapDragLock ) && tap && m_tapDrag->isChecked() );
}

void TouchpadPage::load( bool useDefaults )
{
    if ( useDefaults ) {
	m_loaded = m_defaults;
	m_named = 0;
    } else {
	KConfig config( "touchpadrc", true, false );
	m_loaded = KTouchpad::load( &config, m_defaults, &m_named );
    }
    toWidgets( m_loaded );
}

void TouchpadPage::save()
{
    if ( !m_libinput )
	return;
    // what touchpadrc named stays named, and so does what was changed
    // here, even back to the default of the touchpad shown: another
    // touchpad's default may differ
    const Settings s = fromWidgets();
    m_named |= differences( s, m_loaded );
    m_loaded = s;
    KConfig config( "touchpadrc", false, false );
    KTouchpad::save( &config, m_loaded, m_named );
    applyAll( qt_xdisplay() );
}

void TouchpadPage::toWidgets( const Settings &s )
{
    m_tap->setChecked( s.tap );
    m_tapDrag->setChecked( s.tapDrag );
    m_tapDragLock->setChecked( s.tapDragLock );
    m_tapButtonMap->setCurrentItem( s.tapButtonMap == TapLeftMiddleRight ? 1 : 0 );
    m_scrollTwoFinger->setChecked( s.scrollMethod == ScrollTwoFinger );
    m_scrollEdge->setChecked( s.scrollMethod == ScrollEdge );
    m_scrollNone->setChecked( s.scrollMethod == ScrollNone );
    m_naturalScroll->setChecked( s.naturalScroll );
    m_accelSpeed->setValue( s.accelSpeed );
    const int profile = m_accelProfiles.findIndex( s.accelProfile );
    if ( profile >= 0 )
	m_accelProfile->setCurrentItem( profile );
    const int click = m_clickMethods.findIndex( s.clickMethod );
    if ( click >= 0 )
	m_clickMethod->setCurrentItem( click );
    m_middleEmulation->setChecked( s.middleEmulation );
    m_disableWhileTyping->setChecked( s.disableWhileTyping );
    m_disableOnExternalMouse->setChecked( s.disableOnExternalMouse );
    updateEnabled();
}

Settings TouchpadPage::fromWidgets() const
{
    // what a device lacks keeps its loaded value
    Settings s = m_loaded;
    if ( has( HasTap ) )
	s.tap = m_tap->isChecked();
    if ( has( HasTapDrag ) )
	s.tapDrag = m_tapDrag->isChecked();
    if ( has( HasTapDragLock ) )
	s.tapDragLock = m_tapDragLock->isChecked();
    if ( has( HasTapButtonMap ) )
	s.tapButtonMap = m_tapButtonMap->currentItem() == 1 ? TapLeftMiddleRight : TapLeftRightMiddle;
    if ( has( HasScrollMethod ) ) {
	if ( m_scrollTwoFinger->isChecked() )
	    s.scrollMethod = ScrollTwoFinger;
	else if ( m_scrollEdge->isChecked() )
	    s.scrollMethod = ScrollEdge;
	else if ( m_scrollNone->isChecked() )
	    s.scrollMethod = ScrollNone;
    }
    if ( has( HasNaturalScroll ) )
	s.naturalScroll = m_naturalScroll->isChecked();
    if ( has( HasAccelSpeed ) )
	s.accelSpeed = m_accelSpeed->value();
    if ( has( HasAccelProfile ) && m_accelProfile->currentItem() >= 0
	 && m_accelProfile->currentItem() < int( m_accelProfiles.count() ) )
	s.accelProfile = m_accelProfiles[m_accelProfile->currentItem()];
    if ( has( HasClickMethod ) && m_clickMethod->currentItem() >= 0
	 && m_clickMethod->currentItem() < int( m_clickMethods.count() ) )
	s.clickMethod = m_clickMethods[m_clickMethod->currentItem()];
    if ( has( HasMiddleEmulation ) )
	s.middleEmulation = m_middleEmulation->isChecked();
    if ( has( HasDisableWhileTyping ) )
	s.disableWhileTyping = m_disableWhileTyping->isChecked();
    if ( has( HasDisableOnExternalMouse ) )
	s.disableOnExternalMouse = m_disableOnExternalMouse->isChecked();
    return s;
}

#include "touchpadpage.moc"
