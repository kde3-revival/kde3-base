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
#include <fixx11h.h>

#include <string.h>

namespace KWinInternal
{

GrabProofShortcuts::GrabProofShortcuts()
    : probe( NULL ),
      release_code( 0 ),
      release_time( CurrentTime ),
      repeated( false )
    {
    }

/*
 The autorepeat of a held key sends KGlobalAccel a release and a press with
 the same time again and again (no raw events), each firing the shortcut
 again: repeatedPress() tells the shortcuts that want to fire once.
*/
void GrabProofShortcuts::coreKeyEvent( const XKeyEvent& e )
    {
    if( e.type == KeyRelease )
        {
        release_code = e.keycode;
        release_time = e.time;
        repeated = false;
        return;
        }
    repeated = e.keycode == release_code && e.time == release_time;
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
    }

bool GrabProofShortcuts::isEmpty() const
    {
    return grab_keys.isEmpty();
    }

void GrabProofShortcuts::keyPressed( int keycode, Time time )
    {
    // the keycodes of the shortcuts now (the keyboard map may have changed,
    // as for KGlobalAccel's grabs)
    QString action;
    bool ours = false;
    uint mod = 0;
    for( QValueList< Key >::ConstIterator it = grab_keys.begin();
         it != grab_keys.end();
         ++it )
        {
        KKeyNative native( (*it).key );
        if( native.code() != (uint) keycode )
            continue;
        if( !ours )
            { // the modifiers held: those of the press, a moment later
            XkbStateRec state;
            if( XkbGetState( qt_xdisplay(), XkbUseCoreKbd, &state ) != Success )
                return;
            mod = state.mods & KKeyNative::accelModMaskX();
            ours = true;
            }
        if(( native.mod() & KKeyNative::accelModMaskX()) == mod )
            {
            action = (*it).action;
            break;
            }
        }
    if( action.isEmpty())
        return;
    if( deliveredToUs( keycode, time ))
        return; // KGlobalAccel has it (or KWin's own grab)
    if( !menuShown() || !menuHasGrabs())
        return;
    QMap< QString, Slot >::ConstIterator it = actions.find( action );
    repeated = false; // a press that KWin doesn't get, a new one
    if( it != actions.end() && !it.data().receiver.isNull())
        QTimer::singleShot( 0, it.data().receiver, it.data().member );
    }

namespace
{
struct KeyPressMatch
    {
    unsigned int keycode;
    Time time;
    bool found;
    };
}

static Bool findKeyPress( Display*, XEvent* e, XPointer arg )
    {
    KeyPressMatch* match = reinterpret_cast< KeyPressMatch* >( arg );
    if( e->type == KeyPress && e->xkey.keycode == match->keycode && e->xkey.time == match->time )
        match->found = true;
    return False; // nothing is taken from the queue
    }

/*
 The server makes the raw event and the core one of a press together, the
 raw one first: after a round trip the core press is in our queue, if we
 get it (KGlobalAccel's passive grab, or a grab of KWin's own).
*/
bool GrabProofShortcuts::deliveredToUs( int keycode, Time time )
    {
    Display* dpy = qt_xdisplay();
    XSync( dpy, False );
    KeyPressMatch match;
    match.keycode = keycode;
    match.time = time;
    match.found = false;
    XEvent dummy;
    XCheckIfEvent( dpy, &dummy, findKeyPress, reinterpret_cast< XPointer >( &match ));
    return match.found;
    }

/*
 Menus grab the keyboard and the pointer.  Our own connection tries both:
 one that succeeds is released at once (and the answer is no).
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
    return ( keyboard == AlreadyGrabbed || keyboard == GrabFrozen )
        && ( pointer == AlreadyGrabbed || pointer == GrabFrozen );
    }

/*
 A menu is an override-redirect window smaller than the screen; the
 screen locker is one too (full screen): while it is shown, nothing.
*/
bool GrabProofShortcuts::menuShown()
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
    bool menu = false;
    bool locker = false;
    for( unsigned int i = 0;
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
            menu = true;
        }
    if( children != NULL )
        XFree( children );
    return menu && !locker;
    }

} // namespace
