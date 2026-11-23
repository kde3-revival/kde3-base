/*
 * touchpadtool: tests of the touchpad settings.
 *
 *   touchpadtool list      the touchpads of $DISPLAY: what each has, its
 *                          defaults and the settings touchpadrc gives it
 *   touchpadtool apply     applies touchpadrc to them (as kded_touchpad)
 *   touchpadtool selftest  touchpadrc's reading and writing, without X
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "touchpadsettings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <qfile.h>

#include <kconfig.h>
#include <kinstance.h>
#include <ksimpleconfig.h>

#include <X11/Xlib.h>

using namespace KTouchpad;

static void print( const char *what, const Settings &s )
{
    printf( "  %s: tap=%d drag=%d draglock=%d map=%d scroll=%d natural=%d dwt=%d speed=%.2f "
	    "profile=%d click=%d middle=%d offwithmouse=%d\n", what, s.tap, s.tapDrag,
	    s.tapDragLock, s.tapButtonMap, s.scrollMethod, s.naturalScroll,
	    s.disableWhileTyping, s.accelSpeed, s.accelProfile, s.clickMethod,
	    s.middleEmulation, s.disableOnExternalMouse );
}

static int failures = 0;

static void check( bool ok, const char *what )
{
    printf( "%s: %s\n", ok ? "PASS" : "FAIL", what );
    if ( !ok )
	++failures;
}

static int selftest()
{
    char path[] = "/tmp/touchpadrc-XXXXXX";
    const int fd = mkstemp( path );
    if ( fd < 0 )
	return 2;
    close( fd );

    const Settings d;
    {
	KSimpleConfig config( path );
	unsigned named = 1;
	const Settings s = load( &config, d, &named );
	check( s == d && s.tap && named == 0, "no touchpadrc: the defaults, tap-to-click on, nothing named" );
    }

    Settings custom = d;
    custom.tap = false;
    custom.tapDragLock = true;
    custom.tapButtonMap = TapLeftMiddleRight;
    custom.scrollMethod = ScrollEdge;
    custom.naturalScroll = true;
    custom.disableWhileTyping = false;
    custom.accelSpeed = -0.35;
    custom.accelProfile = AccelFlat;
    custom.clickMethod = ClickFinger;
    custom.middleEmulation = true;
    custom.disableOnExternalMouse = true;
    const unsigned all = 0xfff;
    check( differences( custom, d ) == ( all & ~HasTapDrag ), "differences() names each changed setting" );
    {
	KSimpleConfig config( path );
	save( &config, custom, differences( custom, d ) );
    }
    {
	KSimpleConfig config( path );
	unsigned named = 0;
	check( load( &config, d, &named ) == custom && named == ( all & ~HasTapDrag ),
	       "every setting comes back, named" );
	config.setGroup( "Touchpad" );
	check( config.readEntry( "ScrollMethod" ) == "Edge"
	       && config.readEntry( "ClickMethod" ) == "Clickfinger"
	       && config.readEntry( "TapButtonMap" ) == "LeftMiddleRight"
	       && !config.readBoolEntry( "TapToClick", true ) && !config.hasKey( "TapAndDrag" ),
	       "readable values, the unnamed one left out" );
    }

    // other defaults (another device): what touchpadrc names stays
    Settings other = d;
    other.clickMethod = ClickButtonAreas;
    other.scrollMethod = ScrollNone;
    {
	KSimpleConfig config( path );
	check( load( &config, other ) == custom, "named settings override another device's defaults" );
    }

    // a choice equal to the first touchpad's default is still a choice:
    // a second touchpad whose default differs follows it too
    Settings first = d;
    first.clickMethod = ClickButtonAreas;
    Settings second = d;
    second.clickMethod = ClickFinger;
    {
	KSimpleConfig config( path );
	unsigned named = 0;
	Settings s = load( &config, first, &named );
	s.clickMethod = ClickButtonAreas;
	named |= differences( s, load( &config, first ) );
	save( &config, s, named );
    }
    {
	KSimpleConfig config( path );
	config.setGroup( "Touchpad" );
	check( config.readEntry( "ClickMethod" ) == "ButtonAreas"
	       && load( &config, second ).clickMethod == ClickButtonAreas,
	       "a choice equal to one device's default reaches another device" );
    }

    Settings none = d;
    none.scrollMethod = ScrollNone;
    none.clickMethod = ClickNone;
    {
	KSimpleConfig config( path );
	save( &config, none, HasScrollMethod | HasClickMethod );
    }
    {
	KSimpleConfig config( path );
	check( load( &config, d ) == none, "\"None\" methods come back" );
    }

    // back to the defaults: nothing is left in touchpadrc
    {
	KSimpleConfig config( path );
	save( &config, d, 0 );
    }
    {
	KSimpleConfig config( path );
	check( config.entryMap( "Touchpad" ).isEmpty(), "the defaults leave no keys" );
	Settings changed = d;
	changed.scrollMethod = ScrollEdge;
	check( load( &config, changed ) == changed, "unnamed settings follow the device's defaults" );
    }

    // a hand-written touchpadrc
    {
	KSimpleConfig config( path );
	config.setGroup( "Touchpad" );
	config.writeEntry( "PointerSpeed", 7.5 );
	config.writeEntry( "ScrollMethod", "twofinger" );
	config.writeEntry( "ClickMethod", "bogus" );
	config.sync();
    }
    {
	KSimpleConfig config( path );
	Settings base = d;
	base.scrollMethod = ScrollEdge;
	unsigned named = 0;
	const Settings s = load( &config, base, &named );
	check( s.accelSpeed == 1.0 && s.scrollMethod == ScrollTwoFinger && s.clickMethod == d.clickMethod
	       && named == ( HasAccelSpeed | HasScrollMethod ),
	       "out-of-range and unknown values (an unknown one is not named)" );
    }

    unlink( path );
    printf( "%d failure(s)\n", failures );
    return failures ? 1 : 0;
}

int main( int argc, char **argv )
{
    KInstance instance( "touchpadtool" );
    const char *cmd = argc > 1 ? argv[1] : "list";
    if ( !strcmp( cmd, "selftest" ) )
	return selftest();

    Display *dpy = XOpenDisplay( 0 );
    if ( !dpy ) {
	fprintf( stderr, "touchpadtool: no display\n" );
	return 2;
    }
    int rc = 0;
    if ( !strcmp( cmd, "list" ) ) {
	KConfig config( "touchpadrc", true, false );
	const DeviceList list = touchpads( dpy );
	for ( DeviceList::ConstIterator it = list.begin(); it != list.end(); ++it ) {
	    const Device &dev = *it;
	    printf( "%d \"%s\" libinput=%d features=0x%x scroll=0x%x click=0x%x profiles=0x%x\n",
		    dev.id, dev.name.utf8().data(), dev.libinput, dev.features,
		    dev.scrollMethods, dev.clickMethods, dev.accelProfiles );
	    const Settings d = defaults( dpy, dev );
	    print( "defaults", d );
	    print( "touchpadrc", load( &config, d ) );
	}
	printf( "%u touchpad(s)\n", unsigned( list.count() ) );
    } else if ( !strcmp( cmd, "apply" ) ) {
	printf( "applied to %d touchpad(s)\n", applyAll( dpy ) );
    } else {
	fprintf( stderr, "usage: touchpadtool [list|apply|selftest]\n" );
	rc = 2;
    }
    XCloseDisplay( dpy );
    return rc;
}
