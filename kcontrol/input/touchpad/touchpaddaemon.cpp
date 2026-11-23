/*
 * kded_touchpad (see touchpaddaemon.h).
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "touchpaddaemon.h"
#include "touchpadsettings.h"

#include <qsocketnotifier.h>

#include <kapplication.h>
#include <kdebug.h>

#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

// a device comes with several hierarchy events (added, attached,
// enabled): applied once, after them
static const int settleMs = 100;

extern "C" {
    KDE_EXPORT KDEDModule *create_touchpad( const QCString &obj )
    {
	return new TouchpadDaemon( obj );
    }
}

TouchpadDaemon::TouchpadDaemon( const QCString &obj )
    : KDEDModule( obj ), m_dpy( 0 ), m_opcode( 0 ), m_notifier( 0 )
{
    connect( &m_timer, SIGNAL( timeout() ), SLOT( apply() ) );

    m_dpy = XOpenDisplay( DisplayString( qt_xdisplay() ) );
    if ( !m_dpy )
	return;
    int event, error;
    int major = 2, minor = 0;
    if ( !XQueryExtension( m_dpy, "XInputExtension", &m_opcode, &event, &error )
	 || XIQueryVersion( m_dpy, &major, &minor ) != Success ) {
	kdDebug() << "kded_touchpad: no XInput 2" << endl;
	XCloseDisplay( m_dpy );
	m_dpy = 0;
	return;
    }

    unsigned char bits[XIMaskLen( XI_LASTEVENT )] = { 0 };
    XISetMask( bits, XI_HierarchyChanged );
    XIEventMask mask;
    mask.deviceid = XIAllDevices;
    mask.mask_len = sizeof( bits );
    mask.mask = bits;
    XISelectEvents( m_dpy, DefaultRootWindow( m_dpy ), &mask, 1 );
    XFlush( m_dpy );

    m_notifier = new QSocketNotifier( ConnectionNumber( m_dpy ), QSocketNotifier::Read, this );
    connect( m_notifier, SIGNAL( activated( int ) ), SLOT( readEvents() ) );

    apply();
}

TouchpadDaemon::~TouchpadDaemon()
{
    delete m_notifier;
    if ( m_dpy )
	XCloseDisplay( m_dpy );
}

int TouchpadDaemon::reconfigure()
{
    if ( !m_dpy )
	return 0;
    return KTouchpad::applyAll( m_dpy );
}

void TouchpadDaemon::apply()
{
    const int n = reconfigure();
    kdDebug() << "kded_touchpad: applied to " << n << " touchpad(s)" << endl;
    // the replies read meanwhile may have queued events
    if ( m_dpy && XQLength( m_dpy ) > 0 )
	QTimer::singleShot( 0, this, SLOT( readEvents() ) );
}

void TouchpadDaemon::readEvents()
{
    if ( !m_dpy )
	return;
    bool changed = false;
    while ( XPending( m_dpy ) ) {
	XEvent ev;
	XNextEvent( m_dpy, &ev );
	XGenericEventCookie *cookie = &ev.xcookie;
	if ( cookie->type != GenericEvent || cookie->extension != m_opcode
	     || !XGetEventData( m_dpy, cookie ) )
	    continue;
	if ( cookie->evtype == XI_HierarchyChanged ) {
	    const XIHierarchyEvent *h = static_cast<XIHierarchyEvent *>( cookie->data );
	    if ( h->flags & ( XISlaveAdded | XIDeviceEnabled | XISlaveAttached ) )
		changed = true;
	}
	XFreeEventData( m_dpy, cookie );
    }
    if ( changed )
	m_timer.start( settleMs, true );
}

#include "touchpaddaemon.moc"
