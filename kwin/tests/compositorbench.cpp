/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2026 The KDE3 revival project

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

/*
 A benchmark of the compositor's renderer backends with a desktop-like
 scene: a desktop window over the whole screen, panels, twelve application
 windows (two of them ARGB, translucent), soft shadows, drawn with the
 compositor's painting algorithm (opaque windows top-down with occlusion,
 shadows and translucent windows bottom-up).

   compositorbench [--backend gles|xrender] [--frames N] [--compare out.png]

 Measures, for a full repaint of the screen and for a small (typing)
 repaint: the CPU time to issue a frame and the time until the GPU finished
 it; then a Present loop (full and partial damage), paced by the
 completions, with the missed refreshes.  --compare draws the same frame
 with both backends and reports the differences.

 Run it on a display of its own (it creates windows and redirects the
 root's children): e.g. a rootful Xwayland on a headless compositor.
*/

#include <config.h>

#include <qapplication.h>
#include <qimage.h>
#include <qregion.h>
#include <qvaluelist.h>
#include <qvaluevector.h>

#include "../render.h"
#include "../shadow.h"
#include "../compositing.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xpresent.h>
#include <X11/extensions/Xrender.h>
#include <fixx11h.h>

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>

using namespace KWinInternal;
using namespace KWinInternal::Render;

static double now()
    {
    return Compositing::now();
    }

static double cpuTime()
    {
    struct timespec ts;
    clock_gettime( CLOCK_PROCESS_CPUTIME_ID, &ts );
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
    }

// CPU time of another process (the X server), ms
static double processTime( int pid )
    {
    if( pid <= 0 )
        return 0;
    char path[ 64 ];
    snprintf( path, sizeof( path ), "/proc/%d/stat", pid );
    FILE* f = fopen( path, "r" );
    if( f == NULL )
        return 0;
    char buf[ 1024 ];
    size_t n = fread( buf, 1, sizeof( buf ) - 1, f );
    fclose( f );
    buf[ n ] = 0;
    const char* p = strrchr( buf, ')' );
    if( p == NULL )
        return 0;
    unsigned long utime = 0, stime = 0;
    // fields after the command: state ppid pgrp session tty tpgid flags minflt cminflt majflt cmajflt utime stime
    sscanf( p + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &utime, &stime );
    return ( utime + stime ) * 1000.0 / sysconf( _SC_CLK_TCK );
    }

struct Win
    {
    Window id;
    QRect geom;
    bool argb;
    bool active;
    Pixmap pixmap;
    Texture* tex;
    QRegion clip; // below the opaque windows above
    };

static Display* dpy;
static Window root;
static int sw, sh;
static QValueList< Win > wins;
static Renderer* renderer;
static ShadowCache* shadows;

static void fillWindow( Win& w, int seed )
    {
    // a title bar, a "document" with text-like lines, a scroll bar
    XRenderPictFormat* fmt = XRenderFindStandardFormat( dpy, w.argb ? PictStandardARGB32 : PictStandardRGB24 );
    Picture pic = XRenderCreatePicture( dpy, w.id, fmt, 0, NULL );
    XRenderColor bg = { 0xf000, 0xf000, 0xf000, 0xffff };
    if( w.argb )
        { bg.red = bg.green = bg.blue = 0x1000; bg.alpha = 0xc000; } // premultiplied dark, 75 %
    XRenderFillRectangle( dpy, PictOpSrc, pic, &bg, 0, 0, w.geom.width(), w.geom.height());
    XRenderColor title = { (unsigned short)( 0x3000 + seed * 0x0800 ), 0x5000, 0xa000, 0xffff };
    XRenderFillRectangle( dpy, PictOpSrc, pic, &title, 0, 0, w.geom.width(), 26 );
    XRenderColor text = { 0x2000, 0x2000, 0x2000, 0xffff };
    if( w.argb )
        { text.red = 0xd000; text.green = 0xd000; text.blue = 0xd000; }
    for( int y = 40; y < w.geom.height() - 10; y += 18 )
        for( int x = 10; x < w.geom.width() - 40; x += 7 + ( x * 7 + y + seed ) % 30 )
            XRenderFillRectangle( dpy, PictOpSrc, pic, &text, x, y, 3 + ( x + y ) % 4, 9 );
    XRenderFreePicture( dpy, pic );
    }

