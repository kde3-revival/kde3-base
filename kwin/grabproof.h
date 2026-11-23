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
 also watched as XInput 2 raw key presses (see Overview::handleXI2(); raw
 events come regardless of grabs), and such a press triggers the action
 when all of these hold:
  - the key and the held modifiers are one of the action's shortcuts (not
    an autorepeat; the keyboards are the master device's, i.e. all);
  - KWin did not get the key press itself (it is not in KWin's event
    queue after a round trip), so the normal shortcut did not fire: no
    double trigger, also not while KWin has the keyboard itself;
  - another client actively grabs both the keyboard and the pointer, as
    menus do (a key grab of another program, a virtual machine or remote
    desktop that has only the keyboard: no), tested with grabs of a
    connection of our own that are released at once when they succeed;
  - an override-redirect window smaller than the screen (a menu) is shown
    and the screen locker (kdesktop_lock) is not.
 Nothing else is looked at or kept: no key that is not one of these
 shortcuts, no logging of keys.
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
        // a raw key press, not an autorepeat
        void keyPressed( int keycode, Time time );
        // a key event of KWin's, before KGlobalAccel gets it
        void coreKeyEvent( const XKeyEvent& e );
        // a shortcut fires: whether its key press is an autorepeat
        bool repeatedPress() const { return repeated; }
    private:
        bool deliveredToUs( int keycode, Time time );
        bool menuHasGrabs();
        bool menuShown();
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
        unsigned int release_code;
        Time release_time;
        bool repeated;
    };

} // namespace

#endif
