/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

#ifndef KWIN_SCREENDESKTOPS_H
#define KWIN_SCREENDESKTOPS_H

#include <qmap.h>
#include <qrect.h>
#include <qstringlist.h>
#include <qvaluevector.h>

namespace KWinInternal
{

/*
 The current virtual desktop of every monitor ("Desktops switch per
 monitor").  The numbering 1..N of the desktops is common to all monitors;
 each monitor shows one of them, and a window is visible when its desktop is
 the one of the monitor showing most of it (or it is on all desktops).
 Classic mode (independent() false): all monitors show the same desktop.

 Free of X11: the monitors are given as identities (EDID based, made by
 Workspace::screenIds()) in the window manager's numbering (QDesktopWidget:
 the primary one first).  A Wayland port maps one ScreenDesktops entry to an
 ext-workspace group per output.

 The active monitor is the one the user works on: the monitor of the active
 window, else the one switched last.  Its desktop is the "current desktop"
 of the old single-desktop interfaces (_NET_CURRENT_DESKTOP, DCOP).

 A monitor that goes away keeps its desktop here (by identity) and gets it
 back when it returns.
*/
class ScreenDesktops
    {
    public:
        ScreenDesktops();
        bool independent() const { return independent_; }
        // turning it off makes every monitor show the active one's desktop
        void setIndependent( bool on );
        void setDesktopCount( int count );
        int desktopCount() const { return count_; }
        /*
         The monitors now, in the window manager's numbering.  Monitors that
         were known keep their desktops (by identity, whatever their new
         number); ones that come back get theirs back; new ones show the
         active desktop.  Fills the identities gone and come.
        */
        void setOutputs( const QStringList& ids, QStringList* lost, QStringList* added );
        int outputCount() const { return ids_.count(); }
        QStringList ids() const { return ids_; }
        QString id( int output ) const;
        int output( const QString& id ) const; // -1 if not there
        // the desktop of a monitor (out of range: the active one's)
        int current( int output ) const;
        // returns true if a desktop shown changed; classic: all monitors
        bool setCurrent( int output, int desktop );
        // the identity of a monitor (present or away) changed: it keeps its desktop
        void rename( const QString& from, const QString& to );
        int active() const { return active_; }
        void setActive( int output );
        int activeDesktop() const { return current( active_ ); }
        // "id=desktop,..." of the monitors present and remembered (session)
        QString save() const;
        void restore( const QString& state );
    private:
        bool independent_;
        int count_;
        QStringList ids_;
        QValueVector< int > current_;
        int active_;
        QMap< QString, int > remembered_; // monitors gone
    };

/*
 Where a window lived before its monitor went away (variant A of the design:
 the window waits on another monitor with the same desktop number and goes
 back when its monitor returns, unless the user moved it meanwhile).
 Geometries are relative to the monitor's top left corner.
*/
struct ScreenHome
    {
    ScreenHome() : maximize( 0 ), fullscreen( false ), user_moved( false ) {}
    bool isNull() const { return id.isEmpty(); }
    QString toString() const;
    static ScreenHome fromString( const QString& s );
    QString id; // the monitor
    QRect geometry; // frame
    QRect restore; // maximize restore geometry
    QRect fs_restore; // full screen restore geometry
    int maximize; // Client::MaximizeMode
    bool fullscreen;
    bool user_moved; // the user moved it on the other monitor: it stays
    };

} // namespace

#endif