static Window createWindow( const QRect& r, bool argb )
    {
    XSetWindowAttributes attr;
    attr.override_redirect = True;
    attr.background_pixel = 0;
    attr.border_pixel = 0;
    unsigned long mask = CWOverrideRedirect | CWBackPixel | CWBorderPixel;
    Visual* visual = CopyFromParent;
    int depth = CopyFromParent;
    if( argb )
        {
        XVisualInfo tmpl;
        tmpl.screen = DefaultScreen( dpy );
        tmpl.depth = 32;
        tmpl.c_class = TrueColor;
        int n = 0;
        XVisualInfo* vi = XGetVisualInfo( dpy, VisualScreenMask | VisualDepthMask | VisualClassMask, &tmpl, &n );
        if( vi != NULL && n > 0 )
            {
            visual = vi[ 0 ].visual;
            depth = 32;
            attr.colormap = XCreateColormap( dpy, root, visual, AllocNone );
            mask |= CWColormap;
            }
        if( vi != NULL )
            XFree( vi );
        }
    Window w = XCreateWindow( dpy, root, r.x(), r.y(), r.width(), r.height(), 0, depth, InputOutput, visual,
        mask, &attr );
    XMapWindow( dpy, w );
    return w;
    }

static void buildScene()
    {
    srand( 7 );
    // desktop, two panels, twelve windows (2 ARGB)
    QValueList< QRect > rects;
    rects.append( QRect( 0, 0, sw, sh ));
    rects.append( QRect( 0, sh - 32, sw / 2, 32 ));
    rects.append( QRect( sw / 2, sh - 32, sw / 2, 32 ));
    for( int i = 0;
         i < 12;
         ++i )
        {
        const int w = 800 + rand() % 1000;
        const int h = 600 + rand() % 500;
        rects.append( QRect( 40 + rand() % QMAX( 1, sw - w - 80 ), 30 + rand() % QMAX( 1, sh - h - 90 ), w, h ));
        }
    int index = 0;
    for( QValueList< QRect >::ConstIterator it = rects.begin();
         it != rects.end();
         ++it, ++index )
        {
        Win w;
        w.geom = *it;
        w.argb = index == 6 || index == 11;
        w.active = index == (int) rects.count() - 1;
        w.id = createWindow( w.geom, w.argb );
        w.pixmap = None;
        w.tex = NULL;
        wins.append( w );
        }
    XSync( dpy, False );
    // (drawn when redirected: ARGB contents need their own pixmaps)
    XCompositeRedirectSubwindows( dpy, root, CompositeRedirectManual );
    index = 0;
    for( QValueList< Win >::Iterator it = wins.begin();
         it != wins.end();
         ++it, ++index )
        fillWindow( *it, index );
    XSync( dpy, False );
    }

static void bindScene()
    {
    for( QValueList< Win >::Iterator it = wins.begin();
         it != wins.end();
         ++it )
        {
        if( (*it).tex != NULL )
            delete (*it).tex;
        if( (*it).pixmap == None )
            (*it).pixmap = XCompositeNameWindowPixmap( dpy, (*it).id );
        XWindowAttributes attr;
        XGetWindowAttributes( dpy, (*it).id, &attr );
        (*it).tex = renderer->textureFromPixmap( (*it).pixmap, (*it).geom.width(), (*it).geom.height(),
            attr.depth, attr.visual );
        if( getenv( "BENCH_DEBUG" ) != NULL && (*it).tex != NULL )
            printf( "window %lx depth %d alpha %d %dx%d\n", (*it).id, attr.depth, (*it).tex->hasAlpha(),
                (*it).tex->width(), (*it).tex->height());
        if( (*it).tex == NULL )
            {
            fprintf( stderr, "cannot import a window pixmap\n" );
            exit( 1 );
            }
        }
    }

static const QColor shadow_color( 0, 0, 0 );

