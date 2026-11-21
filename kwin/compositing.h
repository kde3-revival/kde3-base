/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2026 The KDE3 revival project

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

#ifndef KWIN_COMPOSITING_H
#define KWIN_COMPOSITING_H

/*
 The pieces shared by the window overview (overview.cpp) and the window
 animations (effects.cpp): the X extensions, error trapping, XRender
 helpers (pictures with transformations, colours) and the frame
 presentation (Presenter: a few buffers shown
 with Present at the refreshes, at most two frames queued).

 Only operations that glamor runs on the GPU belong in every frame:
 composites of pictures with scaling/translating transformations, bilinear
 filters, solid fills and masks made once.  No trapezoids or polygons
 (glamor rasterizes those on the CPU and uploads the mask every time).
*/

#include <qobject.h>
#include <qtimer.h>
#include <qrect.h>
#include <qcolor.h>
#include <qptrlist.h>

#include <X11/Xlib.h>

#ifdef HAVE_XRENDER
#include <X11/extensions/Xrender.h>
#endif

#include <fixx11h.h>

namespace KWinInternal
{

namespace Compositing
{

// the extensions, checked once by checkExtensions()
extern bool have_render; // RENDER 0.10
extern bool have_composite; // Composite 0.3, Damage, XFixes 2 (not broken by PanoramiX)
extern bool composite_panoramix; // Composite left out: several X screens joined by Xinerama
extern int damage_event;
extern bool have_present;
extern int present_opcode;

// checks the extensions once (KWIN_OVERVIEW_NO_PRESENT leaves Present out)
bool checkExtensions( Display* dpy );

// milliseconds, monotonic
double now();

struct RectF
    {
    double x, y, w, h;
    RectF() : x( 0 ), y( 0 ), w( 0 ), h( 0 ) {}
    RectF( double x_, double y_, double w_, double h_ ) : x( x_ ), y( y_ ), w( w_ ), h( h_ ) {}
    RectF( const QRect& r ) : x( r.x()), y( r.y()), w( r.width()), h( r.height()) {}
    bool contains( const QPoint& p ) const
        { return p.x() >= x && p.x() < x + w && p.y() >= y && p.y() < y + h; }
    double cx() const { return x + w / 2; }
    double cy() const { return y + h / 2; }
    // the pixels touched
    QRect bounds() const;
    };

inline double mix( double a, double b, double t )
    {
    return a + ( b - a ) * t;
    }

inline RectF mix( const RectF& a, const RectF& b, double t )
    {
    return RectF( mix( a.x, b.x, t ), mix( a.y, b.y, t ), mix( a.w, b.w, t ), mix( a.h, b.h, t ));
    }

inline double clamp01( double v )
    {
    return v < 0 ? 0 : v > 1 ? 1 : v;
    }

// GNOME's overview curve
inline double easeOutQuad( double t )
    {
    t = 1 - t;
    return 1 - t * t;
    }

inline double easeOutCubic( double t )
    {
    t = 1 - t;
    return 1 - t * t * t;
    }

inline double easeInOutCubic( double t )
    {
    return t < 0.5 ? 4 * t * t * t : 1 - ( -2 * t + 2 ) * ( -2 * t + 2 ) * ( -2 * t + 2 ) / 2;
    }

// error trapping around requests on windows that may vanish any time
void trapErrors( Display* dpy );
int untrapErrors( Display* dpy );
// the errors since trapErrors() (no round trip)
int trappedErrors();

// the Present events of the overview and the animations (true: eaten)
bool dispatchPresentEvent( XEvent* e );

#ifdef HAVE_XRENDER

XRenderColor renderColor( const QColor& c, double alpha );
Picture solidPicture( Display* dpy, const QColor& c, double alpha );
// scaling by 1/sx, 1/sy and translating (the picture's pixel at x, y is
// drawn at ( x - tx ) / sx, ( y - ty ) / sy)
void setTransform( Display* dpy, Picture pic, double sx, double sy, double tx, double ty );
/*
 Frame presentation: a frame is drawn into a free buffer (beginFrame()),
 then shown (endFrame()) in the window at x, y.  With Present the frames go
 to consecutive refreshes, up to two queued in the server, so that a frame
 that takes more than half a refresh to draw doesn't halve the frame rate;
 frameTime() tells when the frame being drawn will be seen.  Without
 Present the buffer is copied at once.

 The Present events of all presenters come through dispatch().
*/
class Presenter : public QObject
    {
    Q_OBJECT
    public:
        enum { Buffers = 3 };
        Presenter( Display* dpy );
        ~Presenter();
        // buffers of w x h for drawing into window win (offset x, y)
        void start( Window win, int x, int y, int w, int h, bool use_present );
        // presenting count (<= Buffers) pixmaps of the window's size made by
        // someone else (the compositor's renderer), never freed here
        void startExternal( Window win, const Pixmap* pixmaps, int count, bool use_present );
        // the index of a buffer the server doesn't use (-1: none, idle() later)
        int freeBuffer() const;
        // shows buffer index at x, y of the window: with Present at the next
        // refresh (update: what changed, in buffer coordinates, for copies;
        // None: all), else copied at once
        void presentBuffer( int index, XID update, int x = 0, int y = 0 );
        bool isBusy( int index ) const { return busy[ index ]; }
        // how the last presentation was done
        enum Mode { ModeUnknown, ModeCopy, ModeFlip, ModeSkip };
        Mode lastMode() const { return last_mode; }
        void stop();
        bool isStarted() const { return window != None; }
        // a new place in the window; the buffers are made anew when larger
        void setGeometry( int x, int y, int w, int h );
        QRect geometry() const { return QRect( off_x, off_y, width, height ); }
        bool usesPresent() const { return present; }
        // a free buffer for the next frame; None when the server still uses
        // all of them (idle() comes later)
        Picture beginFrame();
        Pixmap framePixmap() const { return buffers[ current ]; }
        Picture framePicture() const { return pictures[ current ]; }
        // when the frame begun at time t will be seen
        double frameTime( double t ) const;
        // shows the frame begun last: with Present at the next refresh, with
        // immediate at once (the first frame, identical to the screen)
        void endFrame( bool immediate = false );
        int queued() const { return queue; }
        // may another frame be queued now?
        bool canQueue() const;
        // missed refreshes are counted from the next presentation
        void resetStats() { stat_msc = 0; }
        double refreshPeriod() const { return period; }
        // the last completed presentation: when it was shown (ms,
        // CLOCK_MONOTONIC) and its refresh counter
        double lastShown() const { return last_ust / 1000.0; }
        unsigned long long lastMsc() const { return last_msc; }
        static bool dispatch( XEvent* e );
    signals:
        // a presentation completed (skipped: never shown; missed: refreshes
        // since the frame before without a new frame); also the watchdog
        // when the server stopped telling
        void completed( bool skipped, int missed );
        // a buffer can be used again
        void idle();
    private slots:
        void watchdog();
    private:
        void createBuffers();
        void freeBuffers();
        void handle( XEvent* e );
        bool mscKnown( double t ) const { return last_complete > 0 && t - last_complete < 2.5 * period; }
        Display* dpy;
        Window window;
        int off_x, off_y;
        int width, height;
        int buf_w, buf_h; // the buffers may be larger
        XRenderPictFormat* format;
        int depth;
        Pixmap buffers[ Buffers ];
        Picture pictures[ Buffers ];
        bool busy[ Buffers ];
        GC gc;
        int current;
        bool present;
        bool external; // the buffers aren't ours
        Mode last_mode;
        int buffer_count;
        XID present_eid;
        unsigned int serial;
        int queue; // presentations queued in the server, at consecutive MSCs
        unsigned long long next_msc;
        unsigned long long last_msc;
        unsigned long long last_ust;
        double period; // of the refresh, ms
        double last_complete; // when the last completion event came
        unsigned long long stat_msc;
        QTimer watchdog_timer;
        static QPtrList< Presenter >* presenters;
    };

#endif // HAVE_XRENDER

} // namespace Compositing

} // namespace KWinInternal

#endif
