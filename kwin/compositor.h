/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2026 The KDE3 revival project

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

#ifndef KWIN_COMPOSITOR_H
#define KWIN_COMPOSITOR_H

#include <qobject.h>
#include <qtimer.h>
#include <qptrlist.h>
#include <qmap.h>
#include <qregion.h>
#include <qstring.h>
#include <qvaluevector.h>

#include <X11/Xlib.h>
#include <fixx11h.h>

class KSelectionOwner;

namespace KWinInternal
{

class Workspace;
class Client;
class ShadowCache;
class SceneWindow;
class CompositorOutput;

namespace Render
{
class Renderer;
class Texture;
}

namespace Compositing
{
class Presenter;
}

/*
 KWin as the compositing manager (see compositor.cpp for how it works):
 all top-level windows are redirected, their pixmaps drawn with soft
 shadows and real translucency into the Composite overlay window, through
 the renderer interface (render.h: GLES or XRender), one frame per refresh
 at most, shown with Present (page flips when possible).

 Off when the settings say so ([Compositing] Enabled, by default on when the
 X server renders on a GPU: DRI3 and Present), when another compositing
 manager owns _NET_WM_CM_Sn, when something fails (then KWin goes on
 uncomposited), and while suspended with the shortcut.
*/
class Compositor : public QObject
    {
    Q_OBJECT
    public:
        Compositor( Workspace* ws );
        ~Compositor();
        bool isActive() const { return active; }
        // the settings changed (or initially)
        void reconfigure();
        // the shortcut (Ctrl+Alt+Shift+F12): off while on, else on
        void toggle();
        // all X events go here first; true means eaten (own damage, shape
        // events); the structure events of the root's children are only
        // watched
        bool x11Event( XEvent* e );
        // something about c changed that changes how it is drawn (active,
        // full screen, maximized, opacity): its shadow, the unredirection
        void clientChanged( Client* c );
        // the reason it is off, or what it runs with
        QString status() const;
        // the screen was resized or the monitors changed (RandR)
        void screenChanged();
        // another compositing manager owns _NET_WM_CM_Sn
        static bool foreignCompositor( Display* dpy );
        // the X server renders on a GPU (DRI3 and Present): Enabled's default
        static bool gpuAvailable( Display* dpy );
        void addRepaint( const QRegion& r );
        void addRepaintFull();
        // a window that is drawn directly, not through the compositor (the
        // screen locker, unredirected full screen windows)
        bool isUnredirected( Window w ) const;
        Window overlayWindow() const { return overlay; }
    signals:
        void activeChanged( bool active );
    private slots:
        void performRepaint();
        void outputCompleted( bool skipped, int missed );
        void outputIdle();
        void checkUnredirect();
        void retry();
        void selectionLost();
    private:
        bool start();
        void stop( bool failed, const QString& reason = QString::null );
        bool claimSelection();
        bool createBuffers();
        void freeBuffers();
        void setupOutputs( bool per_monitor );
        void freeOutputs();
        int freeBuffer() const;
        bool presentTo( CompositorOutput* o, int buffer );
        bool outputWanted( const CompositorOutput* o ) const;
        bool leadCompleted( bool skipped, int missed );
        // the scene's windows
        SceneWindow* findWindow( Window w ) const;
        SceneWindow* addWindow( Window w, bool query_attributes );
        void removeWindow( SceneWindow* w );
        void restack( SceneWindow* w, Window above );
        void windowMapped( SceneWindow* w );
        void windowUnmapped( SceneWindow* w );
        void fetchShape( SceneWindow* w );
        void fetchOpacity( SceneWindow* w );
        void fetchType( SceneWindow* w );
        void updateShadow( SceneWindow* w );
        void releaseContents( SceneWindow* w );
        bool bindContents( SceneWindow* w );
        QRegion extents( const SceneWindow* w ) const; // window + shadow, screen coordinates
        void damageWindow( SceneWindow* w ); // all of it, with the shadow
        bool handleDamage( XEvent* e );
        void flushDamage();
        void scheduleRepaint();
        void paint( Render::Texture* target, const QRegion& region );
        void paintBackground( const QRegion& region );
        void updateRootPixmap();
        // full screen windows drawn directly
        bool unredirectCandidate( const SceneWindow* w ) const;
        void setUnredirected( SceneWindow* w, bool unredirect );
        void updateOverlayShape();
        void debugFrame( double start, const QRegion& repaint );
        QString outputStats() const;

        Workspace* ws;
        Display* dpy;
        Window root;
        int screen_w, screen_h;
        bool active;
        bool suspended; // by the shortcut
        bool failed_once;
        int foreign_retries; // another compositing manager when KWin starts
        QString reason; // why it is off
        KSelectionOwner* selection;
        Render::Renderer* renderer;
        ShadowCache* shadows;
        // where frames are presented (see setupOutputs()); presenter is the
        // first one's, which paces the frames
        QPtrList< CompositorOutput > outputs;
        Compositing::Presenter* presenter;
        bool per_monitor;
        bool flips_failed; // the overlay was copied, not flipped
        int copies_in_row;
        int latest_buffer; // drawn last
        double hold_until; // no drawing into buffers before (outputs switched)
        int force_mode; // KWIN_COMPOSITOR_PRESENT: 1 overlay, 2 monitors
        Window overlay;
        // the present buffers and the frame each one shows (buffer age)
        enum { BufferCount = 3 };
        Render::Texture* buffers[ BufferCount ];
        Pixmap buffer_pixmaps[ BufferCount ];
        int buffer_frame[ BufferCount ];
        QValueVector< QRegion > frame_damage; // the last frames' damage, by frame number % size
        int frame_number;
        QRegion pending; // damage not drawn yet (screen coordinates)
        QPtrList< SceneWindow > stacking; // bottom first
        QMap< Window, SceneWindow* > windows;
        QTimer repaint_timer;
        QTimer unredirect_timer;
        QTimer retry_timer;
        bool frame_wanted;
        Pixmap root_pixmap;
        Render::Texture* root_texture;
        QRegion overlay_hole; // where unredirected windows show through
        int shape_event;
        // frame scheduling: drawn render_budget ms before the next refresh
        bool asap;
        double render_budget, min_budget;
        int on_time; // frames in time since the budget changed
        double target_vblank; // the refresh the queued frame is meant for
        int late_at_max; // late frames with the largest budget
        bool late_failed; // the refreshes are not where they seem to be: frames at once
        double submit_time; // when it was presented
        double shown_delay[ 16 ]; // how long after that frames were shown
        int shown_index;
        bool immediate_server; // shows them at once (no refreshes to wait for)
        Atom atom_opacity, atom_type, atom_rootpmap, atom_esetroot;
        Atom atom_types[ 12 ];
        // statistics (KWIN_COMPOSITOR_DEBUG)
        bool debug;
        int stat_frames;
        double stat_render, stat_max_render;
        int stat_skipped, stat_missed, stat_late;
        double stat_start;
    };

/*
 Tells the compositor about a change of c (active, full screen, maximized)
 when it goes out of scope.
*/
class CompositorUpdate
    {
    public:
        CompositorUpdate( Client* c ) : client( c ) {}
        ~CompositorUpdate();
    private:
        Client* client;
    };

} // namespace

#endif
