/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2026 The KDE3 revival project

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

#include "grabproof.h"

#include <qapplication.h>
#include <qeventloop.h>
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
#include <time.h>

namespace KWinInternal
{

// ms from the map of a menu to its grab, when the map was too quick to watch it
static const double MenuGrabTime = 500.0;

// a monotonic clock (ms)
static double now()
    {
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
    }

GrabProofShortcuts::GrabProofShortcuts()
    : probe( NULL ),
      raw_dpy( NULL ),
      xkb_event( -1 ),
      xkb_grab_mods( 0 ),
      xkb_compat_grab_mods( 0 ),
      core_next( 0 ),
      raw_press_next( 0 ),
      release_code( 0 ),
      release_time( CurrentTime ),
      repeated( false ),
      watching( false ),
      grab_window( None ),
      late_window( None ),
      caught_up( false ),
      wake_time( 0 ),
      late_wake( 0 )
    {
    connect( qApp->eventLoop(), SIGNAL( aboutToBlock()), SLOT( aboutToBlock()));
    connect( qApp->eventLoop(), SIGNAL( awake()), SLOT( awake()));
    memset( core_code, 0, sizeof( core_code ));
    memset( raw_press_code, 0, sizeof( raw_press_code ));
    memset( raw_press_time, 0, sizeof( raw_press_time ));
    memset( core_time, 0, sizeof( core_time ));
    }

GrabProofShortcuts::~GrabProofShortcuts()
    {
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

// the keycode and the modifiers of a shortcut's key, as
// KGlobalAccelPrivate::grabKey() grabs it (now: the keyboard map may change)
static void nativeKey( const KKey& key, uint* code, uint* mod )
    {
    KKeyNative native( key );
    *code = native.code();
    *mod = native.mod() & KKeyNative::accelModMaskX();
    // Alt+Print with the Xorg default keycodes
    if( native.sym() == XK_Sys_Req && XkbKeycodeToKeysym( qt_xdisplay(), 111, 0, 0 ) == XK_Print )
        {
        *code = 111;
        *mod |= KKeyNative::modX( KKey::ALT );
        }
    }

void GrabProofShortcuts::reconfigure( KGlobalAccel* keys, const QStringList& names,
    const QStringList& once )
    {
    once_keys.clear();
    for( QStringList::ConstIterator it = once.begin();
         it != once.end();
         ++it )
        {
        const KShortcut& cut = keys->shortcut( *it );
        for( uint i = 0;
             i < cut.count();
             ++i )
            if( cut.seq( i ).count() == 1 )
                once_keys.append( cut.seq( i ).key( 0 ));
        }
    pending.clear();
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

GrabProofShortcuts::Keys::Keys()
    {
    memset( bits, 0, sizeof( bits ));
    }

/*
 A key is down while one keyboard holds it: a release of the same key on
 another master keyboard doesn't end it.  A release of a known keyboard
 ends it for the unknown one too (it may have been that one).
*/
void GrabProofShortcuts::setDown( int device, unsigned int keycode, bool d )
    {
    const int i = ( keycode / 8 ) & 31;
    const unsigned char bit = 1 << ( keycode % 8 );
    if( d )
        {
        down[ device ].bits[ i ] |= bit;
        return;
        }
    if( down.contains( device ))
        down[ device ].bits[ i ] &= ~bit;
    if( device != UnknownDevice && down.contains( UnknownDevice ))
        down[ UnknownDevice ].bits[ i ] &= ~bit;
    }

bool GrabProofShortcuts::isDown( unsigned int keycode ) const
    {
    const int i = ( keycode / 8 ) & 31;
    const unsigned char bit = 1 << ( keycode % 8 );
    for( QMap< int, Keys >::ConstIterator it = down.begin();
         it != down.end();
         ++it )
        if( (*it).bits[ i ] & bit )
            return true;
    return false;
    }

void GrabProofShortcuts::setRawConnection( Display* raw )
    {
    if( raw == raw_dpy )
        return;
    raw_dpy = raw;
    xkb_event = -1;
    if( raw_dpy == NULL )
        return;
    selectXkb();
    // the keys down already (after the selection: raw events queued before
    // the reply are older, presses and releases that end where the reply is)
    down.clear();
    XQueryKeymap( raw_dpy, reinterpret_cast< char* >( down[ UnknownDevice ].bits ));
    }

void GrabProofShortcuts::rawConnectionEvent( const XEvent* e )
    {
    if( xkb_event >= 0 && e->type == xkb_event
        && reinterpret_cast< const XkbAnyEvent* >( e )->xkb_type == XkbStateNotify )
        {
        const XkbStateNotifyEvent* state = reinterpret_cast< const XkbStateNotifyEvent* >( e );
        xkb_grab_mods = state->grab_mods;
        xkb_compat_grab_mods = state->compat_grab_mods;
        }
    }

void GrabProofShortcuts::rawKeyEvent( int device, int keycode, Time time, bool press, bool repeat )
    {
    setDown( device, keycode, press );
    if( press && !repeat )
        { // (several may be read before KWin gets the core press of the first)
        raw_press_code[ raw_press_next ] = keycode;
        raw_press_time[ raw_press_next ] = time;
        raw_press_next = ( raw_press_next + 1 ) % RawPresses;
        // the key of a shortcut that fires once: kept until its core press
        // comes, whatever other keys are pressed meanwhile (one per key and
        // keyboard, the next press of it replaces it)
        for( QValueList< KKey >::ConstIterator it = once_keys.begin();
             it != once_keys.end();
             ++it )
            {
            uint code, mod;
            nativeKey( *it, &code, &mod );
            if( code == (uint) keycode )
                {
                pending[ ( device << 8 ) | ( keycode & 255 ) ] = time;
                break;
                }
            }
        }
    }

/*
 The modifiers of a press are XKB's state then: its StateNotify events
 (absolute states) are taken in the order they come with the key events,
 on the same connection.  Selected first, read afterwards: the events
 queued before the reply carry older states and are taken after it, the
 last of them is the state that was read.
*/
void GrabProofShortcuts::selectXkb()
    {
    Display* dpy = raw_dpy;
    int opcode, error, major = XkbMajorVersion, minor = XkbMinorVersion;
    if( !XkbQueryExtension( dpy, &opcode, &xkb_event, &error, &major, &minor ))
        {
        xkb_event = -1;
        return;
        }
    const unsigned long details = XkbModifierStateMask | XkbModifierBaseMask | XkbModifierLatchMask
        | XkbModifierLockMask | XkbGrabModsMask | XkbCompatGrabModsMask;
    XkbSelectEventDetails( dpy, XkbUseCoreKbd, XkbStateNotify, details, details );
    XkbStateRec state;
    if( XkbGetState( dpy, XkbUseCoreKbd, &state ) == Success )
        {
        xkb_grab_mods = state.grab_mods;
        xkb_compat_grab_mods = state.compat_grab_mods;
        }
    }

/*
 The modifiers that KGlobalAccel's passive grabs (on KWin's connection)
 are matched with.  The XKB protocol lets a per-client flag decide:
 GrabsUseXKBState set, the XKB grab state (grab_mods), else the
 core-compatible one (compat_grab_mods, plus the modifiers that the
 compatibility map gives the keyboard group).  Qt sets the flag on its
 connection (QApplication's XKB initialization), so it is asked for, not
 assumed (a round trip, only for a press of a shortcut's key).  The X.Org
 server (dix, CheckPassiveGrabsOnWindow()) compares grab_mods whatever
 the flag; with Qt's flag set both agree.
*/
uint GrabProofShortcuts::grabModifiers() const
    {
    unsigned int flags = 0;
    if( XkbGetPerClientControls( qt_xdisplay(), &flags ) && ( flags & XkbPCF_GrabsUseXKBStateMask ))
        return xkb_grab_mods;
    return xkb_compat_grab_mods;
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
        uint code, key_mod;
        nativeKey( (*it).key, &code, &key_mod );
        if( code != (uint) keycode )
            continue;
        if( !ours )
            {
            if( xkb_event < 0 )
                return;
            mod = grabModifiers() & KKeyNative::accelModMaskX();
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
            const unsigned int code = e->xkey.keycode;
            core_code[ core_next ] = code;
            core_time[ core_next ] = e->xkey.time;
            core_next = ( core_next + 1 ) % CorePresses;
            emit readRaw(); // (the raw events up to this press, if read in time)
            /*
             The autorepeat of a held key: with the server's usual one KGlobalAccel
             gets a release and a press with the same time again and again, but it
             may not get the releases (they go to the focus while its passive grab
             is not active); the server makes no raw events for repeats, so a press
             of a key down for an earlier raw press is one too: a press that is
             none of the last raw presses (each keyboard's, read up to this
             press).
            */
            bool raw_press = false;
            for( int i = 0;
                 i < RawPresses;
                 ++i )
                if( raw_press_code[ i ] == code && raw_press_time[ i ] == e->xkey.time )
                    raw_press = true;
            for( QMap< int, Time >::Iterator it = pending.begin();
                 it != pending.end();
                 ++it )
                if( ( it.key() & 255 ) == (int) code && it.data() == e->xkey.time )
                    {
                    raw_press = true;
                    pending.remove( it ); // (matched: done)
                    break;
                    }
            repeated = ( code == release_code && e->xkey.time == release_time )
                || ( raw_dpy != NULL && isDown( code ) && !raw_press );
            setDown( UnknownDevice, code, true );
            break;
            }
        case KeyRelease:
            emit readRaw();
            release_code = e->xkey.keycode;
            release_time = e->xkey.time;
            repeated = false;
            setDown( UnknownDevice, e->xkey.keycode, false );
            break;
        case CreateNotify:
            // (also when created without override-redirect: Qt sets it on a
            // popup right after creating it, watch() looks at it now)
            if( watching && e->xcreatewindow.parent == qt_xrootwin())
                watch( e->xcreatewindow.window );
            break;
        case MapNotify:
            if( watching && e->xmap.event == qt_xrootwin() && e->xmap.override_redirect )
                {
                if( !watched.contains( e->xmap.window ))
                    watch( e->xmap.window );
                // mapped before we could watch it: its grab may come next
                late_window = watched.contains( e->xmap.window ) && !watched[ e->xmap.window ]
                    ? e->xmap.window : None;
                late_wake = wake_time; // (the map came after it)
                }
            break;
        case UnmapNotify:
            if( e->xunmap.window == grab_window )
                grab_window = None;
            if( e->xunmap.window == late_window )
                late_window = None;
            if( watched.contains( e->xunmap.window ))
                watched[ e->xunmap.window ] = true; // its next grab is seen
            break;
        case DestroyNotify:
            watched.remove( e->xdestroywindow.window );
            if( e->xdestroywindow.window == grab_window )
                grab_window = None;
            if( e->xdestroywindow.window == late_window )
                late_window = None;
            break;
        case FocusIn:
            if( e->xfocus.mode == NotifyGrab && watched.contains( e->xfocus.window ))
                {
                grab_window = e->xfocus.window;
                late_window = None;
                }
            break;
        case FocusOut:
            if( e->xfocus.window == grab_window && grab_window != None )
                grab_window = None; // released, or replaced by another grab
            else if( e->xfocus.mode == NotifyGrab && !watched.contains( e->xfocus.window )
                && late_window != None && late_wake > 0 && now() - late_wake < MenuGrabTime )
                { // a grab begins (the focus loses the keyboard) right after the map
                grab_window = late_window;
                late_window = None;
                }
            break;
        default:
            break;
        }
    }

/*
 Neither MapNotify nor FocusOut has a time stamp, and how long both waited
 in KWin's queue is not known (KWin may have been busy while the map and,
 much later, an unrelated grab happened, and then read both at once); a
 server time from a round trip only tells when KWin asked, after both.
 What KWin can know: when its event loop is about to wait and nothing is
 left to read on its connection (the queue empty and nothing pending,
 XPending()), every event the server has sent so far is handled; the wait
 ends (awake()) as soon as something comes, so whatever KWin reads
 afterwards came after the end of that wait.  The grab of a late menu (its
 FocusOut) must be handled within MenuGrabTime of the end of the last such
 wait before its map: map and grab then lie within that time, and KWin's
 own delays (busy, round trips) only make it stricter.
 Not seen: time that KWin did not run while it waited (stopped, not
 scheduled: the wait ends late, with older events), and time that an
 event waited in the server's output buffer.
*/
void GrabProofShortcuts::aboutToBlock()
    {
    caught_up = false;
    if( !watching )
        return;
    if( XPending( qt_xdisplay()) > 0 )
        { // (read now, after Qt looked: don't wait with them in the queue)
        qApp->eventLoop()->wakeUp();
        return;
        }
    caught_up = true;
    }

void GrabProofShortcuts::awake()
    {
    if( caught_up )
        wake_time = now();
    caught_up = false;
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
 The server makes the raw event and the core one of a press together:
 after a round trip on KWin's connection the core press is in its queue, if
 KWin gets it (KGlobalAccel's passive grab, or a grab of KWin's own), or
 KWin has had it already (the raw events come on another connection, read
 when its socket notifier fires or before KWin's own key events).  The
 focus events of the grabs before it are taken into account now too.
*/
bool GrabProofShortcuts::deliveredToUs( int keycode, Time time )
    {
    for( int i = 0;
         i < CorePresses;
         ++i )
        if( core_code[ i ] == (unsigned int) keycode && core_time[ i ] == time )
            return true;
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
            if( children[ i ] == grab_window )
                grab_window_shown = true;
            }
        }
    if( children != NULL )
        XFree( children );
    if( locker )
        return false;
    return grab_window_shown;
    }

} // namespace

#include "grabproof.moc"
