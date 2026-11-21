/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2026 The KDE3 revival project

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

#ifndef KWIN_OVERVIEW_H
#define KWIN_OVERVIEW_H

#include <qobject.h>
#include <qtimer.h>
#include <qptrlist.h>
#include <qvaluelist.h>
#include <qrect.h>
#include <qmap.h>
#include <qstring.h>
#include <kshortcut.h>

#include <X11/Xlib.h>

namespace KWinInternal
{

class Workspace;
class Client;
class OverviewItem;
class OverviewRenderer;

/*
 The window overview: all windows of the current desktop zoom out into
 a grid (per screen), the user picks one with the mouse or the keyboard
 and the windows zoom back in, the picked one on top.

 The overview is drawn into the Composite overlay window from the named
 pixmaps of the (temporarily, automatically) redirected top-level
 windows.  The first frame of the zoom-out and the last frame of the
 zoom-in show every window at its real position at scale 1, so that
 mapping and unmapping the overlay can't be seen.

 Triggers: a hot corner (InputOnly windows, like the electric borders),
 the Super key pressed and released alone (watched with XInput 2 raw key
 events, never grabbed), a global shortcut and the DCOP call
 KWinInterface::showOverview().
*/
class Overview : public QObject
    {
    Q_OBJECT
    public:
        Overview( Workspace* ws );
        ~Overview();

        // shows the overview, or hides it when it is shown
        void toggle();
        void show();
        // starts the zoom-in; activates c (if not NULL) first
        void hide( Client* c = NULL );
        bool isActive() const;

        // all X events go here first; true means eaten
        bool x11Event( XEvent* e );
        void reconfigure();
        void setShortcut( const KShortcut& cut );
        void clientRemoved( Client* c );
        // the desktop was switched behind our back, the screen gets locked...:
        // leave at once, without animation (may be called from anything the
        // overview itself calls, e.g. activateClient())
        void abort();
        // a new frame window, not mapped yet
        void frameCreated( Window frame );
        // the stacking order, a full screen state or the active window changed
        void stackingChanged();
        // keep the hot corners above the electric borders
        void raiseCorners();
        // KWin moves or resizes a window: no barriers meanwhile, the window
        // crosses to the other monitor freely (whatever the server tells
        // about kwin's own grab)
        void setBarriersEnabled( bool enable );

    private slots:
        void frameTimeout();
        void settleTimeout();
        void cornerTimeout();
        void presentWatchdog();
        void monitorsChanged();

    private:
        enum State { Hidden, Settling, Showing, Shown, Hiding, Finishing };

        bool checkExtensions();
        void setKeepRedirected( bool keep );
        bool grabInput();
        void ungrabInput();
        void createCorners();
        void addCorner( const QRect& area );
        void destroyCorners();
        void selectXI2();
        bool handleXI2( XEvent* e );
        bool cornerHit( const QPoint& p ) const;
        bool cornerAllowed( const QPoint& corner, unsigned int buttonState ) const;
        bool blocked() const;
        void updateCorners();
        void updateRedirection();

        void buildItems( bool initial );
        OverviewItem* createItem( Window w, const XWindowAttributes& attr );
        bool bindContents( OverviewItem* item, bool can_copy );
        void releaseContents( OverviewItem* item );
        void clearItems();
        void computeLayout();
        void layoutScreen( const QRect& area, QPtrList< OverviewItem >& list );
        void updateTitles();
        void updateFilter();

        void startTransition( double target );
        void requestFrame();
        void renderFrame();
        void updateMips( OverviewItem* item, int level );
        void present();
        void dumpFrame();
        void finish();
        void cleanup();

        OverviewItem* itemAt( const QPoint& p ) const;
        OverviewItem* firstMatch() const;
        void select( OverviewItem* item );
        void moveSelection( int dx, int dy );
        bool handleKey( XKeyEvent* e );
        bool handleButton( XButtonEvent* e );
        bool superKey( XKeyEvent* e );
        bool superAlone( int keycode );
        void handleMotion( const QPoint& p );
        void startDrag( const QPoint& p );
        void endDrag( bool drop, const QPoint& p );
        void placeDragged( double t );
        int dropTarget( const QPoint& p ) const;
        QRect layoutArea( int screen ) const;
        void drawItem( OverviewItem* item, XID dst );
        void drawBackdrop( XID dst, double dim, double t );
        bool handleDamage( XEvent* e );
        bool handlePresent( XEvent* e );

        static double now();

        Workspace* ws;
        Display* dpy;
        Window root;
        State state;
        bool composite; // window contents available (else icons)
        KShortcut cut;

        QPtrList< OverviewItem > items; // in stacking order, bottom first
        OverviewItem* selected;
        OverviewItem* pressed;
        int pressed_button;
        QPoint press_pos;
        // the press ended otherwise (Escape, the window closed, the drag
        // cancelled): its release does nothing
        bool press_consumed;
        // dragging a thumbnail to another monitor: it follows the pointer
        // (grabbed at drag_fx, drag_fy of its size), shrinking to at most
        // DragSize; nothing is done to the window until the drop
        OverviewItem* dragged;
        QPoint drag_pos;
        double drag_fx, drag_fy;
        double drag_w, drag_h;
        double drag_start;
        int drag_target; // the monitor under the pointer (-1 none or the same)
        double drag_target_start;
        QString filter;

        // the zoom transition: progress 0 = real screen, 1 = overview
        double trans_start;
        double trans_duration;
        double trans_from, trans_to;
        double progress; // current eased value
        double title_start; // titles fade in after the zoom-out
        double title_alpha;

        Window overlay;
        bool own_overlay; // created by us (no Composite)
        Cursor cursor;
        bool grabbed;
        // the top-level windows are redirected all the time (KeepRedirected),
        // or only while the overview is shown
        bool keep_redirected;
        bool temp_redirected;
        // the frames redirected by us one by one (KeepRedirected)
        QMap< Window, bool > redirected;

        OverviewRenderer* r;

        QTimer frame_timer;
        QTimer watchdog_timer;
        QTimer settle_timer;
        double settle_start;
        double last_damage;
        double last_frame;
        bool frame_wanted;

        // hot corners: 1x1 windows in the outer corners of the monitors, and
        // a strip along the edge in the primary monitor's corner when that
        // touches another monitor (the pointer stopped there by a barrier)
        QValueList< Window > corners;
        QValueList< QRect > corner_areas;
        QValueList< bool > corner_mapped;
        struct BarrierLine
            {
            int x1, y1, x2, y2, directions;
            };
        QValueList< BarrierLine > barrier_lines;
        QValueList< XID > barriers; // created from barrier_lines when enabled
        bool barriers_enabled;
        QTimer corner_timer;
        QTimer monitors_timer; // RandR changes, several at once
        bool corner_armed;

        // the Super key pressed alone (XInput 2 raw events)
        bool super_down; // pressed, not released yet
        bool super_alone; // no other key or button since (nor before)
        Time super_time;

        // statistics (KWIN_OVERVIEW_DEBUG)
        bool debug;
        QString dump_dir;
        int frame_count;
        double stat_render, stat_max_render, stat_first_frame;
        int dump_index;
        int stat_skipped, stat_gaps; // Present: frames not shown, refreshes without a new frame
    };

} // namespace

#endif
