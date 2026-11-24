/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2026 The KDE3 revival project

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

/*
 wheelgrabtest: the active window must get the mouse wheel without KWin's
 passive grab in between (a regression test, run against a running KWin).

 KWin keeps a passive grab of the buttons on the wrapper of the active window
 when that window is not the most recently raised one ("obscured", for click
 raise).  After a desktop switch from the panel's pager the panel is the most
 recently raised window, so the active window got that grab, and with it the
 wheel buttons: every (emulated) wheel press then activated the grab, the
 window got an XI_Leave (Grab) + XI_Enter (Ungrab) for it, and Chrome, which
 resets its XI 2.1 scroll valuators on each Enter, lost the scrolling
 (touchpad scrolling dead, most wheel steps lost) until re-activated.

 The test does what the pager does: a dock window is clicked (KWin raises
 it), the desktop is switched away and back with _NET_CURRENT_DESKTOP, so the
 test window is activated again without being raised.  Then:
  - wheel: a passive grab of button 4 without modifiers on the window's
    parent (the wrapper) must be free (we try to grab it ourselves), and an
    XTest wheel step must reach the window without a Leave/Enter;
  - click raise kept: button 1 without modifiers stays grabbed by KWin.
 Every check prints PASS or FAIL; the exit status is the number of failures.

 Usage: wheelgrabtest   (DISPLAY must be a test server, :50 or above;
 at least 2 desktops, focus policy click to focus, click raise on)
*/

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XTest.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Display* dpy;
static Window root;
static int xi_opcode;
static int failures = 0;
static bool bad_access;

static int errorHandler( Display*, XErrorEvent* e )
    {
    if( e->error_code == BadAccess )
        bad_access = true;
    return 0;
    }

static void check( bool ok, const char* what )
    {
    printf( "%s: %s\n", ok ? "PASS" : "FAIL", what );
    if( !ok )
        ++failures;
    }

static Atom atom( const char* name )
    {
    return XInternAtom( dpy, name, False );
    }

static long cardinalProperty( Window w, const char* name, long fallback )
    {
    Atom type;
    int format;
    unsigned long n, after;
    unsigned char* data = NULL;
    long ret = fallback;
    if( XGetWindowProperty( dpy, w, atom( name ), 0, 1, False, AnyPropertyType, &type, &format, &n, &after,
            &data ) == Success && data != NULL )
        {
        if( n == 1 && format == 32 )
            ret = *(long*) data;
        XFree( data );
        }
    return ret;
    }

static Window activeWindow()
    {
    return (Window) cardinalProperty( root, "_NET_ACTIVE_WINDOW", 0 );
    }

// handles the events queued so far; counts the crossings and wheel presses of w
static int crossings = 0;
static int wheel_presses = 0;

static void pump( Window w )
    {
    XSync( dpy, False );
    while( XPending( dpy ))
        {
        XEvent e;
        XNextEvent( dpy, &e );
        if( e.type != GenericEvent || e.xcookie.extension != xi_opcode || !XGetEventData( dpy, &e.xcookie ))
            continue;
        if( e.xcookie.evtype == XI_Enter || e.xcookie.evtype == XI_Leave )
            {
            XIEnterEvent* c = (XIEnterEvent*) e.xcookie.data;
            if( c->event == w )
                ++crossings;
            }
        else if( e.xcookie.evtype == XI_ButtonPress )
            {
            XIDeviceEvent* b = (XIDeviceEvent*) e.xcookie.data;
            if( b->event == w && b->detail == 4 )
                ++wheel_presses;
            }
        XFreeEventData( dpy, &e.xcookie );
        }
    }

static void wait( Window w, int ms )
    {
    for( int i = 0;
         i < ms / 20;
         ++i )
        {
        pump( w );
        usleep( 20000 );
        }
    pump( w );
    }

// waits until KWin has reparented w (it is managed)
static bool waitManaged( Window w )
    {
    for( int i = 0;
         i < 250;
         ++i )
        {
        Window r, parent, *children = NULL;
        unsigned int n;
        if( XQueryTree( dpy, w, &r, &parent, &children, &n ) && children != NULL )
            XFree( children );
        if( parent != root && parent != None )
            return true;
        usleep( 20000 );
        }
    return false;
    }

static Window parentOf( Window w )
    {
    Window r, parent = None, *children = NULL;
    unsigned int n;
    if( XQueryTree( dpy, w, &r, &parent, &children, &n ) && children != NULL )
        XFree( children );
    return parent;
    }

// is button (no modifiers) passively grabbed by another client on w?
static bool grabbedByOther( Window w, unsigned int button )
    {
    bad_access = false;
    XGrabButton( dpy, button, 0, w, False, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None );
    XSync( dpy, False );
    if( !bad_access )
        XUngrabButton( dpy, button, 0, w );
    XSync( dpy, False );
    return bad_access;
    }

static void clientMessage( Window w, const char* type, long d0, long d1, long d2 )
    {
    XEvent ev;
    memset( &ev, 0, sizeof( ev ));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = atom( type );
    ev.xclient.format = 32;
    ev.xclient.data.l[ 0 ] = d0;
    ev.xclient.data.l[ 1 ] = d1;
    ev.xclient.data.l[ 2 ] = d2;
    XSendEvent( dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &ev );
    XFlush( dpy );
    }

static void click( int x, int y, unsigned int button, Window w )
    {
    XTestFakeMotionEvent( dpy, -1, x, y, CurrentTime );
    XSync( dpy, False );
    wait( w, 150 );
    XTestFakeButtonEvent( dpy, button, True, CurrentTime );
    XTestFakeButtonEvent( dpy, button, False, CurrentTime );
    XSync( dpy, False );
    wait( w, 300 );
    }

