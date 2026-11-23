/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2026 The KDE3 revival project

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

#ifndef KWIN_GRABPROOF_H
#define KWIN_GRABPROOF_H

#include <qcstring.h>
#include <qguardedptr.h>
#include <qmap.h>
#include <qobject.h>
#include <qstringlist.h>
#include <qvaluelist.h>

#include <kshortcut.h>

#include <X11/Xlib.h>
#include <fixx11h.h>

class KGlobalAccel;

namespace KWinInternal
{

/*
 Global shortcuts that work while a popup menu is open ("grab-proof"),
 like GNOME's screenshot keys.  An open menu (any application's: a context
 menu, the K menu, a menu bar's) holds an active keyboard grab, so the
 passive key grabs of KGlobalAccel don't fire and the key goes to the
 menu, which ignores it.  The keys of a few actions (kwinrc, [Shortcuts]
 UnderGrab: action names, by default the three screenshot actions) are
 also watched as XInput 2 raw key events (see Overview::handleXI2(); raw
 events come regardless of grabs), and such a press triggers the action
 when all of these hold:
  - the key and the modifiers are one of the action's shortcuts, as
    KGlobalAccel grabs it (Alt+Print on the old keycodes too), not an
    autorepeat, from any keyboard (the master device's events); the
    modifiers are those held at the press, from the raw events of the
    modifier keys before it, in order;
  - KWin did not get the key press itself (it is not in KWin's event
    queue after a round trip), so the normal shortcut did not fire: no
    double trigger, also not while KWin has the keyboard itself;
  - another client actively grabs both the keyboard and the pointer
    (AlreadyGrabbed for both), as menus do, tried with grabs of a
    connection of our own that are released at once when they succeed;
  - the keyboard grab is the menu's: an override-redirect window smaller
    than the screen got the grab's FocusIn (NotifyGrab; KWin selects
    FocusChange on override-redirect windows when they are created).  For
    one created and mapped too quickly for that, the topmost viewable
    override-redirect window, when it is the one mapped last, stands in;
  - the screen locker (kdesktop_lock) is not shown.
 What is left: a program that holds both grabs on an override-redirect
 window smaller than the screen looks like a menu (and with the stand-in,
 one whose override-redirect window was mapped last while it grabs from
 elsewhere).  Virtual machine and remote desktop viewers grab on their own
 managed window or the root window, so they don't.
 Nothing else is looked at or kept: of the other keys only whether they are
 down (the modifiers, the autorepeat), no logging of keys.

 Holding a shortcut key also fired the shortcut again for every autorepeat
 (KGlobalAccel's passive grab activates again): repeatedPress() tells
 whether KWin's current key press is an autorepeat, for the shortcuts that
 want to fire once.
*/
class GrabProofShortcuts
    {
    public:
        GrabProofShortcuts();
        ~GrabProofShortcuts();
        // every action of the KWin shortcuts with its slot (kwinbindings.cpp)
        void addAction( const QString& name, QObject* receiver, const char* slot );
        // the actions named, with their current shortcuts
        void reconfigure( KGlobalAccel* keys, const QStringList& names );
        bool isEmpty() const;
        // whether the raw key events come (Overview::selectXI2())
        void setRawKeysSelected( bool selected );
        // a raw key event (the server sends none for autorepeats)
        void rawKeyEvent( int keycode, Time time, bool press, bool repeat );
        // a raw key press, not an autorepeat, while the shortcuts are enabled
        void keyPressed( int keycode, Time time );
        // every X event of KWin's, before anybody else gets it
        void x11Event( const XEvent* e );
        // a shortcut fires: whether KWin's key press is an autorepeat
        bool repeatedPress() const { return repeated; }
    private:
        uint heldModifiers();
        bool deliveredToUs( int keycode, Time time );
        bool menuHasGrabs();
        bool menuOpen();
        void watch( Window w );
        void watchExisting();
        void setDown( unsigned int keycode, bool down );
        bool isDown( unsigned int keycode ) const;
        struct Slot
            {
            QGuardedPtr< QObject > receiver;
            QCString member;
            };
        struct Key
            {
            KKey key;
            QString action;
            };
        QMap< QString, Slot > actions;
        QValueList< Key > grab_keys;
        Display* probe; // a connection of our own for testing the grabs
        XModifierKeymap* modmap;
        // the keys down, from the raw events (and KWin's own key events)
        unsigned char down[ 32 ];
        bool raw_keys;
        unsigned int raw_press_code; // the last raw press
        Time raw_press_time;
        unsigned int release_code; // KWin's last key release
        Time release_time;
        bool repeated;
        // the override-redirect windows: whether watched before being mapped
        QMap< Window, bool > watched;
        bool watching;
        Window grab_window; // the one with the keyboard grab (FocusIn NotifyGrab)
        Window last_mapped;
    };

} // namespace

#endif
