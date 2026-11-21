/*
 * krandrstartup: applies the saved display configuration (displayrc) of
 * the connected monitors at login. Run by startkde before the splash
 * screen; kded_randr takes over for hotplugs.
 *
 *   krandrstartup            apply the saved profile, if there is one
 *   krandrstartup --default  apply the default layout if there is none
 *   krandrstartup --print    print the outputs and the plan, change nothing
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "randrcore.h"
#include "randrx11.h"

#include <qdir.h>
#include <qfile.h>
#include <qtextstream.h>

#include <kconfig.h>
#include <kglobal.h>
#include <kinstance.h>
#include <kstandarddirs.h>

#include <X11/Xlib.h>

#include <stdio.h>
#include <string.h>

using namespace KRandR;

// scripts in $KDEHOME/env that run xrandr themselves (like a
// monitors.sh from before this existed) undo what is applied here:
// startkde sources them later
static void checkEnvScripts()
{
    QDir env( KGlobal::dirs()->localkdedir() + "env", "*.sh" );
    QStringList files = env.entryList( QDir::Files );
    for ( QStringList::ConstIterator it = files.begin(); it != files.end(); ++it ) {
	QFile f( env.absFilePath( *it ) );
	if ( !f.open( IO_ReadOnly ) )
	    continue;
	QTextStream ts( &f );
	QString text = ts.read();
	if ( text.contains( "xrandr" ) )
	    fprintf( stderr, "krandrstartup: %s runs xrandr after the saved display "
		     "configuration was applied (Control Center, Peripherals, Display); "
		     "it can be removed\n", QFile::encodeName( f.name() ).data() );
    }
}

static void print( const ScreenState &state )
{
    printf( "screen %dx%d (%dx%d mm), min %dx%d, max %dx%d\n", state.width, state.height,
	    state.mmWidth, state.mmHeight, state.minWidth, state.minHeight,
	    state.maxWidth, state.maxHeight );
    for ( QValueList<Output>::ConstIterator it = state.outputs.begin(); it != state.outputs.end(); ++it ) {
	const Output &o = *it;
	if ( !o.connected )
	    continue;
	const Crtc *c = o.crtc ? state.crtc( o.crtc ) : 0;
	const Mode *m = c && c->mode ? state.mode( c->mode ) : 0;
	printf( "output %s: %s%s%s, key %s, model \"%s\", %dx%d mm, %d modes",
		o.name.latin1(), m ? "on" : "off", o.primary ? ", primary" : "",
		o.nonDesktop ? ", non-desktop" : "", o.key.latin1(),
		o.model.isEmpty() ? "" : o.model.latin1(),
		o.mmWidth, o.mmHeight, (int) o.modes.count() );
	if ( m )
	    printf( ", %dx%d@%.2f +%d+%d rotation %d", m->width, m->height, m->rate,
		    c->x, c->y, c->rotation & 0xf );
	printf( "\n" );
    }
    printf( "monitors: %s\n", state.connectedSetKey().isEmpty() ? "none"
	    : state.connectedSetKey().latin1() );
}

int main( int argc, char **argv )
{
    bool printOnly = false, useDefault = false;
    for ( int i = 1; i < argc; ++i ) {
	if ( !strcmp( argv[i], "--print" ) )
	    printOnly = true;
	else if ( !strcmp( argv[i], "--default" ) )
	    useDefault = true;
	else {
	    fprintf( stderr, "usage: krandrstartup [--print] [--default]\n" );
	    return 2;
	}
    }

    KInstance instance( "krandrstartup" );
    Display *dpy = XOpenDisplay( 0 );
    if ( !dpy ) {
	fprintf( stderr, "krandrstartup: cannot open the display\n" );
	return 1;
    }
    // an output that disappears while it is queried (an MST hub) must
    // not end the program
    ignoreXErrors();
    ScreenState state;
    if ( !queryState( dpy, state ) ) {
	XCloseDisplay( dpy );
	return 0;   // no RandR 1.2: nothing to do
    }

    KConfig config( "displayrc", true );
    ProfileStore store( &config );
    bool fromProfile = false;
    Layout layout = layoutFor( state, store, useDefault, &fromProfile );

    if ( printOnly ) {
	print( state );
	printf( "%s: %s\n", fromProfile ? "saved profile" : useDefault ? "default layout" : "no profile",
		layout.isEmpty() ? "-" : makePlan( state, layout ).describe( state ).latin1() );
	XCloseDisplay( dpy );
	return 0;
    }

    bool autoRestore = autoRestoreEnabled( &config );
    config.setGroup( "General" );
    int ret = 0;
    if ( layout.isEmpty() || !config.readBoolEntry( "ApplyOnLogin", true ) ||
	 ( !autoRestore && !useDefault ) ) {
	// nothing saved for these monitors (or restoring switched off):
	// keep what the X server set up
    } else {
	QString error;
	if ( !applyLayout( dpy, layout, &error ) ) {
	    fprintf( stderr, "krandrstartup: %s\n", error.local8Bit().data() );
	    ret = 1;
	}
    }
    if ( fromProfile )
	checkEnvScripts();
    XCloseDisplay( dpy );
    return ret;
}