// the compositor's painting: opaque windows top-down with occlusion, the
// background, then shadows and translucent windows bottom-up
static void paint( Texture* target, const QRegion& region )
    {
    renderer->setTarget( target );
    QRegion left = region;
    for( int i = wins.count() - 1;
         i >= 0;
         --i )
        {
        Win& w = wins[ i ];
        w.clip = left;
        if( !w.argb )
            {
            QRegion visible = left & QRegion( w.geom );
            if( !visible.isEmpty())
                {
                renderer->setClip( visible );
                renderer->drawTexture( w.tex, w.geom.x(), w.geom.y(), 1.0, BlendSource );
                }
            left -= QRegion( w.geom );
            }
        }
    if( !left.isEmpty())
        {
        renderer->setClip( left );
        renderer->fill( RectF( 0, 0, sw, sh ), QColor( 30, 60, 90 ), 1.0, BlendSource );
        }
    for( unsigned int i = 0;
         i < wins.count();
         ++i )
        {
        Win& w = wins[ i ];
        if( i >= 3 ) // not the desktop and the panels
            {
            const int radius = w.active ? 18 : 10;
            const int dy = w.active ? 4 : 2;
            const ShadowTiles* t = shadows->tiles( radius, QRegion(), w.geom.size());
            QRect outer = ShadowCache::outerRect( w.geom, radius, 0, dy );
            QRegion clip = ( w.clip & QRegion( outer )) - QRegion( w.geom );
            if( t != NULL && !clip.isEmpty())
                {
                renderer->setClip( clip );
                renderer->drawNineSlice( t->tile, outer, shadow_color, w.active ? 0.45 : 0.3 );
                }
            }
        if( w.argb )
            {
            QRegion visible = w.clip & QRegion( w.geom );
            if( !visible.isEmpty())
                {
                renderer->setClip( visible );
                renderer->drawTexture( w.tex, w.geom.x(), w.geom.y(), 1.0, BlendOver );
                }
            }
        }
    renderer->clearClip();
    }

static Texture* readback_target;
static Pixmap readback_pixmap;

// waits until the GPU finished drawing into the target
static void waitGpu( Pixmap pixmap )
    {
    renderer->finish();
    // a 1x1 read of the target: the X server (glamor) waits for the buffer
    // (implicit fences of our rendering / its own rendering)
    XImage* img = XGetImage( dpy, pixmap, 0, 0, 1, 1, AllPlanes, ZPixmap );
    if( img != NULL )
        XDestroyImage( img );
    }

static double median( QValueVector< double > v )
    {
    if( v.isEmpty())
        return 0;
    std::sort( v.begin(), v.end());
    return v[ v.size() / 2 ];
    }

static double percentile( QValueVector< double > v, double p )
    {
    if( v.isEmpty())
        return 0;
    std::sort( v.begin(), v.end());
    return v[ QMIN( (int) v.size() - 1, (int)( v.size() * p )) ];
    }

static void measure( const char* label, const QRegion& region, int frames, int xpid )
    {
    QValueVector< double > cpu, total;
    double x0 = processTime( xpid );
    for( int i = 0;
         i < frames;
         ++i )
        {
        const double t0 = now();
        const double c0 = cpuTime();
        renderer->acquireClientBuffers();
        paint( readback_target, region );
        renderer->finish();
        const double c1 = cpuTime();
        waitGpu( readback_pixmap );
        const double t1 = now();
        cpu.append( c1 - c0 );
        total.append( t1 - t0 );
        }
    double x1 = processTime( xpid );
    printf( "%-28s issue CPU median %6.3f ms p95 %6.3f | until done median %6.3f ms p95 %6.3f | X server CPU %5.3f ms/frame\n",
        label, median( cpu ), percentile( cpu, 0.95 ), median( total ), percentile( total, 0.95 ),
        ( x1 - x0 ) / frames );
    }

// the Present loop: frames rendered when the previous one completed
static int completed_count;
static unsigned long long last_msc;
static int missed;
static double last_ust_ms;
static QValueVector< double > intervals;