static Window createWindow( int x, int y, int w, int h, const char* name, bool dock )
    {
    Window win = XCreateSimpleWindow( dpy, root, x, y, w, h, 0, 0, dock ? 0x808080 : 0xc0d0e0 );
    XStoreName( dpy, win, name );
    XSizeHints sh;
    sh.flags = USPosition | USSize;
    sh.x = x;
    sh.y = y;
    sh.width = w;
    sh.height = h;
    XSetWMNormalHints( dpy, win, &sh );
    XWMHints wm;
    wm.flags = InputHint;
    wm.input = dock ? False : True; // a panel doesn't take the focus
    XSetWMHints( dpy, win, &wm );
    if( dock )
        {
        Atom type = atom( "_NET_WM_WINDOW_TYPE_DOCK" );
        XChangeProperty( dpy, win, atom( "_NET_WM_WINDOW_TYPE" ), XA_ATOM, 32, PropModeReplace,
            (unsigned char*) &type, 1 );
        long all = 0xFFFFFFFF;
        XChangeProperty( dpy, win, atom( "_NET_WM_DESKTOP" ), XA_CARDINAL, 32, PropModeReplace,
            (unsigned char*) &all, 1 );
        }
    XMapWindow( dpy, win );
    XSync( dpy, False );
    return win;
    }

int main()
    {
    const char* d = getenv( "DISPLAY" );
    // XTest input: only on a test server, never a user's session
    if( d == NULL || d[ 0 ] != ':' || atoi( d + 1 ) < 50 )
        {
        fprintf( stderr, "wheelgrabtest: DISPLAY must be a test X server (:50 or above)\n" );
        return 100;
        }
    dpy = XOpenDisplay( NULL );
    int event, error, major = 2, minor = 2;
    if( dpy == NULL || !XQueryExtension( dpy, "XInputExtension", &xi_opcode, &event, &error )
        || XIQueryVersion( dpy, &major, &minor ) != Success
        || !XTestQueryExtension( dpy, &event, &error, &major, &minor ))
        {
        fprintf( stderr, "wheelgrabtest: no X server with XInput 2 and XTest\n" );
        return 100;
        }
    XSetErrorHandler( errorHandler );
    root = DefaultRootWindow( dpy );
    const int sw = DisplayWidth( dpy, DefaultScreen( dpy ));
    const int sh = DisplayHeight( dpy, DefaultScreen( dpy ));
    if( cardinalProperty( root, "_NET_NUMBER_OF_DESKTOPS", 1 ) < 2 )
        {
        fprintf( stderr, "wheelgrabtest: needs 2 desktops\n" );
        return 100;
        }

    Window dock = createWindow( 0, sh - 40, sw / 2, 40, "wheelgrabtest dock", true );
    Window win = createWindow( 100, 100, 400, 300, "wheelgrabtest", false );
    unsigned char bits[ XIMaskLen( XI_LASTEVENT ) ] = { 0 };
    XISetMask( bits, XI_Enter );
    XISetMask( bits, XI_Leave );
    XISetMask( bits, XI_ButtonPress );
    XIEventMask mask;
    mask.deviceid = XIAllMasterDevices;
    mask.mask_len = sizeof( bits );
    mask.mask = bits;
    XISelectEvents( dpy, win, &mask, 1 );
    if( !waitManaged( win ) || !waitManaged( dock ))
        {
        fprintf( stderr, "wheelgrabtest: the windows are not managed (no KWin?)\n" );
        return 100;
        }
    wait( win, 500 );
    const long home = cardinalProperty( win, "_NET_WM_DESKTOP", 0 );
    const long other = home == 0 ? 1 : 0;

    // our window active and on top, the dock clicked (raised by KWin, takes no focus)
    click( 300, 250, Button1, win );
    check( activeWindow() == win, "the test window is active after a click" );
    click( 20, sh - 20, Button1, win );
    check( activeWindow() == win, "a click on the dock leaves the test window active" );

    // the desktop switched away and back like a pager does it
    clientMessage( root, "_NET_CURRENT_DESKTOP", other, CurrentTime, 0 );
    wait( win, 500 );
    clientMessage( root, "_NET_CURRENT_DESKTOP", home, CurrentTime, 0 );
    wait( win, 800 );
    check( activeWindow() == win, "the test window is active again after the desktop switch" );

    Window wrapper = parentOf( win );
    check( !grabbedByOther( wrapper, Button4 ), "no passive grab of the wheel on the active window" );
    check( grabbedByOther( wrapper, Button1 ), "click raise: button 1 still grabbed (the dock was raised last)" );

    // a wheel step over the window: delivered without crossing events
    XTestFakeMotionEvent( dpy, -1, 300, 250, CurrentTime );
    wait( win, 300 );
    crossings = 0;
    wheel_presses = 0;
    for( int i = 0;
         i < 3;
         ++i )
        {
        XTestFakeButtonEvent( dpy, Button4, True, CurrentTime );
        XTestFakeButtonEvent( dpy, Button4, False, CurrentTime );
        XSync( dpy, False );
        wait( win, 100 );
        }
    wait( win, 300 );
    printf( "wheel presses %d, crossings %d\n", wheel_presses, crossings );
    check( wheel_presses == 3, "every wheel step reaches the window" );
    check( crossings == 0, "no Leave/Enter (grab) for the wheel steps" );

    XDestroyWindow( dpy, win );
    XDestroyWindow( dpy, dock );
    XCloseDisplay( dpy );
    printf( "%s (%d failed)\n", failures == 0 ? "ALL PASS" : "FAILED", failures );
    return failures;
    }
