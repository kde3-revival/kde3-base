/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

/*
 Virtual desktops per monitor (screendesktops.h holds the state).

 - Each monitor shows its own desktop; a window belongs to the monitor showing
   most of it (Workspace::clientScreen()).  A visible window moved to another
   monitor takes the desktop shown there; a hidden one keeps its desktop.
 - The keys, DCOP and _NET_CURRENT_DESKTOP requests switch the monitor under
   the pointer; activating a window switches only the window's monitor.
 - _NET_CURRENT_DESKTOP is the desktop of the active monitor: the monitor of
   the active window, else the monitor switched last.  _KDE_NET_SCREEN_DESKTOPS
   on the root window lists every monitor (x, y, width, height, desktop as
   CARDINALs, KWin's numbering; absent in classic mode) for kicker's pagers
   and taskbars; a _KDE_NET_SCREEN_DESKTOP client message to the root window
   (l[0] desktop, l[1] x, l[2] y of a point on the monitor) switches one.
 - A monitor is known by its EDID (manufacturer, product, serial), else its
   connector, else its RandR monitor name, never by its number.  When one
   goes away (RandR: the decision to switch an output off is kded_randr's,
   with its own wait; KWin follows the X server at once, so there is a single
   decision), its windows go to the primary remaining monitor keeping their
   desktop numbers, the desktop shown there doesn't change, and each window
   remembers its home (ScreenHome, also in the _KDE_NET_WM_SCREEN_HOME window
   property for a KWin restart and in the session).  When the monitor comes
   back it shows its old desktop again and the windows go home, except those
   the user moved meanwhile.  Zero monitors: nothing moves.
*/

#include "workspace.h"
#include "client.h"
#include "atoms.h"

#include <qapplication.h>
#include <qdesktopwidget.h>
#include <kapplication.h>
#include <kconfig.h>
#include <kglobal.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#ifdef KWIN_HAVE_XRANDR
#include <X11/extensions/Xrandr.h>
#if RANDR_MAJOR > 1 || ( RANDR_MAJOR == 1 && RANDR_MINOR >= 5 )
#define KWIN_RANDR_MONITORS
#endif
#endif

extern Time qt_x_time;

