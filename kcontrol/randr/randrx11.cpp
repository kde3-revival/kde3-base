/*
 * RandR 1.2+ display configuration: X server side.
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "randrx11.h"

#include <klocale.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>

namespace KRandR {

bool hasRandR12( Display *dpy, int *eventBase )
{
    int ev, err, major = 0, minor = 0;
    if ( !dpy || !XRRQueryExtension( dpy, &ev, &err ) )
	return false;
    if ( !XRRQueryVersion( dpy, &major, &minor ) )
	return false;
    if ( eventBase )
	*eventBase = ev;
    return major > 1 || ( major == 1 && minor >= 2 );
}

static QByteArray outputProperty( Display *dpy, RROutput output, const char *name,
				  int *format = 0 )
{
    QByteArray result;
    Atom atom = XInternAtom( dpy, name, True );
    if ( atom == None )
	return result;
    Atom type;
    int fmt = 0;
    unsigned long nitems = 0, after = 0;
    unsigned char *data = 0;
    if ( XRRGetOutputProperty( dpy, output, atom, 0, 256, False, False,
			       AnyPropertyType, &type, &fmt, &nitems, &after,
			       &data ) == Success && data ) {
	if ( fmt == 8 && nitems > 0 ) {
	    result.duplicate( (const char *) data, nitems );
	} else if ( fmt == 32 && nitems > 0 ) {
	    // longs in Xlib
	    long v = *(long *) data;
	    result.duplicate( (const char *) &v, sizeof( v ) );
	}
    }
    if ( data )
	XFree( data );
    if ( format )
	*format = fmt;
    return result;
}

static bool queryState( Display *dpy, ScreenState &state, XRRScreenResources *res )
{
    Window root = DefaultRootWindow( dpy );
    state = ScreenState();
    state.timestamp = res->configTimestamp;

    if ( !XRRGetScreenSizeRange( dpy, root, &state.minWidth, &state.minHeight,
				 &state.maxWidth, &state.maxHeight ) )
	return false;
    Window r;
    int x, y;
    unsigned int w, h, bw, depth;
    if ( XGetGeometry( dpy, root, &r, &x, &y, &w, &h, &bw, &depth ) ) {
	state.width = w;
	state.height = h;
    }
    int scr = DefaultScreen( dpy );
    state.mmWidth = DisplayWidthMM( dpy, scr );
    state.mmHeight = DisplayHeightMM( dpy, scr );
    // Xlib's cached size can lag behind; scale the millimetres with it
    if ( DisplayWidth( dpy, scr ) > 0 && DisplayWidth( dpy, scr ) != state.width )
	state.mmWidth = state.mmWidth * state.width / DisplayWidth( dpy, scr );
    if ( DisplayHeight( dpy, scr ) > 0 && DisplayHeight( dpy, scr ) != state.height )
	state.mmHeight = state.mmHeight * state.height / DisplayHeight( dpy, scr );

    for ( int i = 0; i < res->nmode; ++i ) {
	const XRRModeInfo &mi = res->modes[i];
	Mode m;
	m.id = mi.id;
	m.width = mi.width;
	m.height = mi.height;
	double v = mi.vTotal;
	if ( mi.modeFlags & RR_DoubleScan )
	    v *= 2;
	if ( mi.modeFlags & RR_Interlace )
	    v /= 2;
	m.rate = ( mi.hTotal && v > 0 ) ? mi.dotClock / ( mi.hTotal * v ) : 0;
	state.modes.insert( m.id, m );
    }

    RROutput primary = XRRGetOutputPrimary( dpy, root );
    QMap<XID, EdidInfo> edids;
    for ( int i = 0; i < res->noutput; ++i ) {
	XRROutputInfo *oi = XRRGetOutputInfo( dpy, res, res->outputs[i] );
	if ( !oi )
	    continue;
	Output o;
	o.id = res->outputs[i];
	o.name = QString::fromLatin1( oi->name, oi->nameLen );
	o.connected = oi->connection == RR_Connected;
	o.mmWidth = oi->mm_width;
	o.mmHeight = oi->mm_height;
	for ( int j = 0; j < oi->nmode; ++j )
	    o.modes.append( oi->modes[j] );
	o.preferredCount = oi->npreferred;
	for ( int j = 0; j < oi->ncrtc; ++j )
	    o.crtcs.append( oi->crtcs[j] );
	o.crtc = oi->crtc;
	o.primary = ( o.id == primary );
	XRRFreeOutputInfo( oi );

	if ( o.connected ) {
	    QByteArray edid = outputProperty( dpy, o.id, "EDID" );
	    if ( edid.size() >= 128 )
		edids.insert( o.id, parseEdid( (const unsigned char *) edid.data(), edid.size() ) );
	    int fmt = 0;
	    QByteArray nd = outputProperty( dpy, o.id, "non-desktop", &fmt );
	    if ( fmt == 32 && nd.size() == sizeof( long ) )
		o.nonDesktop = *(const long *) nd.data() != 0;
	}
	state.outputs.append( o );
    }
    assignKeys( state, edids );

    for ( int i = 0; i < res->ncrtc; ++i ) {
	XRRCrtcInfo *ci = XRRGetCrtcInfo( dpy, res, res->crtcs[i] );
	if ( !ci )
	    continue;
	Crtc c;
	c.id = res->crtcs[i];
	c.x = ci->x;
	c.y = ci->y;
	c.width = ci->width;
	c.height = ci->height;
	c.mode = ci->mode;
	c.rotation = ci->rotation;
	c.rotations = ci->rotations;
	for ( int j = 0; j < ci->noutput; ++j )
	    c.outputs.append( ci->outputs[j] );
	for ( int j = 0; j < ci->npossible; ++j )
	    c.possible.append( ci->possible[j] );
	XRRFreeCrtcInfo( ci );
	state.crtcs.append( c );
    }
    return true;
}

bool queryState( Display *dpy, ScreenState &state, bool probe )
{
    if ( !hasRandR12( dpy ) )
	return false;
    XRRScreenResources *res = probe
	? XRRGetScreenResources( dpy, DefaultRootWindow( dpy ) )
	: XRRGetScreenResourcesCurrent( dpy, DefaultRootWindow( dpy ) );
    if ( !res )
	return false;
    if ( !probe && res->noutput == 0 ) {
	// the server has not looked for outputs yet (Xephyr right after
	// it started)
	XRRFreeScreenResources( res );
	res = XRRGetScreenResources( dpy, DefaultRootWindow( dpy ) );
	if ( !res )
	    return false;
    }
    bool ok = queryState( dpy, state, res );
    XRRFreeScreenResources( res );
    return ok;
}

bool executePlan( PlanExecutor &ex, const Plan &plan, QString *error )
{
    if ( !plan.error.isEmpty() ) {
	if ( error )
	    *error = plan.error;
	return false;
    }
    for ( QValueList<CrtcOp>::ConstIterator it = plan.disable.begin(); it != plan.disable.end(); ++it ) {
	if ( !ex.setCrtc( *it ) ) {
	    if ( error )
		*error = i18n( "Could not switch off a display controller." );
	    return false;
	}
    }
    if ( plan.resize && !ex.setScreenSize( plan.width, plan.height, plan.mmWidth, plan.mmHeight ) ) {
	if ( error )
	    *error = i18n( "Could not set the screen size to %1x%2." ).arg( plan.width ).arg( plan.height );
	return false;
    }
    for ( QValueList<CrtcOp>::ConstIterator it = plan.enable.begin(); it != plan.enable.end(); ++it ) {
	if ( !ex.setCrtc( *it ) ) {
	    if ( error )
		*error = i18n( "Could not configure a display controller." );
	    return false;
	}
    }
    if ( plan.setPrimary && !ex.setPrimary( plan.primary ) ) {
	if ( error )
	    *error = i18n( "Could not set the primary monitor." );
	return false;
    }
    return true;
}

// --- X11 ---------------------------------------------------------------------

static int s_errors = 0;

static int trapErrors( Display *, XErrorEvent * )
{
    ++s_errors;
    return 0;
}

namespace {
class X11Executor : public PlanExecutor
{
public:
    X11Executor( Display *dpy, XRRScreenResources *res )
	: m_dpy( dpy ), m_res( res ) {}

    bool setCrtc( const CrtcOp &op ) {
	RROutput outs[16];
	int n = 0;
	for ( QValueList<XID>::ConstIterator it = op.outputs.begin();
	      it != op.outputs.end() && n < 16; ++it )
	    outs[n++] = *it;
	int before = s_errors;
	Status st = XRRSetCrtcConfig( m_dpy, m_res, op.crtc, CurrentTime,
				      op.x, op.y, op.mode,
				      op.mode ? op.rotation : RR_Rotate_0,
				      n ? outs : 0, n );
	XSync( m_dpy, False );
	return st == RRSetConfigSuccess && s_errors == before;
    }
    bool setScreenSize( int w, int h, int mmw, int mmh ) {
	int before = s_errors;
	XRRSetScreenSize( m_dpy, DefaultRootWindow( m_dpy ), w, h, mmw, mmh );
	XSync( m_dpy, False );
	return s_errors == before;
    }
    bool setPrimary( XID output ) {
	int before = s_errors;
	XRRSetOutputPrimary( m_dpy, DefaultRootWindow( m_dpy ), output );
	XSync( m_dpy, False );
	return s_errors == before;
    }

private:
    Display *m_dpy;
    XRRScreenResources *m_res;
};
}

bool applyLayout( Display *dpy, const Layout &layout, QString *error )
{
    if ( !hasRandR12( dpy ) ) {
	if ( error )
	    *error = i18n( "The X server does not support RandR 1.2." );
	return false;
    }

    XSync( dpy, False );
    int (*oldHandler)( Display *, XErrorEvent * ) = XSetErrorHandler( trapErrors );
    XGrabServer( dpy );

    bool ok = false;
    QString err;
    XRRScreenResources *res = XRRGetScreenResourcesCurrent( dpy, DefaultRootWindow( dpy ) );
    ScreenState before;
    if ( !res || !queryState( dpy, before, res ) ) {
	err = i18n( "Could not read the display configuration." );
    } else {
	Plan plan = makePlan( before, layout );
	X11Executor ex( dpy, res );
	ok = executePlan( ex, plan, &err );
	if ( !ok && plan.error.isEmpty() ) {
	    // put the previous configuration back
	    XRRFreeScreenResources( res );
	    res = XRRGetScreenResourcesCurrent( dpy, DefaultRootWindow( dpy ) );
	    ScreenState now;
	    if ( res && queryState( dpy, now, res ) ) {
		X11Executor undo( dpy, res );
		executePlan( undo, makePlan( now, currentLayout( before ) ) );
	    }
	}
    }
    if ( res )
	XRRFreeScreenResources( res );

    XUngrabServer( dpy );
    XSync( dpy, False );
    XSetErrorHandler( oldHandler );

    if ( !ok && error )
	*error = err;
    return ok;
}

} // namespace KRandR