static void handlePresent( XEvent* e, XID eid, bool* idle, Pixmap* pixmaps, int npix )
    {
    XGenericEventCookie* cookie = &e->xcookie;
    if( !XGetEventData( dpy, cookie ))
        return;
    if( cookie->evtype == PresentCompleteNotify )
        {
        XPresentCompleteNotifyEvent* ce = (XPresentCompleteNotifyEvent*) cookie->data;
        if( ce->eid == eid && ce->kind == PresentCompleteKindPixmap )
            {
            if( last_msc != 0 && ce->msc > last_msc + 1 )
                missed += ce->msc - last_msc - 1;
            if( last_ust_ms > 0 )
                intervals.append( ce->ust / 1000.0 - last_ust_ms );
            last_msc = ce->msc;
            last_ust_ms = ce->ust / 1000.0;
            ++completed_count;
            }
        }
    else if( cookie->evtype == PresentIdleNotify )
        {
        XPresentIdleNotifyEvent* ie = (XPresentIdleNotifyEvent*) cookie->data;
        for( int i = 0;
             i < npix;
             ++i )
            if( pixmaps[ i ] == ie->pixmap )
                idle[ i ] = true;
        }
    XFreeEventData( dpy, cookie );
    }

static void presentLoop( const char* label, Window overlay, int present_opcode, bool partial, int frames, int xpid )
    {
    const int N = 3;
    Texture* buffers[ N ];
    Pixmap pixmaps[ N ];
    bool idle[ N ];
    int age_frame[ N ]; // the frame number each buffer shows (-1: none)
    for( int i = 0;
         i < N;
         ++i )
        {
        buffers[ i ] = renderer->createPresentBuffer( overlay, sw, sh, &pixmaps[ i ] );
        idle[ i ] = true;
        age_frame[ i ] = -1;
        if( buffers[ i ] == NULL )
            {
            fprintf( stderr, "no present buffer\n" );
            return;
            }
        }
    XID eid = XPresentSelectInput( dpy, overlay, PresentCompleteNotifyMask | PresentIdleNotifyMask );
    completed_count = 0;
    last_msc = 0;
    missed = 0;
    last_ust_ms = 0;
    intervals.clear();
    QValueVector< QRegion > damage_history; // per frame
    QValueVector< double > cpu, repaint_px;
    int queued = 0;
    unsigned int serial = 0;
    int frame = 0;
    const double start = now();
    const double x0 = processTime( xpid );
    // a "typing" damage moving along a line, or everything
    while( completed_count < frames && now() - start < frames * 100.0 )
        {
        while( XPending( dpy ))
            {
            XEvent e;
            XNextEvent( dpy, &e );
            if( e.type == GenericEvent && e.xgeneric.extension == present_opcode )
                {
                int before = completed_count;
                handlePresent( &e, eid, idle, pixmaps, N );
                queued -= completed_count - before;
                }
            }
        int buf = -1;
        for( int i = 0;
             i < N;
             ++i )
            if( idle[ i ] && ( buf < 0 || age_frame[ i ] > age_frame[ buf ] ))
                buf = i;
        if( queued >= 1 || buf < 0 )
            {
            // wait for an event (not XPeekEvent: libXpresent can't copy idle events)
            struct pollfd pfd;
            pfd.fd = ConnectionNumber( dpy );
            pfd.events = POLLIN;
            poll( &pfd, 1, 100 );
            continue;
            }
        QRegion damage = partial ? QRegion( 600 + ( frame * 9 ) % 1200, 400, 20, 18 ) : QRegion( 0, 0, sw, sh );
        damage_history.append( damage );
        QRegion repaint;
        if( age_frame[ buf ] < 0 )
            repaint = QRegion( 0, 0, sw, sh );
        else
            for( int f = age_frame[ buf ] + 1;
                 f <= frame;
                 ++f )
                repaint |= damage_history[ f ];
        QMemArray< QRect > rr = repaint.rects();
        double px = 0;
        for( unsigned int i = 0;
             i < rr.size();
             ++i )
            px += rr[ i ].width() * (double) rr[ i ].height();
        repaint_px.append( px );
        const double c0 = cpuTime();
        renderer->acquireClientBuffers();
        paint( buffers[ buf ], repaint );
        renderer->finish();
        XserverRegion update = None;
        if( partial )
            {
            QMemArray< QRect > dr = damage.rects();
            XRectangle xr[ 16 ];
            int n = 0;
            for( unsigned int i = 0;
                 i < dr.size() && n < 16;
                 ++i, ++n )
                {
                xr[ n ].x = dr[ i ].x();
                xr[ n ].y = dr[ i ].y();
                xr[ n ].width = dr[ i ].width();
                xr[ n ].height = dr[ i ].height();
                }
            update = XFixesCreateRegion( dpy, xr, n );
            }
        XPresentPixmap( dpy, overlay, pixmaps[ buf ], ++serial, None, update, 0, 0, None, None, None,
            PresentOptionNone, 0, 0, 0, NULL, 0 );
        if( update != None )
            XFixesDestroyRegion( dpy, update );
        XFlush( dpy );
        cpu.append( cpuTime() - c0 );
        idle[ buf ] = false;
        age_frame[ buf ] = frame;
        ++frame;
        ++queued;
        }
    const double elapsed = now() - start;
    const double x1 = processTime( xpid );
    XPresentFreeInput( dpy, overlay, eid );
    printf( "%-28s %d frames in %.0f ms (%.1f fps), interval median %.2f ms p95 %.2f, missed refreshes %d,"
        " CPU/frame %.3f ms (p95 %.3f), X server CPU %.3f ms/frame, repaint median %.0f px\n",
        label, completed_count, elapsed, completed_count * 1000.0 / elapsed, median( intervals ),
        percentile( intervals, 0.95 ), missed, median( cpu ), percentile( cpu, 0.95 ),
        ( x1 - x0 ) / QMAX( 1, completed_count ), median( repaint_px ));
    for( int i = 0;
         i < N;
         ++i )
        delete buffers[ i ];
    }

