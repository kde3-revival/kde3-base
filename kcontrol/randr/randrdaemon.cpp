/*
 * kded_randr: applies the saved display configuration (displayrc) when
 * monitors are connected or disconnected.
 *
 * It reacts only to a change of the set of connected monitors: a
 * configuration made by hand (xrandr, the control module) stays until
 * the next hotplug or login. At login the configuration is applied by
 * krandrstartup from startkde, before the splash screen.
 *
 * It uses its own X connection, so that its RandR event selection and
 * its server grab do not interfere with kded's Qt connection.
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "randrdaemon.h"
#include "randrcore.h"
#include "randrx11.h"

#include <qsocketnotifier.h>

#include <kapplication.h>
#include <kconfig.h>
#include <kdebug.h>
#include <kglobal.h>

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/dpms.h>

using namespace KRandR;

// connection changes come in bursts (a monitor waking up, a dock):
// wait until the outputs settled
static const int settleMs = 2000;
// monitors gone in power save: look again this often
static const int asleepPollMs = 5000;
// after the monitors woke up: the time they get to connect again
static const int wakeUpMs = 10000;

extern "C" {
    KDE_EXPORT KDEDModule *create_randr( const QCString &obj )
    {
	return new RandRDaemon( obj );
    }
}

RandRDaemon::RandRDaemon( const QCString &obj )
    : KDEDModule( obj ), m_dpy( 0 ), m_eventBase( 0 ), m_edidAtom( 0 ),
      m_notifier( 0 ), m_wasAsleep( false )
{
    connect( &m_settle, SIGNAL( timeout() ), SLOT( outputsChanged() ) );

    m_dpy = XOpenDisplay( DisplayString( qt_xdisplay() ) );
    if ( !m_dpy || !hasRandR12( m_dpy, &m_eventBase ) ) {
	kdDebug() << "kded_randr: no RandR 1.2 on the display" << endl;
	if ( m_dpy )
	    XCloseDisplay( m_dpy );
	m_dpy = 0;
	return;
    }
    m_edidAtom = XInternAtom( m_dpy, "EDID", False );
    // output properties: an EDID that is read after the connection
    // changed (a DisplayPort link coming up) changes the monitor's identity
    XRRSelectInput( m_dpy, DefaultRootWindow( m_dpy ),
		    RRScreenChangeNotifyMask | RROutputChangeNotifyMask |
		    RROutputPropertyNotifyMask );
    XFlush( m_dpy );
    m_notifier = new QSocketNotifier( ConnectionNumber( m_dpy ), QSocketNotifier::Read, this );
    connect( m_notifier, SIGNAL( activated( int ) ), SLOT( readEvents() ) );

    // what krandrstartup (or the X server) set up at login is the
    // starting point: nothing to do until it changes
    ScreenState state;
    if ( query( state ) )
	m_lastSet = state.connectedSetKey();
    kdDebug() << "kded_randr: connected monitors: " << m_lastSet << endl;
}

RandRDaemon::~RandRDaemon()
{
    delete m_notifier;
    if ( m_dpy )
	XCloseDisplay( m_dpy );
}

bool RandRDaemon::query( ScreenState &state )
{
    bool ok = m_dpy && queryState( m_dpy, state );
    // the round trips read the events that came meanwhile into Xlib's
    // queue, where the socket notifier does not see them
    if ( m_dpy && XQLength( m_dpy ) > 0 )
	QTimer::singleShot( 0, this, SLOT( readEvents() ) );
    return ok;
}

void RandRDaemon::readEvents()
{
    if ( !m_dpy )
	return;
    bool changed = false;
    while ( XPending( m_dpy ) ) {
	XEvent ev;
	XNextEvent( m_dpy, &ev );
	if ( ev.type == m_eventBase + RRScreenChangeNotify ) {
	    XRRUpdateConfiguration( &ev );
	    changed = true;
	} else if ( ev.type == m_eventBase + RRNotify ) {
	    XRRNotifyEvent *n = (XRRNotifyEvent *) &ev;
	    if ( n->subtype == RRNotify_OutputChange )
		changed = true;
	    else if ( n->subtype == RRNotify_OutputProperty &&
		      ( (XRROutputPropertyNotifyEvent *) &ev )->property == m_edidAtom )
		changed = true;
	}
    }
    if ( changed )
	m_settle.start( settleMs, true );
}

// are the monitors in power save? (DisplayPort monitors disconnect
// when they sleep, and connect again when they wake up)
bool RandRDaemon::monitorsAsleep()
{
    int ev, err;
    CARD16 level = DPMSModeOn;
    BOOL enabled = False;
    if ( !DPMSQueryExtension( m_dpy, &ev, &err ) || !DPMSCapable( m_dpy ) ||
	 !DPMSInfo( m_dpy, &level, &enabled ) )
	return false;
    return enabled && level != DPMSModeOn;
}

static bool isSubset( const QString &set, const QString &of )
{
    QStringList a = QStringList::split( '+', set ), b = QStringList::split( '+', of );
    for ( QStringList::ConstIterator it = a.begin(); it != a.end(); ++it )
	if ( !b.contains( *it ) )
	    return false;
    return true;
}

void RandRDaemon::outputsChanged()
{
    ScreenState state;
    if ( !query( state ) )
	return;
    QString set = state.connectedSetKey();
    // the same monitors: a mode set by hand, our own change, a probe,
    // a monitor that woke up again
    if ( set == m_lastSet ) {
	m_wasAsleep = false;
	return;
    }

    // monitors gone while they sleep: wait until they are awake and
    // had time to connect again; only then are they really gone
    if ( isSubset( set, m_lastSet ) ) {
	if ( monitorsAsleep() ) {
	    kdDebug() << "kded_randr: monitors gone in power save, waiting: " << set << endl;
	    m_wasAsleep = true;
	    m_settle.start( asleepPollMs, true );
	    return;
	}
	if ( m_wasAsleep ) {
	    m_wasAsleep = false;
	    m_settle.start( wakeUpMs, true );
	    return;
	}
    }
    m_wasAsleep = false;

    kdDebug() << "kded_randr: monitors changed: " << m_lastSet << " -> " << set << endl;
    // also when it fails: no retrying in a loop (the next hotplug or
    // login tries again)
    m_lastSet = set;
    if ( set.isEmpty() )
	return;     // everything unplugged: leave the server alone

    KConfig config( "displayrc", true );
    config.setGroup( "General" );
    if ( !config.readBoolEntry( "ApplyOnHotplug", true ) )
	return;
    apply( state );
}

bool RandRDaemon::apply( const ScreenState &state )
{
    KConfig config( "displayrc", true );
    ProfileStore store( &config );
    bool fromProfile = false;
    Layout layout = layoutFor( state, store, true, &fromProfile );
    QString error;
    bool ok = applyLayout( m_dpy, layout, &error );
    if ( ok )
	kdDebug() << "kded_randr: applied the " << ( fromProfile ? "saved" : "default" )
		  << " configuration of " << state.connectedSetKey() << endl;
    else
	kdWarning() << "kded_randr: could not apply the "
		    << ( fromProfile ? "saved" : "default" ) << " configuration of "
		    << state.connectedSetKey() << ": " << error << endl;
    // our own changes are in the queue now; they do not change the set
    readEvents();
    return ok;
}

bool RandRDaemon::reconfigure()
{
    ScreenState state;
    if ( !query( state ) )
	return false;
    m_lastSet = state.connectedSetKey();
    return apply( state );
}

QString RandRDaemon::connectedSet()
{
    ScreenState state;
    if ( !query( state ) )
	return QString::null;
    return state.connectedSetKey();
}

#include "randrdaemon.moc"
