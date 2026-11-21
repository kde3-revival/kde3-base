/* This file is part of the KDE project
   Copyright (C) 2026 Serghei Amelian <serghei.amelian@gmail.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

/*
 * kstartupx: the X settings startkde makes before KDE itself runs and its
 * error messages, so that startkde does not need xsetroot, xprop and
 * xmessage.
 *
 *   --black-root             paint the root window black
 *                            (xsetroot -solid "#000000")
 *   --root-cursor            give the root window the left_ptr cursor
 *                            (xsetroot -cursor_name left_ptr)
 *   --set-full-session       set the KDE_FULL_SESSION root window property
 *                            (xprop -root -f KDE_FULL_SESSION 8t
 *                             -set KDE_FULL_SESSION true)
 *   --remove-full-session    remove it again
 *                            (xprop -root -remove KDE_FULL_SESSION)
 *   --message TEXT           show TEXT in a message box and wait until it is
 *                            closed (xmessage TEXT); this needs only the
 *                            X server and Qt, because it is used when KDE
 *                            could not be started
 *
 * Several options may be given; they are applied in that order.  The exit
 * status is 0 on success, 1 for bad arguments and 2 if the display cannot
 * be opened.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <qapplication.h>
#include <qmessagebox.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>

enum
{
    BlackRoot = 1 << 0,
    RootCursor = 1 << 1,
    SetFullSession = 1 << 2,
    RemoveFullSession = 1 << 3,
    Message = 1 << 4
};

static int usage()
{
    fprintf( stderr, "usage: kstartupx [--black-root] [--root-cursor]"
        " [--set-full-session] [--remove-full-session] [--message TEXT]\n" );
    return 1;
}

int main( int argc, char* argv[] )
{
    int actions = 0;
    const char* message = NULL;
    for( int i = 1; i < argc; ++i )
    {
        if( strcmp( argv[ i ], "--black-root" ) == 0 )
            actions |= BlackRoot;
        else if( strcmp( argv[ i ], "--root-cursor" ) == 0 )
            actions |= RootCursor;
        else if( strcmp( argv[ i ], "--set-full-session" ) == 0 )
            actions |= SetFullSession;
        else if( strcmp( argv[ i ], "--remove-full-session" ) == 0 )
            actions |= RemoveFullSession;
        else if( strcmp( argv[ i ], "--message" ) == 0 && i + 1 < argc )
        {
            actions |= Message;
            message = argv[ ++i ];
        }
        else
            return usage();
    }
    if( actions == 0 )
        return usage();

    Display* dpy = XOpenDisplay( NULL );
    if( dpy == NULL )
    {
        fprintf( stderr, "kstartupx: cannot open display\n" );
        return 2;
    }
    Window root = DefaultRootWindow( dpy );

    if( actions & BlackRoot )
    {
        XSetWindowBackground( dpy, root, BlackPixel( dpy, DefaultScreen( dpy )));
        XClearWindow( dpy, root );
    }
    if( actions & RootCursor )
    {
        // Xlib loads the theme's cursor through Xcursor when one is set,
        // just as for xsetroot.
        Cursor cursor = XCreateFontCursor( dpy, XC_left_ptr );
        XDefineCursor( dpy, root, cursor );
        XFreeCursor( dpy, cursor );
    }
    if( actions & ( SetFullSession | RemoveFullSession ))
    {
        // KApplication and other desktops' toolkits check the property for
        // an 8-bit STRING
        Atom atom = XInternAtom( dpy, "KDE_FULL_SESSION", False );
        if( actions & SetFullSession )
        {
            static const char value[] = "true";
            XChangeProperty( dpy, root, atom, XA_STRING, 8, PropModeReplace,
                reinterpret_cast< const unsigned char* >( value ), strlen( value ));
        }
        if( actions & RemoveFullSession )
            XDeleteProperty( dpy, root, atom );
    }

    if( actions & Message )
    {
        // not a client of whatever session startkde may have been started from
        unsetenv( "SESSION_MANAGER" );
        QApplication app( dpy );
        QMessageBox box( "KDE", QString::fromLocal8Bit( message ), QMessageBox::Warning,
            QMessageBox::Ok | QMessageBox::Default | QMessageBox::Escape,
            QMessageBox::NoButton, QMessageBox::NoButton );
        box.setTextFormat( Qt::PlainText );
        box.exec();
    }

    XCloseDisplay( dpy );
    return 0;
}