static QImage grab( Pixmap pixmap )
    {
    XImage* xi = XGetImage( dpy, pixmap, 0, 0, sw, sh, AllPlanes, ZPixmap );
    QImage img( sw, sh, 32 );
    for( int y = 0;
         y < sh;
         ++y )
        for( int x = 0;
             x < sw;
             ++x )
            img.setPixel( x, y, XGetPixel( xi, x, y ) | 0xff000000 );
    XDestroyImage( xi );
    return img;
    }

int main( int argc, char** argv )
    {
    Renderer::Backend backend = Renderer::Auto;
    int frames = 200;
    const char* compare = NULL;
    int xpid = 0;
    for( int i = 1;
         i < argc;
         ++i )
        {
        if( strcmp( argv[ i ], "--backend" ) == 0 && i + 1 < argc )
            {
            ++i;
            backend = strcmp( argv[ i ], "gles" ) == 0 ? Renderer::GLES
                : strcmp( argv[ i ], "xrender" ) == 0 ? Renderer::XRender : Renderer::Auto;
            }
        else if( strcmp( argv[ i ], "--frames" ) == 0 && i + 1 < argc )
            frames = atoi( argv[ ++i ] );
        else if( strcmp( argv[ i ], "--compare" ) == 0 && i + 1 < argc )
            compare = argv[ ++i ];
        else if( strcmp( argv[ i ], "--xpid" ) == 0 && i + 1 < argc )
            xpid = atoi( argv[ ++i ] );
        }
    const char* display = getenv( "DISPLAY" );
    if( display == NULL || strcmp( display, ":0" ) == 0 || strcmp( display, ":1" ) == 0
        || strncmp( display, ":0.", 3 ) == 0 || strncmp( display, ":1.", 3 ) == 0 )
        {
        fprintf( stderr, "refusing to run on DISPLAY=%s\n", display ? display : "(none)" );
        return 1;
        }
    QApplication app( argc, argv, false );
    dpy = XOpenDisplay( NULL );
    if( dpy == NULL )
        return 1;
    root = DefaultRootWindow( dpy );
    sw = DisplayWidth( dpy, DefaultScreen( dpy ));
    sh = DisplayHeight( dpy, DefaultScreen( dpy ));
    Compositing::checkExtensions( dpy );
    int present_opcode = 0, ev, err;
    XPresentQueryExtension( dpy, &present_opcode, &ev, &err );
    buildScene();
    Window overlay = XCompositeGetOverlayWindow( dpy, root );
    XserverRegion empty = XFixesCreateRegion( dpy, NULL, 0 );
    XFixesSetWindowShapeRegion( dpy, overlay, ShapeInput, 0, 0, empty );
    XFixesDestroyRegion( dpy, empty );
    XSync( dpy, False );
    QString error;
    for( int pass = 0;
         pass < ( compare != NULL ? 2 : 1 );
         ++pass )
        {
        Renderer::Backend b = compare != NULL ? ( pass == 0 ? Renderer::XRender : Renderer::GLES ) : backend;
        renderer = Renderer::create( dpy, b, &error );
        if( renderer == NULL )
            {
            fprintf( stderr, "no renderer: %s\n", error.latin1());
            return 1;
            }
        printf( "renderer: %s, screen %dx%d, %d windows\n", renderer->description().latin1(), sw, sh,
            (int) wins.count());
        bindScene();
        shadows = new ShadowCache( renderer );
        readback_target = renderer->createPresentBuffer( overlay, sw, sh, &readback_pixmap );
        if( compare != NULL )
            {
            paint( readback_target, QRegion( 0, 0, sw, sh ));
            waitGpu( readback_pixmap );
            static QImage first;
            QImage img = grab( readback_pixmap );
            if( pass == 0 )
                first = img;
            else
                {
                int differ = 0, maxdiff = 0;
                QImage diff( sw, sh, 32 );
                for( int y = 0;
                     y < sh;
                     ++y )
                    for( int x = 0;
                         x < sw;
                         ++x )
                        {
                        QRgb a = first.pixel( x, y ), c = img.pixel( x, y );
                        int d = QMAX( QABS( qRed( a ) - qRed( c )), QMAX( QABS( qGreen( a ) - qGreen( c )),
                            QABS( qBlue( a ) - qBlue( c ))));
                        if( d > 0 )
                            ++differ;
                        maxdiff = QMAX( maxdiff, d );
                        diff.setPixel( x, y, qRgb( QMIN( 255, d * 32 ), 0, 0 ));
                        }
                printf( "compare xrender/gles: %d pixels differ, max difference %d\n", differ, maxdiff );
                img.save( QString( compare ) + "-gles.png", "PNG" );
                first.save( QString( compare ) + "-xrender.png", "PNG" );
                diff.save( QString( compare ) + "-diff.png", "PNG" );
                }
            }
        else
            {
            // warm up (shadow tiles, first imports)
            for( int i = 0;
                 i < 5;
                 ++i )
                {
                paint( readback_target, QRegion( 0, 0, sw, sh ));
                waitGpu( readback_pixmap );
                }
            measure( "full repaint (5120x1440)", QRegion( 0, 0, sw, sh ), frames, xpid );
            measure( "half (one monitor)", QRegion( 0, 0, sw / 2, sh ), frames, xpid );
            measure( "typing (20x18 px)", QRegion( 900, 400, 20, 18 ), frames, xpid );
            measure( "window move (1400x900 x2)", QRegion( 300, 200, 1400, 900 ) | QRegion( 330, 220, 1400, 900 ),
                frames, xpid );
            double t0 = now();
            for( int i = 0;
                 i < frames;
                 ++i )
                renderer->acquireClientBuffers();
            printf( "%-28s %.3f ms\n", "acquireClientBuffers", ( now() - t0 ) / frames );
            presentLoop( "present loop, full damage", overlay, present_opcode, false, frames, xpid );
            presentLoop( "present loop, typing damage", overlay, present_opcode, true, frames, xpid );
            }
        delete readback_target;
        delete shadows;
        for( QValueList< Win >::Iterator it = wins.begin();
             it != wins.end();
             ++it )
            {
            delete (*it).tex;
            (*it).tex = NULL;
            }
        delete renderer;
        }
    XCompositeReleaseOverlayWindow( dpy, root );
    XCloseDisplay( dpy );
    return 0;
    }
