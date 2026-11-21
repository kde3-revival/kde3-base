/*
 * kded_randr: keeps the arrangement of the monitors (displayrc) when
 * monitors are connected or disconnected, and remembers the arrangement
 * the user makes (see randrpolicy.h for the rules).
 *
 * It never changes the configuration when it is loaded, at login or in
 * the middle of a session; at login the configuration is applied by
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

#include <time.h>

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/dpms.h>

using namespace KRandR;

// connection changes come in bursts (a monitor waking up, a dock):
// wait until the outputs settled
static const int settleMs = 2000;

static long monotonicMs()
{
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

extern "C" {
    KDE_EXPORT KDEDModule *create_randr( const QCString &obj )
    {
	return new RandRDaemon( obj );
    }
}

RandRDaemon::RandRDaemon( const QCString &obj )
    : KDEDModule( obj ), m_dpy( 0 ), m_eventBase( 0 ), m_edidAtom( 0 ),
      m_notifier( 0 ), m_connectionChanged( false ), m_lastEvent( 0 )
{
    connect( &m_timer, SIGNAL( timeout() ), SLOT( decide() ) );

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

    ScreenState state;
    if ( query( state ) )
	for ( QValueList<Output>::ConstIterator it = state.outputs.begin();
	      it != state.outputs.end(); ++it )
	    m_connected[(*it).id] = (*it).connected;
    m_lastEvent = monotonicMs();
    // only looks at the arrangement (and remembers it if the monitors
    // have none yet), when they are awake
    decide();
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
	    if ( n->subtype == RRNotify_OutputChange ) {
		XRROutputChangeNotifyEvent *o = (XRROutputChangeNotifyEvent *) &ev;
		bool connected = o->connection == RR_Connected;
		if ( !m_connected.contains( o->output ) || m_connected[o->output] != connected )
		    m_connectionChanged = true;
		m_connected[o->output] = connected;
		changed = true;
	    } else if ( n->subtype == RRNotify_OutputProperty &&
			( (XRROutputPropertyNotifyEvent *) &ev )->property == m_edidAtom ) {
		changed = true;
	    }
	}
    }
    if ( changed ) {
	m_lastEvent = monotonicMs();
	m_timer.start( settleMs, true );
    }
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
    // the level, also when DPMS is disabled (forced off)
    return level != DPMSModeOn;
}

bool RandRDaemon::enabled()
{
    KConfig config( "displayrc", true );
    return autoRestoreEnabled( &config );
}

void RandRDaemon::decide()
{
    m_timer.stop();
    ScreenState state;
    if ( !query( state ) )
	return;
    bool asleep = monitorsAsleep();
    bool connectionChanged = m_connectionChanged;
    m_connectionChanged = false;

    m_policy.setEnabled( enabled() );
    KConfig config( "displayrc" );
    ProfileStore store( &config );
    HotplugPolicy::Action a = m_policy.decide( state, asleep, monotonicMs(), m_lastEvent,
					       connectionChanged, store );
    if ( !a.message.isEmpty() )
	kdDebug() << "kded_randr: " << a.message << endl;
    if ( a.type == HotplugPolicy::Action::Wait )
	m_timer.start( a.waitMs, true );
    else if ( a.type == HotplugPolicy::Action::Apply )
	apply( a.layout, state.connectedSetKey() );
}

bool RandRDaemon::apply( const Layout &layout, const QString &set )
{
    QString error;
    bool ok = applyLayout( m_dpy, layout, &error, set );
    if ( !ok )
	kdWarning() << "kded_randr: could not arrange " << set << ": " << error << endl;
    // also after a failure: no retrying in a loop (the next change of
    // the monitors or login tries again)
    ScreenState after;
    if ( query( after ) )
	m_policy.applied( after );
    // our own changes are in the queue now
    readEvents();
    return ok;
}

bool RandRDaemon::reconfigure()
{
    ScreenState state;
    if ( !query( state ) )
	return false;
    KConfig config( "displayrc", true );
    ProfileStore store( &config );
    return apply( m_policy.reconfigure( state, store ), state.connectedSetKey() );
}

QString RandRDaemon::connectedSet()
{
    ScreenState state;
    if ( !query( state ) )
	return QString::null;
    return state.connectedSetKey();
}

#include "randrdaemon.moc"
