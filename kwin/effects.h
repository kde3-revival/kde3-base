/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2026 The KDE3 revival project

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

#ifndef KWIN_EFFECTS_H
#define KWIN_EFFECTS_H

#include <qobject.h>
#include <qtimer.h>
#include <qptrlist.h>
#include <qmap.h>
#include <qrect.h>
#include <qstring.h>

#include <X11/Xlib.h>

namespace KWinInternal
{

class Workspace;
class Client;
class EffectAnim;
class EffectScene;
class EffectPicture;

namespace Compositing
{
class Presenter;
}

/*
 Window animations drawn with XRender from the named pixmaps of the
 redirected frames (KeepRedirected, see overview.cpp), like the overview:
 minimize into the taskbar button and restore from it (scaled, or the
 genie), the morph of maximize and restore, open and close.

 The applications never see anything of it: a window is configured once
 (its real new geometry), mapped and unmapped as usual; the animation draws
 pixmaps captured before (the last contents of a minimized or closed
 window, the old size of a maximized one) and waits for the first repaint
 of a window shown or resized before it shows the new contents.

 Drawn into an override-redirect window of our own above all managed
 windows and below the other override-redirect windows (menus, tooltips,
 the screen locker stay above it), shaped to the animated area before it
 is mapped: nothing is exposed when it comes or goes.  It is mapped only
 while something animates.
*/
class Effects : public QObject
    {
    Q_OBJECT
    public:
        Effects( Workspace* ws );
        ~Effects();
        void reconfigure();
        // all X events go here first; true means eaten (own damage events)
        bool x11Event( XEvent* e );
        bool isActive() const;
        // finishes every animation at once (the overview opens, the desktop
        // is switched, the screen gets locked)
        void abort();

        // c is being minimized (its frame is still mapped); animate: the
        // animation is wanted, else only its contents are kept for the restore
        void minimize( Client* c, bool animate );
        // c is being unminimized (its frame not mapped yet)
        void unminimize( Client* c, bool animate );
        // before and after the geometry change of maximize/restore
        void maximizeBegin( Client* c );
        void maximizeEnd( Client* c );
        // a new window is about to be shown for the first time
        void windowOpening( Client* c );
        // c is being closed or withdrawn, its frame still exists
        void windowClosing( Client* c );
        void clientRemoved( Client* c );

    private slots:
        void frameTimeout();
        void pollTimeout();
        void presentCompleted( bool skipped, int missed );
        void presentIdle();

    private:
        enum Style { None_, Scale, Genie };
        bool enabled() const;
        bool canAnimate( Client* c, bool need_shown = true ) const;
        QRect iconRect( Client* c ) const;
        double duration( double factor ) const;
        EffectAnim* findAnim( Client* c ) const;
        EffectAnim* newAnim( int kind, Client* c );
        void startIconify( Client* c, bool minimizing, const QRect& icon );
        bool namePixmap( Window frame, Pixmap* pixmap, QRect* geom, int* depth, Visual** visual );
        void retain( Client* c, Pixmap pixmap, const QSize& size, int depth, Visual* visual );
        void dropRetained( Client* c );
        void freeAnim( EffectAnim* anim );

        bool startSession( const QRect& area );
        void createWindow();
        void showFirstFrame();
        bool extendArea( const QRect& area );
        void endSession();
        bool buildScene();
        bool sceneOk() const;
        void placeWindow();
        void requestFrame();
        void renderFrame( bool immediate = false );
        bool updateAnim( EffectAnim* anim, double t );
        void drawAnim( EffectAnim* anim, XID dst );
        void drawGenie( EffectAnim* anim, EffectPicture& pic, bool scaled_copies, XID dst, double p, double alpha );
        void dumpFrame();
        void report();

        Workspace* ws;
        Display* dpy;
        Window root;
        Style minimize_style;
        bool maximize_anim;
        bool open_close_anim;
        double base_time; // ms of the normal speed

        QPtrList< EffectAnim > anims;
        // the last contents of minimized windows, for their restore
        struct Retained
            {
            Retained() : pixmap( None ), depth( 0 ), visual( NULL ), stamp( 0 ) {}
            Pixmap pixmap;
            QSize size;
            int depth;
            Visual* visual;
            double stamp;
            };
        QMap< Client*, Retained > retained;
        unsigned long retained_bytes;

        // the session: the window, its area, the scene below it
        Window out;
        QRect area;
        EffectScene* scene;
        Compositing::Presenter* presenter;
        QTimer frame_timer;
        QTimer poll_timer;
        bool frame_wanted;
        bool finishing; // the last frame (without animations) is being shown
        bool need_immediate; // our window is new: its first frame is copied at once
        double last_frame;

        // statistics (KWIN_EFFECTS_DEBUG), per session
        bool debug;
        QString dump_dir;
        QString names;
        int frame_count;
        int dump_index;
        double stat_start, stat_setup, stat_render, stat_max_render;
        int stat_skipped, stat_gaps;
        int stat_strips; // the most genie strips in a frame
    };

/*
 Around Client::changeMaximize() (see effects.cpp).
*/
class EffectsMaximize
    {
    public:
        EffectsMaximize( Client* c, bool on );
        ~EffectsMaximize();
    private:
        Client* client;
    };

} // namespace

#endif
