/*
 * Control module for the monitors: arrangement, resolution, refresh rate,
 * rotation and the primary monitor (RandR 1.2).
 *
 * Applying shows the new configuration for 15 seconds and puts the old
 * one back unless it is accepted; an accepted configuration is saved
 * as the profile of the connected monitors (displayrc), which
 * krandrstartup applies at login and kded_randr when these monitors are
 * connected again.
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "displaymodule.h"
#include "monitorcanvas.h"
#include "randrx11.h"
#include "ktimerdialog.h"

#include <qcheckbox.h>
#include <qcombobox.h>
#include <qdesktopwidget.h>
#include <qgroupbox.h>
#include <qlabel.h>
#include <qlayout.h>
#include <qpushbutton.h>
#include <qtimer.h>
#include <qwhatsthis.h>

#include <kaboutdata.h>
#include <kactivelabel.h>
#include <kapplication.h>
#include <kconfig.h>
#include <kdialog.h>
#include <kgenericfactory.h>
#include <kglobalsettings.h>
#include <klocale.h>
#include <kmessagebox.h>

#include <X11/Xlib.h>

using namespace KRandR;

typedef KGenericFactory<DisplayModule, QWidget> DisplayModuleFactory;
K_EXPORT_COMPONENT_FACTORY( kcm_randr, DisplayModuleFactory( "kcmrandr" ) )

extern "C" {
    // shown in the Control Center with RandR 1.2 only
    KDE_EXPORT bool test_randr()
    {
	return hasRandR12( qt_xdisplay() );
    }
}

static const int rotationValues[4] = { Rotate0, Rotate90, Rotate180, Rotate270 };

static QString rotationName( int r )
{
    switch ( r ) {
    case Rotate90: return i18n( "rotation", "Left" );
    case Rotate180: return i18n( "rotation", "Upside down" );
    case Rotate270: return i18n( "rotation", "Right" );
    default: return i18n( "rotation", "Normal" );
    }
}

DisplayModule::DisplayModule( QWidget *parent, const char *name, const QStringList & )
    : KCModule( DisplayModuleFactory::instance(), parent, name ),
      m_available( false ), m_applying( false ), m_updating( false ),
      m_layoutChanged( false ), m_current( -1 )
{
    KAboutData *about = new KAboutData( "kcmrandr", I18N_NOOP( "Monitors" ), 0,
					I18N_NOOP( "Arrangement and resolution of the monitors" ),
					KAboutData::License_GPL, "(c) 2026 Serghei Amelian" );
    setAboutData( about );

    QVBoxLayout *top = new QVBoxLayout( this, 0, KDialog::spacingHint() );

    m_note = new QLabel( this );
    m_note->setAlignment( WordBreak );
    m_note->hide();
    top->addWidget( m_note );

    m_canvas = new MonitorCanvas( this );
    top->addWidget( m_canvas, 1 );
    QWhatsThis::add( m_canvas, i18n( "The monitors as they are arranged. Drag a monitor "
				     "to place it at another side of the others; it snaps "
				     "to their edges." ) );
    connect( m_canvas, SIGNAL( selected( int ) ), SLOT( outputSelected( int ) ) );
    connect( m_canvas, SIGNAL( moved() ), SLOT( monitorMoved() ) );

    QGroupBox *box = new QGroupBox( i18n( "Monitor" ), this );
    box->setColumnLayout( 0, Qt::Vertical );
    box->layout()->setSpacing( KDialog::spacingHint() );
    box->layout()->setMargin( KDialog::marginHint() );
    top->addWidget( box );
    QGridLayout *grid = new QGridLayout( box->layout(), 4, 5, KDialog::spacingHint() );
    grid->setColStretch( 1, 1 );
    grid->setColStretch( 4, 1 );
    grid->addColSpacing( 2, KDialog::spacingHint() * 2 );

    m_output = new QComboBox( false, box );
    grid->addMultiCellWidget( m_output, 0, 0, 0, 3 );
    m_identify = new QPushButton( i18n( "&Identify" ), box );
    grid->addWidget( m_identify, 0, 4, AlignLeft );
    QWhatsThis::add( m_identify, i18n( "Shows the name of each monitor on it for a few seconds." ) );
    connect( m_output, SIGNAL( activated( int ) ), SLOT( outputSelected( int ) ) );
    connect( m_identify, SIGNAL( clicked() ), SLOT( identify() ) );

    m_enabled = new QCheckBox( i18n( "&Enabled" ), box );
    grid->addMultiCellWidget( m_enabled, 1, 1, 0, 1 );
    m_primary = new QCheckBox( i18n( "&Primary monitor" ), box );
    grid->addMultiCellWidget( m_primary, 1, 1, 3, 4 );
    QWhatsThis::add( m_primary, i18n( "The primary monitor holds the main panel; "
				      "new windows open on it." ) );
    connect( m_enabled, SIGNAL( toggled( bool ) ), SLOT( enabledToggled( bool ) ) );
    connect( m_primary, SIGNAL( toggled( bool ) ), SLOT( primaryToggled( bool ) ) );

    QLabel *l = new QLabel( i18n( "&Resolution:" ), box );
    grid->addWidget( l, 2, 0 );
    m_resolution = new QComboBox( false, box );
    grid->addWidget( m_resolution, 2, 1 );
    l->setBuddy( m_resolution );
    l = new QLabel( i18n( "Re&fresh rate:" ), box );
    grid->addWidget( l, 2, 3 );
    m_rate = new QComboBox( false, box );
    grid->addWidget( m_rate, 2, 4 );
    l->setBuddy( m_rate );
    l = new QLabel( i18n( "&Orientation:" ), box );
    grid->addWidget( l, 3, 0 );
    m_rotation = new QComboBox( false, box );
    grid->addWidget( m_rotation, 3, 1 );
    l->setBuddy( m_rotation );
    connect( m_resolution, SIGNAL( activated( int ) ), SLOT( resolutionChanged( int ) ) );
    connect( m_rate, SIGNAL( activated( int ) ), SLOT( rateChanged( int ) ) );
    connect( m_rotation, SIGNAL( activated( int ) ), SLOT( rotationChanged( int ) ) );

    m_hotplug = new QCheckBox( i18n( "Restore the monitor arrangement &automatically" ), this );
    QWhatsThis::add( m_hotplug, i18n( "Remembers how the monitors are arranged, also when "
				      "that is done elsewhere (xrandr, a login script), and "
				      "puts the arrangement back at login and when the same "
				      "monitors are connected again. Monitors connected for "
				      "the first time are put next to the others. Nothing is "
				      "changed while the monitors are in power save.<p>"
				      "Switched off, the arrangement is only changed here." ) );
    top->addWidget( m_hotplug );
    connect( m_hotplug, SIGNAL( toggled( bool ) ), SLOT( hotplugToggled( bool ) ) );

    connect( QApplication::desktop(), SIGNAL( resized( int ) ), SLOT( desktopResized() ) );

    setButtons( Apply | Default | Help );
    load();
}

QString DisplayModule::quickHelp() const
{
    return i18n( "<h1>Monitors</h1> Arrange your monitors, choose their resolution, "
		 "refresh rate and orientation, and which one is the primary monitor. "
		 "The arrangement is saved for the monitors that are connected and used "
		 "again whenever they are connected." );
}

const Output *DisplayModule::currentOutput() const
{
    if ( m_current < 0 || m_current >= (int) m_layout.count() )
	return 0;
    return m_state.outputByKey( m_layout[m_current].key );
}

void DisplayModule::load()
{
    // probing the outputs is slow (and may blink some monitors): only
    // the first time; afterwards the server reports hotplugs itself
    static bool probed = false;
    m_available = queryState( qt_xdisplay(), m_state, !probed );
    probed = true;
    m_layout = m_available ? currentLayout( m_state ) : Layout();
    m_available = m_available && !m_layout.isEmpty();

    KConfig config( "displayrc", true );
    m_updating = true;
    m_hotplug->setChecked( autoRestoreEnabled( &config ) );
    m_updating = false;

    bool saved = false;
    if ( m_available ) {
	ProfileStore store( &config );
	saved = store.hasProfile( m_state.connectedSetKey() );
    }
    if ( !m_available ) {
	m_note->setText( i18n( "The X server does not support the RandR extension 1.2, "
			       "or reports no monitors: the monitors cannot be configured here." ) );
	m_note->show();
    } else if ( !saved ) {
	m_note->setText( i18n( "No arrangement is saved for these monitors yet. "
			       "Apply saves the one shown here." ) );
	m_note->show();
    } else {
	m_note->hide();
    }

    m_output->clear();
    for ( Layout::ConstIterator it = m_layout.begin(); it != m_layout.end(); ++it )
	m_output->insertItem( (*it).model.isEmpty() ? (*it).name
			      : QString( "%1 (%2)" ).arg( (*it).name ).arg( (*it).model ) );
    m_current = 0;
    for ( int i = 0; i < (int) m_layout.count(); ++i )
	if ( m_layout[i].primary )
	    m_current = i;
    if ( m_layout.isEmpty() )
	m_current = -1;
    m_canvas->setMonitors( &m_layout );
    m_canvas->setCurrent( m_current );
    updateControls();

    // nothing saved: Apply saves what is shown (after the caller of
    // load() has reset the changed state)
    m_layoutChanged = m_available && !saved;
    if ( m_layoutChanged )
	QTimer::singleShot( 0, this, SLOT( markUnsaved() ) );
    else
	emit changed( false );
}

void DisplayModule::stillChanged()
{
    // the caller of save() marks the module unchanged afterwards: say
    // it again later
    QTimer::singleShot( 0, this, SLOT( markUnsaved() ) );
}

void DisplayModule::markUnsaved()
{
    if ( m_layoutChanged )
	emit changed( true );
}

void DisplayModule::defaults()
{
    if ( !m_available )
	return;
    m_layout = defaultLayout( m_state );
    m_canvas->setMonitors( &m_layout );
    m_updating = true;
    m_hotplug->setChecked( true );
    m_updating = false;
    updateControls();
    setModuleChanged( true );
}

void DisplayModule::setModuleChanged( bool layoutChanged )
{
    if ( layoutChanged )
	m_layoutChanged = true;
    emit changed( true );
}

void DisplayModule::updateControls()
{
    m_updating = true;
    const Output *o = currentOutput();
    bool have = ( o != 0 );
    m_output->setEnabled( have );
    m_identify->setEnabled( have );
    m_enabled->setEnabled( have );
    m_primary->setEnabled( have );
    m_resolution->setEnabled( have );
    m_rate->setEnabled( have );
    m_rotation->setEnabled( have );
    m_resolution->clear();
    m_rate->clear();
    m_rotation->clear();
    m_sizes.clear();
    m_rotations.clear();
    if ( !have ) {
	m_updating = false;
	return;
    }
    const OutputSetting &s = m_layout[m_current];
    m_output->setCurrentItem( m_current );
    m_enabled->setChecked( s.enabled );
    m_primary->setChecked( s.primary );
    // there is always one primary monitor: choose another one instead
    m_primary->setEnabled( s.enabled && !s.primary );

    // sizes, largest first
    for ( QValueList<XID>::ConstIterator it = o->modes.begin(); it != o->modes.end(); ++it ) {
	const Mode *m = m_state.mode( *it );
	if ( !m )
	    continue;
	QSize sz( m->width, m->height );
	if ( m_sizes.contains( sz ) )
	    continue;
	QValueList<QSize>::Iterator pos = m_sizes.begin();
	while ( pos != m_sizes.end() &&
		( (*pos).width() * (*pos).height() > sz.width() * sz.height() ||
		  ( (*pos).width() * (*pos).height() == sz.width() * sz.height() &&
		    (*pos).width() > sz.width() ) ) )
	    ++pos;
	m_sizes.insert( pos, sz );
    }
    const Mode *preferred = preferredMode( m_state, *o );
    for ( QValueList<QSize>::ConstIterator it = m_sizes.begin(); it != m_sizes.end(); ++it ) {
	QString text = QString( "%1 x %2" ).arg( (*it).width() ).arg( (*it).height() );
	if ( preferred && preferred->width == (*it).width() && preferred->height == (*it).height() )
	    text = i18n( "%1 (recommended)" ).arg( text );
	m_resolution->insertItem( text );
	if ( (*it).width() == s.width && (*it).height() == s.height )
	    m_resolution->setCurrentItem( m_resolution->count() - 1 );
    }
    fillRates();

    int rotations = 0;
    for ( QValueList<XID>::ConstIterator it = o->crtcs.begin(); it != o->crtcs.end(); ++it ) {
	const Crtc *c = m_state.crtc( *it );
	if ( c )
	    rotations |= c->rotations;
    }
    for ( int i = 0; i < 4; ++i ) {
	if ( !( rotations & rotationValues[i] ) && rotationValues[i] != Rotate0 )
	    continue;
	m_rotations.append( rotationValues[i] );
	m_rotation->insertItem( rotationName( rotationValues[i] ) );
	if ( ( s.rotation & RotationMask ) == rotationValues[i] )
	    m_rotation->setCurrentItem( m_rotation->count() - 1 );
    }
    m_resolution->setEnabled( s.enabled );
    m_rate->setEnabled( s.enabled );
    m_rotation->setEnabled( s.enabled && m_rotations.count() > 1 );
    m_updating = false;
}

void DisplayModule::fillRates()
{
    m_rate->clear();
    m_rates.clear();
    const Output *o = currentOutput();
    if ( !o )
	return;
    const OutputSetting &s = m_layout[m_current];
    for ( QValueList<XID>::ConstIterator it = o->modes.begin(); it != o->modes.end(); ++it ) {
	const Mode *m = m_state.mode( *it );
	if ( !m || m->width != s.width || m->height != s.height )
	    continue;
	bool dup = false;
	for ( QValueList<double>::ConstIterator r = m_rates.begin(); r != m_rates.end(); ++r )
	    if ( QABS( *r - m->rate ) < 0.005 )
		dup = true;
	if ( dup )
	    continue;
	QValueList<double>::Iterator pos = m_rates.begin();
	while ( pos != m_rates.end() && *pos > m->rate )
	    ++pos;
	m_rates.insert( pos, m->rate );
    }
    int best = 0;
    for ( int i = 0; i < (int) m_rates.count(); ++i ) {
	m_rate->insertItem( m_rates[i] > 0 ? i18n( "%1 Hz" ).arg( m_rates[i], 0, 'f', 2 )
			    : i18n( "refresh rate", "Unknown" ) );
	if ( QABS( m_rates[i] - s.rate ) < QABS( m_rates[best] - s.rate ) )
	    best = i;
    }
    if ( !m_rates.isEmpty() )
	m_rate->setCurrentItem( best );
}

void DisplayModule::outputSelected( int index )
{
    if ( index < 0 || index >= (int) m_layout.count() )
	return;
    m_current = index;
    m_canvas->setCurrent( index );
    updateControls();
}

void DisplayModule::monitorMoved()
{
    setModuleChanged( true );
}

void DisplayModule::sizeChanged( int bx, int by, int bw, int bh )
{
    adjustNeighbours( m_layout, m_current, bx, by, bw, bh );
    normalizeLayout( m_layout );
    m_canvas->setMonitors( &m_layout );
    m_canvas->setCurrent( m_current );
    setModuleChanged( true );
}

void DisplayModule::enabledToggled( bool on )
{
    if ( m_updating || m_current < 0 )
	return;
    OutputSetting &s = m_layout[m_current];
    if ( s.enabled == on )
	return;
    QRect before = s.enabled ? MonitorCanvas::monitorRect( s ) : QRect();
    if ( !on ) {
	int enabled = 0;
	for ( Layout::ConstIterator it = m_layout.begin(); it != m_layout.end(); ++it )
	    if ( (*it).enabled )
		++enabled;
	if ( enabled <= 1 ) {
	    // the last one stays on
	    m_updating = true;
	    m_enabled->setChecked( true );
	    m_updating = false;
	    return;
	}
    }
    s.enabled = on;
    if ( on ) {
	if ( s.width <= 0 ) {
	    const Output *o = currentOutput();
	    const Mode *m = o ? preferredMode( m_state, *o ) : 0;
	    if ( m ) {
		s.width = m->width;
		s.height = m->height;
		s.rate = m->rate;
	    }
	}
	placeAtRight( m_layout, m_current );
	sizeChanged( 0, 0, 0, 0 );
    } else {
	if ( s.primary ) {
	    // the first enabled one becomes primary
	    s.primary = false;
	    for ( Layout::Iterator it = m_layout.begin(); it != m_layout.end(); ++it ) {
		if ( (*it).enabled ) {
		    (*it).primary = true;
		    break;
		}
	    }
	}
	sizeChanged( before.x(), before.y(), before.width(), before.height() );
    }
    updateControls();
}

void DisplayModule::primaryToggled( bool on )
{
    if ( m_updating || m_current < 0 || !on )
	return;
    for ( int i = 0; i < (int) m_layout.count(); ++i )
	m_layout[i].primary = ( i == m_current );
    m_canvas->update();
    updateControls();
    setModuleChanged( true );
}

void DisplayModule::resolutionChanged( int index )
{
    if ( m_updating || m_current < 0 || index < 0 || index >= (int) m_sizes.count() )
	return;
    OutputSetting &s = m_layout[m_current];
    QRect before = MonitorCanvas::monitorRect( s );
    s.width = m_sizes[index].width();
    s.height = m_sizes[index].height();
    s.rate = 0;
    fillRates();
    if ( !m_rates.isEmpty() )
	s.rate = m_rates.first();
    sizeChanged( before.x(), before.y(), before.width(), before.height() );
    updateControls();
}

void DisplayModule::rateChanged( int index )
{
    if ( m_updating || m_current < 0 || index < 0 || index >= (int) m_rates.count() )
	return;
    m_layout[m_current].rate = m_rates[index];
    setModuleChanged( true );
}

void DisplayModule::rotationChanged( int index )
{
    if ( m_updating || m_current < 0 || index < 0 || index >= (int) m_rotations.count() )
	return;
    OutputSetting &s = m_layout[m_current];
    QRect before = MonitorCanvas::monitorRect( s );
    s.rotation = m_rotations[index] | ( s.rotation & ReflectMask );
    sizeChanged( before.x(), before.y(), before.width(), before.height() );
}

void DisplayModule::hotplugToggled( bool )
{
    if ( !m_updating )
	setModuleChanged( false );
}

bool DisplayModule::confirm()
{
    KTimerDialog dlg( 15000, KTimerDialog::CountDown, this, "confirmDisplay", true,
		      i18n( "Confirm Display Setting Change" ),
		      KTimerDialog::Ok | KTimerDialog::Cancel, KTimerDialog::Cancel );
    dlg.setButtonOK( KGuiItem( i18n( "&Keep These Settings" ), "button_ok" ) );
    dlg.setButtonCancel( KGuiItem( i18n( "&Revert" ), "button_cancel" ) );
    KActiveLabel *label = new KActiveLabel(
	i18n( "The monitors have been arranged as requested. Do you want to keep "
	      "these settings? In 15 seconds the previous settings will be restored." ),
	&dlg, "confirmLabel" );
    dlg.setMainWidget( label );
    dlg.adjustSize();
    // on the primary monitor of the new arrangement
    QApplication::syncX();
    kapp->processEvents();
    KDialog::centerOnScreen( &dlg, QApplication::desktop()->primaryScreen() );
    return dlg.exec() == KTimerDialog::Accepted;
}

void DisplayModule::save()
{
    if ( !m_available )
	return;

    KConfig config( "displayrc" );
    config.setGroup( "General" );
    config.writeEntry( "AutoRestore", m_hotplug->isChecked() );
    config.sync();

    if ( !m_layoutChanged ) {
	emit changed( false );
	return;
    }

    Layout layout = m_layout;
    normalizeLayout( layout );
    QString error;
    if ( !validateLayout( layout, &error ) ) {
	KMessageBox::sorry( this, error );
	stillChanged();
	return;
    }

    Display *dpy = qt_xdisplay();
    ScreenState now;
    if ( !queryState( dpy, now ) ) {
	KMessageBox::sorry( this, i18n( "Could not read the display configuration." ) );
	stillChanged();
	return;
    }
    if ( now.connectedSetKey() != m_state.connectedSetKey() ) {
	KMessageBox::sorry( this, i18n( "The connected monitors changed. Please check the "
					"arrangement again." ) );
	load();
	return;
    }
    Plan plan = makePlan( now, layout );
    if ( !plan.error.isEmpty() ) {
	KMessageBox::sorry( this, plan.error );
	stillChanged();
	return;
    }

    m_applying = true;
    bool keep = true;
    if ( !plan.isEmpty() ) {
	Layout before = currentLayout( now );
	if ( !applyLayout( dpy, layout, &error ) ) {
	    m_applying = false;
	    KMessageBox::sorry( this, i18n( "The monitors could not be configured: %1" )
				.arg( error ) );
	    load();
	    return;
	}
	keep = confirm();
	if ( !keep )
	    if ( !applyLayout( dpy, before, &error ) )
		KMessageBox::sorry( this, i18n( "The previous configuration could not be "
						"restored: %1" ).arg( error ) );
    }
    if ( keep ) {
	ProfileStore store( &config );
	store.saveProfile( now.connectedSetKey(), layout );
    }
    QApplication::syncX();
    kapp->processEvents();
    m_applying = false;
    // reverted: nothing saved, Apply stays available (see load())
    load();
}

void DisplayModule::desktopResized()
{
    if ( m_applying )
	return;
    // monitors connected or disconnected (kded_randr may have arranged
    // them): start again from what the server has
    ScreenState now;
    if ( queryState( qt_xdisplay(), now ) && now.connectedSetKey() != m_state.connectedSetKey() )
	load();
}

// --- identify ----------------------------------------------------------------

namespace {
class IdentifyLabel : public QLabel
{
public:
    IdentifyLabel( const QString &text, const QRect &screen )
	: QLabel( 0, "identify", WStyle_Customize | WStyle_NoBorder | WStyle_StaysOnTop |
		  WX11BypassWM | WDestructiveClose )
    {
	QFont f = KGlobalSettings::generalFont();
	f.setPointSize( f.pointSize() * 3 );
	f.setBold( true );
	setFont( f );
	setText( text );
	setAlignment( AlignCenter );
	setMargin( 24 );
	setFrameStyle( QFrame::Box | QFrame::Plain );
	setLineWidth( 3 );
	setPaletteBackgroundColor( KGlobalSettings::highlightColor() );
	setPaletteForegroundColor( KGlobalSettings::highlightedTextColor() );
	adjustSize();
	move( screen.x() + ( screen.width() - width() ) / 2,
	      screen.y() + ( screen.height() - height() ) / 2 );
	QTimer::singleShot( 3000, this, SLOT( close() ) );
    }
};
}

void DisplayModule::identify()
{
    ScreenState now;
    if ( !queryState( qt_xdisplay(), now ) )
	return;
    QValueList<Output> outs = now.connectedOutputs();
    for ( QValueList<Output>::ConstIterator it = outs.begin(); it != outs.end(); ++it ) {
	const Crtc *c = (*it).crtc ? now.crtc( (*it).crtc ) : 0;
	if ( !c || !c->mode )
	    continue;
	QString text = (*it).name;
	if ( !(*it).model.isEmpty() )
	    text += "\n" + (*it).model;
	( new IdentifyLabel( text, QRect( c->x, c->y, c->width, c->height ) ) )->show();
    }
}

#include "displaymodule.moc"