namespace KWinInternal
{

bool Workspace::isOnShownDesktop( const Client* c ) const
    {
    if( c->isOnAllDesktops())
        return true;
    if( !desktopsPerScreen() || screen_desktops.outputCount() < 2 )
        return c->isOnDesktop( currentDesktop());
    return c->isOnDesktop( currentDesktop( clientScreen( c )));
    }

void Workspace::setDesktopsPerScreen( bool on )
    {
    if( on == desktopsPerScreen())
        return;
    screen_desktops.setIndependent( on ); // off: all monitors show the active one's desktop
    StackingUpdatesBlocker blocker( this );
    for( ClientList::ConstIterator it = stacking_order.begin();
         it != stacking_order.end();
         ++it )
        if( !(*it)->isOnCurrentDesktop())
            (*it)->updateVisibility();
    for( ClientList::ConstIterator it = stacking_order.fromLast();
         it != stacking_order.end();
         --it )
        if( (*it)->isOnCurrentDesktop())
            (*it)->updateVisibility();
    publishScreenDesktops();
    }

int Workspace::pointerScreen() const
    {
    QDesktopWidget* dw = QApplication::desktop();
    int screen = dw->screenNumber( QCursor::pos());
    if( screen < 0 || screen >= screen_desktops.outputCount())
        screen = 0;
    return screen;
    }

void Workspace::setActiveScreen( int screen )
    {
    if( screen < 0 || screen == activeScreen() || !desktopsPerScreen())
        return;
    screen_desktops.setActive( screen );
    publishScreenDesktops();
    }

/*
 _NET_CURRENT_DESKTOP (the active monitor's desktop) and, in per monitor
 mode, _KDE_NET_SCREEN_DESKTOPS.
*/
void Workspace::publishScreenDesktops()
    {
    if( published_desktop != currentDesktop())
        {
        published_desktop = currentDesktop();
        rootInfo->setCurrentDesktop( published_desktop );
        }
    QString state = ""; // (not null: published_screens is null until the first time)
    QValueList< long > data;
    if( desktopsPerScreen())
        {
        for( int i = 0;
             i < screen_desktops.outputCount() && i < int( screen_geometries.count());
             ++i )
            {
            const QRect& g = screen_geometries[ i ];
            data.append( g.x());
            data.append( g.y());
            data.append( g.width());
            data.append( g.height());
            data.append( currentDesktop( i ));
            state += QString( "%1,%2,%3,%4,%5;" ).arg( g.x()).arg( g.y()).arg( g.width())
                .arg( g.height()).arg( currentDesktop( i ));
            }
        }
    if( state == published_screens && !published_screens.isNull())
        return;
    published_screens = state;
    // KWin's own state for its next start: also the monitors away ("id=desktop" lines)
    if( desktopsPerScreen())
        {
        QCString saved = screen_desktops.save().utf8();
        XChangeProperty( qt_xdisplay(), root, atoms->kde_net_screen_desktops_state, XA_STRING, 8,
            PropModeReplace, reinterpret_cast< const unsigned char* >( saved.data()), saved.length());
        }
    else
        XDeleteProperty( qt_xdisplay(), root, atoms->kde_net_screen_desktops_state );
    if( data.isEmpty())
        XDeleteProperty( qt_xdisplay(), root, atoms->kde_net_screen_desktops );
    else
        {
        QMemArray< long > values( data.count());
        int i = 0;
        for( QValueList< long >::ConstIterator it = data.begin();
             it != data.end();
             ++it )
            values[ i++ ] = *it;
        XChangeProperty( qt_xdisplay(), root, atoms->kde_net_screen_desktops, XA_CARDINAL, 32,
            PropModeReplace, reinterpret_cast< unsigned char* >( values.data()), values.size());
        }
    }

/*
 The monitors at the start, before the first desktop is set.
*/
void Workspace::initScreens()
    {
    QDesktopWidget* dw = QApplication::desktop();
    screen_geometries.clear();
    for( int i = 0;
         i < dw->numScreens();
         ++i )
        screen_geometries.append( dw->screenGeometry( i ));
    screen_desktops.setOutputs( screenIds(), NULL, NULL );
    // a KWin before this one: its state by identity (with the monitors away), else by geometry
    Atom type;
    int format;
    unsigned long count, after;
    unsigned char* data = NULL;
    if( XGetWindowProperty( qt_xdisplay(), root, atoms->kde_net_screen_desktops_state, 0, 4096, False,
            XA_STRING, &type, &format, &count, &after, &data ) == Success
        && data != NULL && type == XA_STRING && format == 8 )
        restart_state = QString::fromUtf8( reinterpret_cast< const char* >( data ), count );
    if( data != NULL )
        XFree( data );
    data = NULL;
    if( restart_state.isEmpty()
        && XGetWindowProperty( qt_xdisplay(), root, atoms->kde_net_screen_desktops, 0, 5 * 64, False,
            XA_CARDINAL, &type, &format, &count, &after, &data ) == Success
        && data != NULL && type == XA_CARDINAL && format == 32 )
        {
        const long* l = reinterpret_cast< long* >( data );
        for( unsigned long i = 0;
             i + 4 < count;
             i += 5 )
            {
            const QRect g( l[ i ], l[ i + 1 ], l[ i + 2 ], l[ i + 3 ] );
            for( int s = 0;
                 s < int( screen_geometries.count());
                 ++s )
                if( screen_geometries[ s ] == g && !screen_desktops.id( s ).isEmpty())
                    restart_state += screen_desktops.id( s ) + '=' + QString::number( l[ i + 4 ] ) + '\n';
            }
        }
    if( data != NULL )
        XFree( data );
    }

/*
 After the first setCurrentDesktop(): the desktops of the monitors from the
 session, else from the root window property of the KWin before this one.
*/
void Workspace::readScreenDesktops()
    {
    if( !desktopsPerScreen())
        {
        publishScreenDesktops(); // (removes a stale property)
        return;
        }
    if( kapp->isSessionRestored())
        {
        KConfig* config = kapp->sessionConfig();
        KConfigGroupSaver saver( config, "Session" );
        screen_desktops.restore( config->readEntry( "screenDesktops" ));
        const int active = screen_desktops.output( config->readEntry( "activeScreen" ));
        if( active >= 0 )
            screen_desktops.setActive( active );
        }
    else if( !restart_state.isEmpty())
        { // what the KWin before this one left (read before the first publishing)
        screen_desktops.restore( restart_state );
        // the active monitor: the pointer's if it shows _NET_CURRENT_DESKTOP
        if( currentDesktop( pointerScreen()) == currentDesktop())
            screen_desktops.setActive( pointerScreen());
        }
    restart_state = QString::null;
    publishScreenDesktops();
    }

/*
 A monitor's identity, never its number: the EDID (manufacturer, product,
 serial number, serial string) of its first output, else the output's
 connector, else the RandR monitor's name ("xrandr --setmonitor"), else
 "screen N" without RandR 1.5.  Identical monitors without serials get the
 connector added.
*/
QStringList Workspace::screenIds() const
    {
    QDesktopWidget* dw = QApplication::desktop();
    const int screens = dw->numScreens();
    QStringList ids;
    QStringList connectors;
    for( int i = 0;
         i < screens;
         ++i )
        {
        ids.append( QString( "screen:%1" ).arg( i ));
        connectors.append( QString::null );
        }
#ifdef KWIN_RANDR_MONITORS
    Display* dpy = qt_xdisplay();
    int event_base, error_base, major = 0, minor = 0;
    if( screens > 0 && XRRQueryExtension( dpy, &event_base, &error_base )
        && XRRQueryVersion( dpy, &major, &minor ) && ( major > 1 || minor >= 5 ))
        {
        int count = 0;
        XRRMonitorInfo* mon = XRRGetMonitors( dpy, root, True, &count );
        if( mon != NULL && count == 0 )
            { // every output off (Qt falls back to the whole root window): none
            XRRFreeMonitors( mon );
            return QStringList();
            }
        XRRScreenResources* res = XRRGetScreenResourcesCurrent( dpy, root );
        Atom edid_atom = XInternAtom( dpy, RR_PROPERTY_RANDR_EDID, False );
        for( int m = 0;
             mon != NULL && m < count;
             ++m )
            {
            const QRect g( mon[ m ].x, mon[ m ].y, mon[ m ].width, mon[ m ].height );
            int screen = -1;
            for( int i = 0;
                 i < screens && screen < 0;
                 ++i )
                if( dw->screenGeometry( i ) == g && connectors[ i ].isNull())
                    screen = i;
            if( screen < 0 )
                continue;
            QString id;
            QString connector;
            if( mon[ m ].noutput > 0 && res != NULL )
                {
                RROutput output = mon[ m ].outputs[ 0 ];
                if( XRROutputInfo* info = XRRGetOutputInfo( dpy, res, output ))
                    {
                    connector = QString::fromLatin1( info->name, info->nameLen );
                    XRRFreeOutputInfo( info );
                    }
                Atom type;
                int format;
                unsigned long items, after;
                unsigned char* edid = NULL;
                if( XRRGetOutputProperty( dpy, output, edid_atom, 0, 64, False, False, AnyPropertyType,
                        &type, &format, &items, &after, &edid ) == Success
                    && edid != NULL && format == 8 && items >= 128 )
                    {
                    id = "edid:";
                    for( int b = 8; // manufacturer, product, serial number
                         b < 16;
                         ++b )
                        id += QString().sprintf( "%02x", edid[ b ] );
                    for( int d = 54; // the serial string descriptor
                         d <= 108;
                         d += 18 )
                        if( edid[ d ] == 0 && edid[ d + 1 ] == 0 && edid[ d + 2 ] == 0 && edid[ d + 3 ] == 0xff )
                            {
                            QCString serial( reinterpret_cast< const char* >( edid + d + 5 ), 14 );
                            id += ':' + QString::fromLatin1( serial ).stripWhiteSpace();
                            }
                    }
                if( edid != NULL )
                    XFree( edid );
                if( id.isEmpty() && !connector.isEmpty())
                    id = "output:" + connector;
                }
            if( id.isEmpty())
                {
                char* name = mon[ m ].name != None ? XGetAtomName( dpy, mon[ m ].name ) : NULL;
                if( name != NULL )
                    {
                    id = QString( "monitor:" ) + QString::fromLatin1( name );
                    XFree( name );
                    }
                }
            if( !id.isEmpty())
                ids[ screen ] = id;
            connectors[ screen ] = connector.isEmpty() ? QString( "%1" ).arg( m ) : connector;
            }
        if( res != NULL )
            XRRFreeScreenResources( res );
        if( mon != NULL )
            XRRFreeMonitors( mon );
        }
#endif
    // the same monitor model twice without serials: the connector tells them
    for( int i = 0;
         i < screens;
         ++i )
        if( ids.contains( ids[ i ] ) > 1 )
            {
            const QString dup = ids[ i ];
            for( int j = i;
                 j < screens;
                 ++j )
                if( ids[ j ] == dup )
                    ids[ j ] = dup + '@' + connectors[ j ];
            }
    return ids;
    }

/*
 The monitors changed (Workspace::desktopResized(), before the work areas):
 windows of monitors gone go to the primary remaining one, windows whose
 monitor is back go home (see the top of this file).
*/
void Workspace::updateScreens()
    {
    QDesktopWidget* dw = QApplication::desktop();
    QValueVector< QRect > geometries;
    for( int i = 0;
         i < dw->numScreens();
         ++i )
        geometries.append( dw->screenGeometry( i ));
    const QStringList ids = screenIds();
    if( geometries.isEmpty() || ids.isEmpty())
        return; // zero monitors: nothing moves (the old ones stay known)
    const QStringList old_ids = screen_desktops.ids();
    const QValueVector< QRect > old_geometries = screen_geometries;
    // the old monitor of every window, from the old geometries
    QMap< Client*, int > old_screen;
    for( ClientList::ConstIterator it = clients.begin();
         it != clients.end();
         ++it )
        {
        const QRect g = (*it)->geometry();
        int best = -1;
        long best_area = 0;
        for( int i = 0;
             i < int( old_geometries.count());
             ++i )
            {
            QRect is = old_geometries[ i ].intersect( g );
            long area = is.isValid() ? long( is.width()) * is.height() : 0;
            if( area > best_area )
                {
                best_area = area;
                best = i;
                }
            }
        old_screen[ *it ] = best;
        }
    QStringList lost;
    QStringList added;
    screen_desktops.setOutputs( ids, &lost, &added );
    screen_geometries = geometries;
    ++block_screen_follow;
    if( !lost.isEmpty())
        {
        const QRect target = geometries[ 0 ]; // the primary monitor (else the first)
        for( ClientList::ConstIterator it = clients.begin();
             it != clients.end();
             ++it )
            {
            Client* c = *it;
            const int from = old_screen[ c ];
            if( from < 0 || from >= int( old_ids.count()) || !lost.contains( old_ids[ from ] ))
                continue;
            if( c->isDesktop() || c->isDock() || c->isTopMenu())
                continue; // they follow the monitors themselves
            c->leaveScreen( old_ids[ from ], old_geometries[ from ], target );
            }
        }
    returnHomes();
    --block_screen_follow;
    }

/*
 Windows whose home monitor is there go back, unless the user moved them
 meanwhile (then they forget their home).
*/
void Workspace::returnHomes()
    {
    ++block_screen_follow;
    for( ClientList::ConstIterator it = clients.begin();
         it != clients.end();
         ++it )
        {
        const ScreenHome& home = (*it)->screenHome();
        if( home.isNull())
            continue;
        const int screen = screen_desktops.output( home.id );
        if( screen < 0 || screen >= int( screen_geometries.count()))
            continue;
        (*it)->returnHome( screen_geometries[ screen ] );
        }
    --block_screen_follow;
    }

/*
 c changed monitor (from: the identity of the old one).  A window shown on
 its old monitor takes the desktop shown on the new one (dragged, sent to
 the other monitor, moved by the application); a hidden one keeps its desktop
 and may become visible there.  Not while KWin itself moves windows for a
 monitor change.
*/
void Workspace::clientScreenChanged( Client* c, const QString& from_id )
    {
    if( !desktopsPerScreen() || block_screen_follow != 0 || !hasClient( c ))
        return;
    const int to = clientScreen( c );
    if( c == active_client )
        setActiveScreen( to );
    if( c->isOnAllDesktops() || c->isDesktop() || c->isDock())
        return;
    const int from = screen_desktops.output( from_id );
    if( from >= 0 && from != to && c->desktop() == currentDesktop( from )
        && c->desktop() != currentDesktop( to ))
        c->setDesktop( currentDesktop( to )); // (updates the visibility)
    else
        c->updateVisibility();
    }

bool Workspace::screenDesktopMessage( XEvent* e )
    {
    if( e->type != ClientMessage || e->xclient.message_type != atoms->kde_net_screen_desktop
        || e->xclient.window != root || e->xclient.format != 32 )
        return false;
    const int desktop = e->xclient.data.l[ 0 ];
    const QPoint p( e->xclient.data.l[ 1 ], e->xclient.data.l[ 2 ] );
    int screen = QApplication::desktop()->screenNumber( p );
    if( screen < 0 )
        screen = pointerScreen();
    setCurrentDesktop( screen, desktop );
    return true;
    }

// Client

/*
 After every geometry change: the monitor showing most of the window.
*/
void Client::checkScreen()
    {
    if( deleting )
        return;
    const QString id = workspace()->screenDesktops().id( workspace()->clientScreen( this ));
    if( id == screen_id )
        return;
    const QString from = screen_id;
    screen_id = id;
    if( !from.isNull()) // (not the first placement)
        workspace()->clientScreenChanged( this, from );
    }

void Client::resetScreen()
    {
    screen_id = workspace()->screenDesktops().id( workspace()->clientScreen( this ));
    }

void Client::setScreenHome( const ScreenHome& home )
    {
    screen_home = home;
    if( home.isNull())
        XDeleteProperty( qt_xdisplay(), window(), atoms->kde_net_wm_screen_home );
    else
        {
        QCString s = home.toString().utf8();
        XChangeProperty( qt_xdisplay(), window(), atoms->kde_net_wm_screen_home, XA_STRING, 8,
            PropModeReplace, reinterpret_cast< const unsigned char* >( s.data()), s.length());
        }
    }

void Client::readScreenHome()
    {
    Atom type;
    int format;
    unsigned long count, after;
    unsigned char* data = NULL;
    if( XGetWindowProperty( qt_xdisplay(), window(), atoms->kde_net_wm_screen_home, 0, 1024, False,
            XA_STRING, &type, &format, &count, &after, &data ) == Success
        && data != NULL && type == XA_STRING && format == 8 )
        screen_home = ScreenHome::fromString( QString::fromUtf8( reinterpret_cast< const char* >( data ), count ));
    if( data != NULL )
        XFree( data );
    }

// the user moved the window: it stays where it is when its monitor comes back
void Client::userMovedFromHome()
    {
    if( !screen_home.isNull() && !screen_home.user_moved )
        {
        ScreenHome h = screen_home;
        h.user_moved = true;
        setScreenHome( h );
        }
    }

} // namespace
