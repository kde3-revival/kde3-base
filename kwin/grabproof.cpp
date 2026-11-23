/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2026 The KDE3 revival project

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

#include "grabproof.h"

#include <qtimer.h>

#include <kglobalaccel.h>
#include <kkeynative.h>
#include <kshortcut.h>
#include <kxerrorhandler.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <fixx11h.h>

#include <string.h>

namespace KWinInternal
{

GrabProofShortcuts::GrabProofShortcuts()
    : probe( NULL ),
      modmap( NULL ),
      raw_keys( false ),
      raw_press_code( 0 ),
      raw_press_time( CurrentTime ),
      release_code( 0 ),
      release_time( CurrentTime ),
      repeated( false ),
      watching( false ),
      grab_window( None ),
      last_mapped( None )
    {
    memset( down, 0, sizeof( down ));
    }

GrabProofShortcuts::~GrabProofShortcuts()
    {
    if( modmap != NULL )
        XFreeModifiermap( modmap );
    if( probe != NULL )
        XCloseDisplay( probe );
    }

void GrabProofShortcuts::addAction( const QString& name, QObject* receiver, const char* slot )
    {
    Slot s;
    s.receiver = receiver;
    s.member = slot;
    actions[ name ] = s;
    }

void GrabProofShortcuts::reconfigure( KGlobalAccel* keys, const QStringList& names )
    {
    grab_keys.clear();
    for( QStringList::ConstIterator it = names.begin();
         it != names.end();
         ++it )
        {
        QMap< QString, Slot >::ConstIterator action = actions.find( *it );
        // a slot with arguments (the desktop number) can't be called alone
        if( action == actions.end() || action.data().member.contains( "()" ) == 0 )
            continue;
        const KShortcut& cut = keys->shortcut( *it );
        for( uint i = 0;
             i < cut.count();
             ++i )
            {
            if( cut.seq( i ).count() != 1 ) // a key sequence: not watched
                continue;
            Key k;
            k.key = cut.seq( i ).key( 0 );
            k.action = *it;
            grab_keys.append( k );
            }
        }
    if( !grab_keys.isEmpty() && !watching )
        watchExisting();
    }

bool GrabProofShortcuts::isEmpty() const
    {
    return grab_keys.isEmpty();
    }

void GrabProofShortcuts::setDown( unsigned int keycode, bool d )
    {
    if( d )
        down[ ( keycode / 8 ) & 31 ] |= 1 << ( keycode % 8 );
    else
        down[ ( keycode / 8 ) & 31 ] &= ~( 1 << ( keycode % 8 ));
    }

bool GrabProofShortcuts::isDown( unsigned int keycode ) const
    {
    return down[ ( keycode / 8 ) & 31 ] & ( 1 << ( keycode % 8 ));
    }

void GrabProofShortcuts::setRawKeysSelected( bool selected )
    {
    if( selected && !raw_keys ) // the keys down already
        XQueryKeymap( qt_xdisplay(), reinterpret_cast< char* >( down ));
    raw_keys = selected;
    }

void GrabProofShortcuts::rawKeyEvent( int keycode, Time time, bool press, bool repeat )
    {
    setDown( keycode, press );
    if( press && !repeat )
        {
        raw_press_code = keycode;
        raw_press_time = time;
        }
    }

// the modifiers of the keys down (in the order of the raw events)
uint GrabProofShortcuts::heldModifiers()
    {
    if( modmap == NULL )
        modmap = XGetModifierMapping( qt_xdisplay());
    if( modmap == NULL )
        return 0;
    uint mod = 0;
    for( int i = 0;
         i < 8;
         ++i )
        for( int j = 0;
             j < modmap->max_keypermod;
             ++j )
            {
            KeyCode code = modmap->modifiermap[ i * modmap->max_keypermod + j ];
            if( code != 0 && isDown( code ))
                mod |= 1 << i;
            }
    return mod & KKeyNative::accelModMaskX();
    }

void GrabProofShortcuts::keyPressed( int keycode, Time time )
    {
    // the keycodes of the shortcuts now (the keyboard map may have changed),
    // as KGlobalAccelPrivate::grabKey() grabs them
    QString action;
    bool ours = false;
    uint mod = 0;
    for( QValueList< Key >::ConstIterator it = grab_keys.begin();
         it != grab_keys.end();
         ++it )
        {
        KKeyNative native( (*it).key );
        uint code = native.code();
        uint key_mod = native.mod() & KKeyNative::accelModMaskX();
        // Alt+Print with the Xorg default keycodes
        if( native.sym() == XK_Sys_Req && XkbKeycodeToKeysym( qt_xdisplay(), 111, 0, 0 ) == XK_Print )
            {
            code = 111;
            key_mod |= KKeyNative::modX( KKey::ALT );
            }
        if( code != (uint) keycode )
            continue;
        if( !ours )
            {
            mod = heldModifiers();
            ours = true;
            }
        if( key_mod == mod )
            {
            action = (*it).action;
            break;
            }
        }
    if( action.isEmpty())
        return;
    if( deliveredToUs( keycode, time ))
        return; // KGlobalAccel has it (or KWin's own grab)
    if( !menuOpen() || !menuHasGrabs())
        return;
    QMap< QString, Slot >::ConstIterator it = actions.find( action );
    repeated = false; // a press that KWin doesn't get, a new one
    if( it != actions.end() && !it.data().receiver.isNull())
        QTimer::singleShot( 0, it.data().receiver, it.data().member );
    }

void GrabProofShortcuts::x11Event( const XEvent* e )
    {
    switch( e->type )
        {
        case KeyPress:
            {
            /*
             The autorepeat of a held key: with the server's usual one KGlobalAccel
             gets a release and a press with the same time again and again, but it
             may not get the releases (they go to the focus while its passive grab
             is not active); the server makes no raw events for repeats, so a press
             of a key down for an earlier raw press is one too.
            */
            const unsigned int code = e->xkey.keycode;
            repeated = ( code == release_code && e->xkey.time == release_time )
                || ( raw_keys && isDown( code )
                    && !( code == raw_press_code && e->xkey.time == raw_press_time ));
            setDown( code, true );
            break;
            }
        case KeyRelease:
            release_code = e->xkey.keycode;
            release_time = e->xkey.time;
            repeated = false;
            setDown( e->xkey.keycode, false );
            break;
        case MappingNotify:
            if( modmap != NULL )
                XFreeModifiermap( modmap );
            modmap = NULL;
            break;
        case CreateNotify:
            if( watching && e->xcreatewindow.parent == qt_xrootwin() && e->xcreatewindow.override_redirect )
                watch( e->xcreatewindow.window );
            break;
        case MapNotify:
            if( watching && e->xmap.event == qt_xrootwin() && e->xmap.override_redirect )
                {
                if( !watched.contains( e->xmap.window ))
                    watch( e->xmap.window );
                last_mapped = e->xmap.window;
                }
            break;
        case UnmapNotify:
            if( e->xunmap.window == grab_window )
                grab_window = None;
            break;
        case DestroyNotify:
            watched.remove( e->xdestroywindow.window );
            if( e->xdestroywindow.window == grab_window )
                grab_window = None;
            if( e->xdestroywindow.window == last_mapped )
                last_mapped = None;
            break;
        case FocusIn:
            if( e->xfocus.mode == NotifyGrab && watched.contains( e->xfocus.window ))
                grab_window = e->xfocus.window;
            break;
        case FocusOut:
            if( e->xfocus.mode == NotifyUngrab && e->xfocus.window == grab_window )
                grab_window = None;
            break;
        default:
            break;
        }
    }

/*
 FocusChange on an override-redirect window: its FocusIn with NotifyGrab
 tells that it got the keyboard grab (a menu).  Whether it was unmapped
 then: if not, its grab may have been missed.
*/
void GrabProofShortcuts::watch( Window w )
    {
    Display* dpy = qt_xdisplay();
    KXErrorHandler handler; // it may be gone
    XWindowAttributes attr;
    if( !XGetWindowAttributes( dpy, w, &attr ) || !attr.override_redirect || attr.c_class != InputOutput )
        return;
    XSelectInput( dpy, w, attr.your_event_mask | FocusChangeMask ); // (the compositor's mask stays)
    if( !XGetWindowAttributes( dpy, w, &attr ) || handler.error( true ))
        return;
    watched[ w ] = attr.map_state == IsUnmapped;
    }

void GrabProofShortcuts::watchExisting()
    {
    watching = true;
    Window dummy_root, dummy_parent;
    Window* children = NULL;
    unsigned int count = 0;
    if( !XQueryTree( qt_xdisplay(), qt_xrootwin(), &dummy_root, &dummy_parent, &children, &count ))
        return;
    for( unsigned int i = 0;
         i < count;
         ++i )
        watch( children[ i ] );
    if( children != NULL )
        XFree( children );
    }

namespace
{
struct QueueScan
    {
    unsigned int keycode;
    Time time;
    bool found;
    GrabProofShortcuts* self;
    };
}

static Bool scanQueue( Display*, XEvent* e, XPointer arg )
    {
    QueueScan* scan = reinterpret_cast< QueueScan* >( arg );
    if( e->type == KeyPress && e->xkey.keycode == scan->keycode && e->xkey.time == scan->time )
        scan->found = true;
    // a grab that has just begun or ended (nothing that makes requests)
    if( e->type == FocusIn || e->type == FocusOut )
        scan->self->x11Event( e );
    return False; // nothing is taken from the queue
    }

/*
 The server makes the raw event and the core one of a press together, the
 raw one first: after a round trip the core press is in our queue, if we
 get it (KGlobalAccel's passive grab, or a grab of KWin's own).  The focus
 events of the grabs before it are taken into account now too.
*/
bool GrabProofShortcuts::deliveredToUs( int keycode, Time time )
    {
    Display* dpy = qt_xdisplay();
    XSync( dpy, False );
    QueueScan scan;
    scan.keycode = keycode;
    scan.time = time;
    scan.found = false;
    scan.self = this;
    XEvent dummy;
    XCheckIfEvent( dpy, &dummy, scanQueue, reinterpret_cast< XPointer >( &scan ));
    return scan.found;
    }

/*
 Menus grab the keyboard and the pointer.  Our own connection tries both:
 one that succeeds is released at once (and the answer is no).  Only
 AlreadyGrabbed proves another client's grab (GrabFrozen: a device frozen
 by another's synchronous grab of the other one).
*/
bool GrabProofShortcuts::menuHasGrabs()
    {
    if( probe == NULL )
        probe = XOpenDisplay( DisplayString( qt_xdisplay()));
    if( probe == NULL )
        return false;
    Window root = DefaultRootWindow( probe );
    int keyboard = XGrabKeyboard( probe, root, False, GrabModeAsync, GrabModeAsync, CurrentTime );
    if( keyboard == GrabSuccess )
        {
        XUngrabKeyboard( probe, CurrentTime );
        XFlush( probe );
        return false;
        }
    int pointer = XGrabPointer( probe, root, False, 0, GrabModeAsync, GrabModeAsync, None, None, CurrentTime );
    if( pointer == GrabSuccess )
        {
        XUngrabPointer( probe, CurrentTime );
        XFlush( probe );
        return false;
        }
    return keyboard == AlreadyGrabbed && pointer == AlreadyGrabbed;
    }

/*
 The keyboard grab is a menu's (see grabproof.h), and the screen locker
 (full screen, override-redirect too) is not shown.
*/
bool GrabProofShortcuts::menuOpen()
    {
    Display* dpy = qt_xdisplay();
    KXErrorHandler handler; // windows may go meanwhile
    Window root = qt_xrootwin();
    XWindowAttributes root_attr;
    if( !XGetWindowAttributes( dpy, root, &root_attr ))
        return false;
    Window dummy_root, dummy_parent;
    Window* children = NULL;
    unsigned int count = 0;
    if( !XQueryTree( dpy, root, &dummy_root, &dummy_parent, &children, &count ))
        return false;
    bool grab_window_shown = false;
    Window topmost = None;
    bool locker = false;
    for( unsigned int i = 0; // bottom to top
         i < count && !locker;
         ++i )
        {
        XWindowAttributes attr;
        if( !XGetWindowAttributes( dpy, children[ i ], &attr ) || attr.map_state != IsViewable
            || !attr.override_redirect || attr.c_class != InputOutput )
            continue;
        XClassHint hint;
        if( XGetClassHint( dpy, children[ i ], &hint ))
            {
            locker = hint.res_name != NULL && strcmp( hint.res_name, "kdesktop_lock" ) == 0;
            if( hint.res_name != NULL )
                XFree( hint.res_name );
            if( hint.res_class != NULL )
                XFree( hint.res_class );
            }
        if( attr.x > 0 || attr.y > 0 || attr.x + attr.width < root_attr.width
            || attr.y + attr.height < root_attr.height )
            { // smaller than the screen
            topmost = children[ i ];
            if( children[ i ] == grab_window )
                grab_window_shown = true;
            }
        }
    if( children != NULL )
        XFree( children );
    if( locker )
        return false;
    if( grab_window_shown )
        return true;
    // mapped too quickly to know its grab: the topmost one, mapped last
    return topmost != None && topmost == last_mapped && watched.contains( topmost )
        && !watched[ topmost ];
    }

} // namespace
