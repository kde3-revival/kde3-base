/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2026 The KDE3 revival project

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

/*
 The window overview (see overview.h).

 Life cycle:

   Hidden    - nothing exists but the hot corner windows
   Settling  - input grabbed, overlay mapped and showing frame 0 (identical
               to the screen), windows redirected; waiting until the
               applications stop repainting after the redirection
   Showing   - zooming out
   Shown     - waiting for the user
   Hiding    - zooming in (the picked window already activated and raised)
   Finishing - the last frame (identical to the screen) is shown, the
               windows are unredirected, waiting for the repaints before
               the overlay is released

 Every frame is computed from the time elapsed since the start of the
 transition, never by accumulating per-frame steps, so slow frames make
 the animation choppier but not longer.
*/

#include <config.h>

#include "overview.h"

#include <qapplication.h>
#include <qdesktopwidget.h>
#include <qpainter.h>
#include <qpixmap.h>
#include <qimage.h>
#include <qcursor.h>
#include <qfile.h>
#include <qvaluevector.h>
#include <kglobalsettings.h>
#include <kstringhandler.h>
#include <kkeynative.h>
#include <kdebug.h>

#include "workspace.h"
#include "client.h"
#include "options.h"
#include "utils.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/shape.h>
#ifdef HAVE_XRENDER
#include <X11/extensions/Xrender.h>
#endif
#if defined( HAVE_XRENDER ) && defined( HAVE_XCOMPOSITE ) && defined( HAVE_XDAMAGE ) && defined( HAVE_XFIXES )
#define OVERVIEW_COMPOSITE
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#endif
#if defined( OVERVIEW_COMPOSITE ) && defined( HAVE_XPRESENT )
#define OVERVIEW_PRESENT
#include <X11/extensions/Xpresent.h>
#endif
#include <fixx11h.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

extern Time qt_x_time;

namespace KWinInternal
{

#ifdef HAVE_XRENDER

namespace
{

// extension state, checked once
bool ext_checked = false;
bool have_render = false;
bool have_composite = false;
int damage_event = 0;
bool have_present = false;
int present_opcode = 0;

// the duration of the filter fades and of the title fade-in
const double FadeTime = 150.0;
// background dimming in the overview
const double DimAlpha = 0.45;
// layout metrics
const int Spacing = 28;
const int TitleGap = 10;
const int MipLevels = 4; // 1, 1/2, 1/4, 1/8

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

// the mip level that is downscaled by at most a half
int mipLevel( double scale )
    {
    int level = 0;
    while( level < MipLevels - 1 && scale * ( 1 << ( level + 1 )) <= 1.0 )
        ++level;
    return level;
    }

// GNOME's overview curve; ease out cubic puts too much of the motion
// into the first frames
inline double easeOutQuad( double t )
    {
    t = 1 - t;
    return 1 - t * t;
    }

// error trapping around requests on windows that may vanish any time
int trapped_errors = 0;
int (*old_error_handler)( Display*, XErrorEvent* ) = NULL;

int trapHandler( Display*, XErrorEvent* )
    {
    ++trapped_errors;
    return 0;
    }

void trapErrors( Display* dpy )
    {
    XSync( dpy, False );
    trapped_errors = 0;
    old_error_handler = XSetErrorHandler( trapHandler );
    }

int untrapErrors( Display* dpy )
    {
    XSync( dpy, False );
    XSetErrorHandler( old_error_handler );
    return trapped_errors;
    }

XRenderColor renderColor( const QColor& c, double alpha )
    {
    XRenderColor col;
    col.alpha = (unsigned short)( clamp01( alpha ) * 0xffff );
    col.red = (unsigned short)( c.red() * 0x101 * alpha );
    col.green = (unsigned short)( c.green() * 0x101 * alpha );
    col.blue = (unsigned short)( c.blue() * 0x101 * alpha );
    return col;
    }

Picture solidPicture( Display* dpy, const QColor& c, double alpha )
    {
    XRenderColor col = renderColor( c, alpha );
    return XRenderCreateSolidFill( dpy, &col );
    }

// an anti-aliased rounded rectangle
void roundedRect( Display* dpy, Picture dst, const RectF& r, double radius, const QColor& c, double alpha )
    {
    if( alpha <= 1.0 / 512 || r.w <= 0 || r.h <= 0 )
        return;
    radius = QMIN( radius, QMIN( r.w, r.h ) / 2 );
    const int steps = 8;
    XPointDouble pts[ 4 * ( steps + 1 ) ];
    int n = 0;
    const double cx[ 4 ] = { r.x + r.w - radius, r.x + r.w - radius, r.x + radius, r.x + radius };
    const double cy[ 4 ] = { r.y + radius, r.y + r.h - radius, r.y + r.h - radius, r.y + radius };
    for( int corner = 0;
         corner < 4;
         ++corner )
        {
        for( int i = 0;
             i <= steps;
             ++i )
            {
            double a = ( corner * 90.0 - 90.0 + 90.0 * i / steps ) * M_PI / 180.0;
            pts[ n ].x = cx[ corner ] + radius * cos( a );
            pts[ n ].y = cy[ corner ] + radius * sin( a );
            ++n;
            }
        }
    Picture src = solidPicture( dpy, c, alpha );
    XRenderCompositeDoublePoly( dpy, PictOpOver, src, dst,
        XRenderFindStandardFormat( dpy, PictStandardA8 ), 0, 0, 0, 0, pts, n, WindingRule );
    XRenderFreePicture( dpy, src );
    }

void setTransform( Display* dpy, Picture pic, double sx, double sy, double tx, double ty )
    {
    XTransform xf = {{
        { XDoubleToFixed( sx ), XDoubleToFixed( 0 ), XDoubleToFixed( tx ) },
        { XDoubleToFixed( 0 ), XDoubleToFixed( sy ), XDoubleToFixed( ty ) },
        { XDoubleToFixed( 0 ), XDoubleToFixed( 0 ), XDoubleToFixed( 1 ) }
        }};
    XRenderSetPictureTransform( dpy, pic, &xf );
    }

// a premultiplied ARGB picture from a QPixmap (icons)
Picture argbPicture( Display* dpy, const QPixmap& pm, Pixmap* pixmap_return )
    {
    *pixmap_return = None;
    if( pm.isNull())
        return None;
    QImage img = pm.convertToImage().convertDepth( 32 );
    const int w = img.width();
    const int h = img.height();
    const bool alpha = img.hasAlphaBuffer();
    unsigned int* data = (unsigned int*) malloc( w * h * 4 );
    if( data == NULL )
        return None;
    for( int y = 0;
         y < h;
         ++y )
        {
        const QRgb* line = (const QRgb*) img.scanLine( y );
        for( int x = 0;
             x < w;
             ++x )
            {
            QRgb p = line[ x ];
            unsigned int a = alpha ? qAlpha( p ) : 255;
            data[ y * w + x ] = ( a << 24 ) | (( qRed( p ) * a / 255 ) << 16 )
                | (( qGreen( p ) * a / 255 ) << 8 ) | ( qBlue( p ) * a / 255 );
            }
        }
    XImage* xi = XCreateImage( dpy, DefaultVisual( dpy, DefaultScreen( dpy )), 32, ZPixmap, 0,
        (char*) data, w, h, 32, w * 4 );
    if( xi == NULL )
        {
        free( data );
        return None;
        }
    xi->byte_order = LSBFirst; // the data is in host order, Xlib converts if needed
    Pixmap p = XCreatePixmap( dpy, DefaultRootWindow( dpy ), w, h, 32 );
    GC gc = XCreateGC( dpy, p, 0, NULL );
    XPutImage( dpy, p, gc, xi, 0, 0, 0, 0, w, h );
    XFreeGC( dpy, gc );
    XDestroyImage( xi ); // frees data
    *pixmap_return = p;
    return XRenderCreatePicture( dpy, p, XRenderFindStandardFormat( dpy, PictStandardARGB32 ), 0, NULL );
    }

} // namespace

class OverviewItem
    {
    public:
        enum Kind { Desktop, Dock, Managed, Other };
        OverviewItem()
            : kind( Other ), client( NULL ), id( None ), depth( 0 ), format( NULL ),
              alpha_format( false ), shaped( false ), pixmap( None ), damage( None ),
              in_layout( false ), screen( 0 ), matches( true ),
              fade_from( 1 ), fade_to( 1 ), fade_start( 0 ), fade( 1 ),
              title_picture( None ), icon_pixmap( None ), icon_picture( None ), seen( true )
            {
            for( int i = 0;
                 i < MipLevels;
                 ++i )
                {
                mip_pixmap[ i ] = None;
                mip_picture[ i ] = None;
                mip_dirty[ i ] = true;
                mip_w[ i ] = mip_h[ i ] = 0;
                }
            }
        Kind kind;
        Client* client;
        Window id;
        QRect geom; // the real geometry, including the X border
        int depth;
        XRenderPictFormat* format;
        bool alpha_format;
        bool shaped;
        QValueVector< XRectangle > shape; // relative to geom
        Pixmap pixmap; // mip level 0 is the named window pixmap
        Pixmap mip_pixmap[ MipLevels ];
        Picture mip_picture[ MipLevels ];
        bool mip_dirty[ MipLevels ];
        int mip_w[ MipLevels ], mip_h[ MipLevels ];
        XID damage;
        bool in_layout;
        int screen;
        QRect slot;
        RectF from, to, cur;
        bool matches;
        double fade_from, fade_to, fade_start, fade;
        QString caption;
        QPixmap title_pixmap;
        Picture title_picture;
        Pixmap icon_pixmap;
        Picture icon_picture;
        int icon_w, icon_h;
        bool seen;
    };

class OverviewRenderer
    {
    public:
        OverviewRenderer()
            : width( 0 ), height( 0 ), format( NULL ), gc( NULL ), current( 0 ),
              present( false ), present_eid( None ), serial( 0 ), queued( 0 ), next_msc( 0 ),
              last_msc( 0 ), last_ust( 0 ), period( 1000.0 / 60 ), last_complete( 0 ), stat_msc( 0 ),
              snapshot( None ), snapshot_picture( None ), filter_picture( None )
            {
            for( int i = 0;
                 i < Buffers;
                 ++i )
                {
                buffers[ i ] = None;
                pictures[ i ] = None;
                busy[ i ] = false;
                }
            }
        int width, height;
        XRenderPictFormat* format;
        enum { Buffers = 3 };
        Pixmap buffers[ Buffers ];
        Picture pictures[ Buffers ];
        bool busy[ Buffers ];
        GC gc;
        int current;
        bool present;
        XID present_eid;
        unsigned int serial;
        // presentations queued in the server, at consecutive MSCs
        int queued;
        unsigned long long next_msc;
        unsigned long long last_msc;
        unsigned long long last_ust;
        double period; // of the refresh, ms
        double last_complete; // when the last completion event came
        unsigned long long stat_msc;
        // the MSC of the next refresh is known only while frames are flowing
        bool mscKnown( double t ) const { return last_complete > 0 && t - last_complete < 2.5 * period; }
        Pixmap snapshot; // the screen, without Composite
        Picture snapshot_picture;
        QPixmap filter_pixmap;
        Picture filter_picture;
        QFont font;
        int title_height;
    };

Overview::Overview( Workspace* w )
    : ws( w ),
      dpy( qt_xdisplay()),
      root( qt_xrootwin()),
      state( Hidden ),
      composite( false ),
      selected( NULL ),
      pressed( NULL ),
      pressed_button( 0 ),
      trans_start( 0 ),
      trans_duration( 0 ),
      trans_from( 0 ),
      trans_to( 0 ),
      progress( 0 ),
      title_start( 0 ),
      title_alpha( 0 ),
      overlay( None ),
      own_overlay( false ),
      cursor( None ),
      grabbed( false ),
      keep_redirected( false ),
      temp_redirected( false ),
      r( NULL ),
      settle_start( 0 ),
      last_damage( 0 ),
      last_frame( 0 ),
      frame_wanted( false ),
      corner_armed( true ),
      frame_count( 0 ),
      stat_render( 0 ),
      stat_max_render( 0 ),
      stat_first_frame( 0 ),
      dump_index( 0 ),
      stat_skipped( 0 ),
      stat_gaps( 0 )
    {
    items.setAutoDelete( true );
    debug = getenv( "KWIN_OVERVIEW_DEBUG" ) != NULL;
    dump_dir = QFile::decodeName( getenv( "KWIN_OVERVIEW_DUMP" ));
    connect( &frame_timer, SIGNAL( timeout()), SLOT( frameTimeout()));
    connect( &watchdog_timer, SIGNAL( timeout()), SLOT( presentWatchdog()));
    connect( &settle_timer, SIGNAL( timeout()), SLOT( settleTimeout()));
    connect( &corner_timer, SIGNAL( timeout()), SLOT( cornerTimeout()));
    checkExtensions();
    reconfigure();
    }

Overview::~Overview()
    {
    abort();
    destroyCorners();
    setKeepRedirected( false );
    }

double Overview::now()
    {
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
    }

bool Overview::checkExtensions()
    {
    if( ext_checked )
        return have_render;
    ext_checked = true;
    int ev, err;
    int major = 0, minor = 0;
    // solid fills need Render 0.10, transformations and filters 0.6
    if( XRenderQueryExtension( dpy, &ev, &err ) && XRenderQueryVersion( dpy, &major, &minor )
        && ( major > 0 || minor >= 10 ))
        have_render = true;
#ifdef OVERVIEW_COMPOSITE
    major = minor = 0;
    // the overlay window needs Composite 0.3
    if( have_render && XCompositeQueryExtension( dpy, &ev, &err )
        && XCompositeQueryVersion( dpy, &major, &minor ) && ( major > 0 || minor >= 3 ))
        {
        int damage_error;
        int fixes_event, fixes_error;
        int fixes_major = 0, fixes_minor = 0;
        if( XDamageQueryExtension( dpy, &damage_event, &damage_error )
            && XFixesQueryExtension( dpy, &fixes_event, &fixes_error )
            && XFixesQueryVersion( dpy, &fixes_major, &fixes_minor ) && fixes_major >= 2 )
            have_composite = true;
        }
#endif
#ifdef OVERVIEW_PRESENT
    if( have_composite && getenv( "KWIN_OVERVIEW_NO_PRESENT" ) == NULL )
        {
        int present_event, present_error;
        if( XPresentQueryExtension( dpy, &present_opcode, &present_event, &present_error ))
            {
            major = minor = 0;
            if( XPresentQueryVersion( dpy, &major, &minor ) && major >= 1 )
                have_present = true;
            }
        }
#endif
    if( !have_render )
        kdWarning( 1212 ) << "Window overview disabled: the X server lacks RENDER 0.10" << endl;
    else if( !have_composite )
        kdWarning( 1212 ) << "Window overview without window contents: no Composite 0.3/Damage/XFixes 2" << endl;
    if( debug )
        kdWarning( 1212 ) << "overview: render " << have_render << " composite " << have_composite
            << " present " << have_present << endl;
    return have_render;
    }

bool Overview::isActive() const
    {
    return state != Hidden;
    }

void Overview::setShortcut( const KShortcut& c )
    {
    cut = c;
    }

void Overview::reconfigure()
    {
    destroyCorners();
    if( have_render && options->overviewHotCorner )
        createCorners();
    if( state == Hidden )
        setKeepRedirected( options->overviewKeepRedirected );
    }

/*
 Redirecting a window gives it a new (empty) pixmap and unredirecting
 drops it: both expose the whole window, i.e. every application would
 repaint everything when the overview opens and again when it closes.
 With KeepRedirected (the default) the windows are redirected once, when
 KWin starts, and the overview itself never makes the applications see
 anything but the focus change of the window that gets activated. The
 cost is that of any composited desktop: one pixmap per mapped
 top-level window, and the server copies every drawing to the screen.
*/
void Overview::setKeepRedirected( bool keep )
    {
#ifdef OVERVIEW_COMPOSITE
    if( !have_composite || keep == keep_redirected )
        return;
    if( keep )
        XCompositeRedirectSubwindows( dpy, root, CompositeRedirectAutomatic );
    else
        XCompositeUnredirectSubwindows( dpy, root, CompositeRedirectAutomatic );
    keep_redirected = keep;
#else
    Q_UNUSED( keep );
#endif
    }

//****************************************
// hot corners
//****************************************

void Overview::createCorners()
    {
    QDesktopWidget* desktop = QApplication::desktop();
    const int pos = options->overviewHotCornerPosition;
    const bool right = pos == 1 || pos == 3;
    const bool bottom = pos == 2 || pos == 3;
    for( int i = 0;
         i < desktop->numScreens();
         ++i )
        {
        QRect g = desktop->screenGeometry( i );
        QPoint p( right ? g.right() : g.left(), bottom ? g.bottom() : g.top());
        // only real corners: nothing beyond them horizontally or vertically
        // (the top left corner of the right monitor is no corner)
        QPoint outx( right ? p.x() + 1 : p.x() - 1, p.y());
        QPoint outy( p.x(), bottom ? p.y() + 1 : p.y() - 1 );
        bool real = true;
        for( int j = 0;
             j < desktop->numScreens();
             ++j )
            {
            QRect g2 = desktop->screenGeometry( j );
            if( g2.contains( outx ) || g2.contains( outy ))
                real = false;
            }
        if( !real || corner_points.contains( p ))
            continue;
        XSetWindowAttributes attr;
        attr.override_redirect = True;
        attr.event_mask = EnterWindowMask | LeaveWindowMask;
        Window w = XCreateWindow( dpy, root, p.x(), p.y(), 1, 1, 0, CopyFromParent, InputOnly,
            CopyFromParent, CWOverrideRedirect | CWEventMask, &attr );
        XMapRaised( dpy, w );
        corners.append( w );
        corner_points.append( p );
        }
    }

void Overview::destroyCorners()
    {
    corner_timer.stop();
    for( QValueList< Window >::ConstIterator it = corners.begin();
         it != corners.end();
         ++it )
        XDestroyWindow( dpy, *it );
    corners.clear();
    corner_points.clear();
    }

bool Overview::cornerHit( const QPoint& p ) const
    {
    return corner_points.contains( p );
    }

bool Overview::cornerAllowed( unsigned int button_state ) const
    {
    if( button_state & ( Button1Mask | Button2Mask | Button3Mask | Button4Mask | Button5Mask ))
        return false; // dragging something
    Client* active = ws->activeClient();
    if( active != NULL && active->isFullScreen())
        return false;
    return true;
    }

void Overview::cornerTimeout()
    {
    Window root_return, child;
    int rx, ry, wx, wy;
    unsigned int mask;
    if( !XQueryPointer( dpy, root, &root_return, &child, &rx, &ry, &wx, &wy, &mask ))
        return;
    if( cornerHit( QPoint( rx, ry )) && cornerAllowed( mask ) && state == Hidden )
        {
        corner_armed = false;
        show();
        }
    }

//****************************************
// showing and hiding
//****************************************

void Overview::toggle()
    {
    if( state == Hidden || state == Hiding || state == Finishing )
        show();
    else
        hide();
    }

bool Overview::grabInput()
    {
    updateXTime();
    if( cursor == None )
        cursor = QCursor( Qt::ArrowCursor ).handle();
    if( XGrabKeyboard( dpy, root, False, GrabModeAsync, GrabModeAsync, qt_x_time ) != GrabSuccess )
        return false;
    if( XGrabPointer( dpy, root, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
        GrabModeAsync, GrabModeAsync, None, cursor, qt_x_time ) != GrabSuccess )
        {
        XUngrabKeyboard( dpy, qt_x_time );
        return false;
        }
    grabbed = true;
    return true;
    }

void Overview::ungrabInput()
    {
    if( !grabbed )
        return;
    grabbed = false;
    XUngrabPointer( dpy, CurrentTime );
    XUngrabKeyboard( dpy, CurrentTime );
    }

void Overview::show()
    {
    if( !have_render )
        return;
    if( state == Hiding || state == Finishing )
        { // reverse a running zoom-in (the activated window stays activated)
        if( state == Finishing )
            return; // too late, the windows are being unredirected
        state = Showing;
        computeLayout();
        updateTitles();
        startTransition( 1.0 );
        return;
        }
    if( state != Hidden )
        return;
    if( !grabInput())
        {
        kdWarning( 1212 ) << "overview: cannot grab the keyboard or the pointer" << endl;
        return;
        }
    composite = have_composite;
    frame_count = 0;
    stat_render = stat_max_render = 0;
    stat_skipped = stat_gaps = 0;
    stat_first_frame = now();
    filter = QString::null;
    selected = pressed = NULL;
    progress = 0;
    title_alpha = 0;

    r = new OverviewRenderer;
    r->width = DisplayWidth( dpy, DefaultScreen( dpy ));
    r->height = DisplayHeight( dpy, DefaultScreen( dpy ));
    r->format = XRenderFindVisualFormat( dpy, DefaultVisual( dpy, DefaultScreen( dpy )));
    r->font = KGlobalSettings::generalFont();
    r->title_height = QFontMetrics( r->font ).height() + 8;

#ifdef OVERVIEW_COMPOSITE
    if( composite )
        {
        // the overlay first: from now on the screen doesn't change
        // until we draw, and the redirection below can't be seen
        overlay = XCompositeGetOverlayWindow( dpy, root );
        own_overlay = false;
        // let the input through (it's grabbed anyway)
        XserverRegion empty = XFixesCreateRegion( dpy, NULL, 0 );
        XFixesSetWindowShapeRegion( dpy, overlay, ShapeInput, 0, 0, empty );
        XFixesDestroyRegion( dpy, empty );
        if( !keep_redirected )
            {
            XCompositeRedirectSubwindows( dpy, root, CompositeRedirectAutomatic );
            temp_redirected = true;
            }
        }
#endif
    if( !composite )
        { // no window contents: a dimmed snapshot of the screen with icons
        XSetWindowAttributes attr;
        attr.override_redirect = True;
        attr.background_pixmap = None;
        overlay = XCreateWindow( dpy, root, 0, 0, r->width, r->height, 0, CopyFromParent,
            InputOutput, CopyFromParent, CWOverrideRedirect | CWBackPixmap, &attr );
        own_overlay = true;
        r->snapshot = XCreatePixmap( dpy, root, r->width, r->height, DefaultDepth( dpy, DefaultScreen( dpy )));
        XGCValues gcv;
        gcv.subwindow_mode = IncludeInferiors;
        GC gc = XCreateGC( dpy, r->snapshot, GCSubwindowMode, &gcv );
        XCopyArea( dpy, root, r->snapshot, gc, 0, 0, r->width, r->height, 0, 0 );
        XFreeGC( dpy, gc );
        r->snapshot_picture = XRenderCreatePicture( dpy, r->snapshot, r->format, 0, NULL );
        }

    r->present = composite && have_present;
    const int buffers = r->present ? (int) OverviewRenderer::Buffers : 1;
    for( int i = 0;
         i < buffers;
         ++i )
        {
        r->buffers[ i ] = XCreatePixmap( dpy, root, r->width, r->height, DefaultDepth( dpy, DefaultScreen( dpy )));
        r->pictures[ i ] = XRenderCreatePicture( dpy, r->buffers[ i ], r->format, 0, NULL );
        }
    r->gc = XCreateGC( dpy, overlay, 0, NULL );
#ifdef OVERVIEW_PRESENT
    if( r->present )
        r->present_eid = XPresentSelectInput( dpy, overlay, PresentCompleteNotifyMask | PresentIdleNotifyMask );
#endif

    buildItems( true );
    computeLayout();
    updateTitles();

    // frame 0: everything where it is
    trans_from = trans_to = progress = 0;
    trans_duration = 0;
    for( OverviewItem* item = items.first();
         item != NULL;
         item = items.next())
        item->from = item->to = item->cur = RectF( item->geom );
    state = Settling;
    renderFrame();
    if( own_overlay )
        XMapRaised( dpy, overlay ); // after the first frame is in the buffer
    // wait for the repaints caused by a new redirection before moving anything
    settle_start = last_damage = now();
    if( !temp_redirected )
        settle_start = last_damage = settle_start - 1000; // nothing to wait for
    settle_timer.start( temp_redirected ? 8 : 0 );
    }

void Overview::settleTimeout()
    {
    double t = now();
    // only a new redirection makes the applications repaint
    bool quiet = ( state == Settling && !temp_redirected )
        || ( t - last_damage >= 30 && t - settle_start >= 30 );
    if( state == Settling )
        {
        if( quiet || t - settle_start >= 150 )
            {
            settle_timer.stop();
            if( debug && temp_redirected )
                kdWarning( 1212 ) << "overview: settled after " << t - settle_start << " ms" << endl;
            // the downscaled copies now, not in the first frames of the zoom
            for( OverviewItem* item = items.first();
                 item != NULL;
                 item = items.next())
                if( item->in_layout && item->mip_picture[ 0 ] != None )
                    updateMips( item, mipLevel( double( item->slot.width()) / item->geom.width()));
            state = Showing;
            stat_first_frame = t = now();
            frame_count = 0;
            stat_render = stat_max_render = 0;
            stat_skipped = stat_gaps = 0;
            startTransition( 1.0 );
            }
        }
    else if( state == Finishing )
        {
        if( quiet || t - settle_start >= 200 )
            {
            settle_timer.stop();
            cleanup();
            }
        }
    else
        settle_timer.stop();
    }

void Overview::hide( Client* c )
    {
    if( state == Hidden || state == Hiding || state == Finishing )
        return;
    settle_timer.stop();
    if( c != NULL && ws->hasClient( c ))
        {
        ws->activateClient( c, true );
        }
    // the new stacking order, windows that appeared meanwhile, new sizes
    buildItems( false );
    title_alpha = 0;
    title_start = 0;
    pressed = NULL;
    // the filtered out windows come back while zooming in
    const double t = now();
    for( OverviewItem* item = items.first();
         item != NULL;
         item = items.next())
        if( item->fade < 1 || item->fade_start > 0 )
            {
            item->fade_from = item->fade;
            item->fade_to = 1;
            item->fade_start = t;
            }
    state = Hiding;
    stat_first_frame = now();
    frame_count = 0;
    stat_render = stat_max_render = 0;
    stat_skipped = stat_gaps = 0;
    startTransition( 0.0 );
    }

void Overview::abort()
    {
    if( state == Hidden )
        return;
    cleanup();
    }

// the zoom-in has drawn its last frame: give the windows back
void Overview::finish()
    {
    if( !temp_redirected )
        { // the screen below the overlay is already what the last frame shows
        cleanup();
        return;
        }
#ifdef OVERVIEW_COMPOSITE
    XCompositeUnredirectSubwindows( dpy, root, CompositeRedirectAutomatic );
    XSync( dpy, False );
    temp_redirected = false;
#endif
    // wait for the repaints before uncovering the windows
    settle_start = last_damage = now();
    settle_timer.start( 8 );
    }

void Overview::cleanup()
    {
    frame_timer.stop();
    watchdog_timer.stop();
    settle_timer.stop();
    if( r == NULL )
        {
        state = Hidden;
        return;
        }
    trapErrors( dpy );
    for( OverviewItem* item = items.first();
         item != NULL;
         item = items.next())
        releaseContents( item );
    items.clear();
#ifdef OVERVIEW_COMPOSITE
    if( temp_redirected )
        XCompositeUnredirectSubwindows( dpy, root, CompositeRedirectAutomatic );
    temp_redirected = false;
#endif
#ifdef OVERVIEW_PRESENT
    if( r->present_eid != None )
        XPresentFreeInput( dpy, overlay, r->present_eid );
#endif
    for( int i = 0;
         i < OverviewRenderer::Buffers;
         ++i )
        {
        if( r->pictures[ i ] != None )
            XRenderFreePicture( dpy, r->pictures[ i ] );
        if( r->buffers[ i ] != None )
            XFreePixmap( dpy, r->buffers[ i ] );
        }
    if( r->snapshot_picture != None )
        XRenderFreePicture( dpy, r->snapshot_picture );
    if( r->snapshot != None )
        XFreePixmap( dpy, r->snapshot );
    if( r->filter_picture != None )
        XRenderFreePicture( dpy, r->filter_picture );
    if( r->gc != NULL )
        XFreeGC( dpy, r->gc );
    if( own_overlay )
        XDestroyWindow( dpy, overlay );
#ifdef OVERVIEW_COMPOSITE
    else if( overlay != None )
        XCompositeReleaseOverlayWindow( dpy, root );
#endif
    overlay = None;
    untrapErrors( dpy );
    delete r;
    r = NULL;
    ungrabInput();
    state = Hidden;
    selected = pressed = NULL;
    filter = QString::null;
    frame_wanted = false;
    // don't open again right away if the pointer is still in the corner
    Window root_return, child;
    int rx, ry, wx, wy;
    unsigned int mask;
    if( XQueryPointer( dpy, root, &root_return, &child, &rx, &ry, &wx, &wy, &mask ))
        corner_armed = !cornerHit( QPoint( rx, ry ));
    XFlush( dpy );
    if( debug )
        kdWarning( 1212 ) << "overview: hidden" << endl;
    }

//****************************************
// items
//****************************************

static bool inOverview( const Client* c )
    {
    return c->isOnCurrentDesktop() && c->isShown( true ) && !c->isSpecialWindow()
        && ( c->isNormalWindow() || c->isDialog());
    }

OverviewItem* Overview::createItem( Window w, const XWindowAttributes& attr )
    {
    OverviewItem* item = new OverviewItem;
    item->id = w;
    item->geom = QRect( attr.x, attr.y, attr.width + 2 * attr.border_width, attr.height + 2 * attr.border_width );
    item->depth = attr.depth;
    item->format = XRenderFindVisualFormat( dpy, attr.visual );
    item->alpha_format = item->format != NULL && item->format->type == PictTypeDirect
        && item->format->direct.alphaMask != 0;
    item->client = ws->findClient( FrameIdMatchPredicate( w ));
    if( Shape::available())
        {
        int count, ordering;
        XRectangle* rects = XShapeGetRectangles( dpy, w, ShapeBounding, &count, &ordering );
        if( rects != NULL )
            {
            if( !( count == 1 && rects[ 0 ].x <= -attr.border_width && rects[ 0 ].y <= -attr.border_width
                && rects[ 0 ].width >= item->geom.width() && rects[ 0 ].height >= item->geom.height()))
                {
                item->shaped = true;
                item->shape.resize( count );
                for( int i = 0;
                     i < count;
                     ++i )
                    {
                    item->shape[ i ] = rects[ i ];
                    // relative to the outer edge of the border
                    item->shape[ i ].x += attr.border_width;
                    item->shape[ i ].y += attr.border_width;
                    }
                }
            XFree( rects );
            }
        }
    return item;
    }

void Overview::bindContents( OverviewItem* item )
    {
#ifdef OVERVIEW_COMPOSITE
    if( !composite || item->format == NULL || item->mip_picture[ 0 ] != None )
        return;
    item->pixmap = XCompositeNameWindowPixmap( dpy, item->id );
    item->mip_picture[ 0 ] = XRenderCreatePicture( dpy, item->pixmap, item->format, 0, NULL );
    item->mip_w[ 0 ] = item->geom.width();
    item->mip_h[ 0 ] = item->geom.height();
    for( int i = 1;
         i < MipLevels;
         ++i )
        item->mip_dirty[ i ] = true;
    if( item->damage == None )
        item->damage = XDamageCreate( dpy, item->id, XDamageReportNonEmpty );
#else
    Q_UNUSED( item );
#endif
    }

void Overview::releaseContents( OverviewItem* item )
    {
    for( int i = 0;
         i < MipLevels;
         ++i )
        {
        if( item->mip_picture[ i ] != None )
            XRenderFreePicture( dpy, item->mip_picture[ i ] );
        if( item->mip_pixmap[ i ] != None )
            XFreePixmap( dpy, item->mip_pixmap[ i ] );
        item->mip_picture[ i ] = None;
        item->mip_pixmap[ i ] = None;
        }
    if( item->pixmap != None )
        XFreePixmap( dpy, item->pixmap );
    item->pixmap = None;
#ifdef OVERVIEW_COMPOSITE
    if( item->damage != None )
        XDamageDestroy( dpy, item->damage );
#endif
    item->damage = None;
    if( item->title_picture != None )
        XRenderFreePicture( dpy, item->title_picture );
    item->title_picture = None;
    if( item->icon_picture != None )
        XRenderFreePicture( dpy, item->icon_picture );
    item->icon_picture = None;
    if( item->icon_pixmap != None )
        XFreePixmap( dpy, item->icon_pixmap );
    item->icon_pixmap = None;
    }

void Overview::clearItems()
    {
    for( OverviewItem* item = items.first();
         item != NULL;
         item = items.next())
        releaseContents( item );
    items.clear();
    }

// (re)reads the top-level windows in their stacking order
void Overview::buildItems( bool initial )
    {
    trapErrors( dpy );
    for( OverviewItem* item = items.first();
         item != NULL;
         item = items.next())
        item->seen = false;
    QPtrList< OverviewItem > list;
    Window root_return, parent;
    Window* children = NULL;
    unsigned int count = 0;
    if( XQueryTree( dpy, root, &root_return, &parent, &children, &count ))
        {
        for( unsigned int i = 0;
             i < count;
             ++i )
            {
            Window w = children[ i ];
            if( w == overlay )
                continue;
            XWindowAttributes attr;
            if( !XGetWindowAttributes( dpy, w, &attr ) || attr.map_state != IsViewable
                || attr.c_class != InputOutput || attr.width <= 0 || attr.height <= 0 )
                continue;
            OverviewItem* item = NULL;
            for( OverviewItem* it = items.first();
                 it != NULL;
                 it = items.next())
                if( it->id == w )
                    {
                    item = it;
                    break;
                    }
            if( item != NULL )
                {
                QRect geom( attr.x, attr.y, attr.width + 2 * attr.border_width, attr.height + 2 * attr.border_width );
                if( geom.size() != item->geom.size())
                    { // the pixmap is stale after a resize
                    releaseContents( item );
                    OverviewItem* fresh = createItem( w, attr );
                    item->geom = fresh->geom;
                    item->shaped = fresh->shaped;
                    item->shape = fresh->shape;
                    delete fresh;
                    if( item->in_layout )
                        updateTitles();
                    }
                item->geom = geom;
                item->seen = true;
                }
            else
                {
                item = createItem( w, attr );
                if( item->client != NULL && item->client->isDesktop())
                    item->kind = OverviewItem::Desktop;
                else if( item->client != NULL && ( item->client->isDock() || item->client->isTopMenu()))
                    item->kind = OverviewItem::Dock;
                else if( initial && item->client != NULL && inOverview( item->client ))
                    item->kind = OverviewItem::Managed;
                else
                    item->kind = OverviewItem::Other;
                item->in_layout = item->kind == OverviewItem::Managed;
                item->from = item->to = item->cur = RectF( item->geom );
                }
            bindContents( item );
            list.append( item );
            }
        if( children != NULL )
            XFree( children );
        }
    // windows that are gone
    items.setAutoDelete( false );
    for( OverviewItem* item = items.first();
         item != NULL;
         item = items.next())
        if( !item->seen )
            {
            if( item == selected )
                selected = NULL;
            if( item == pressed )
                pressed = NULL;
            releaseContents( item );
            delete item;
            }
    items = list;
    items.setAutoDelete( true );
    if( untrapErrors( dpy ) > 0 && debug )
        kdWarning( 1212 ) << "overview: X errors while reading the windows" << endl;
    }

void Overview::clientRemoved( Client* c )
    {
    if( state == Hidden )
        return;
    OverviewItem* item = NULL;
    for( OverviewItem* it = items.first();
         it != NULL;
         it = items.next())
        if( it->client == c )
            {
            item = it;
            break;
            }
    if( item == NULL )
        return;
    if( item == selected )
        selected = NULL;
    if( item == pressed )
        pressed = NULL;
    bool relayout = item->in_layout;
    trapErrors( dpy );
    releaseContents( item );
    untrapErrors( dpy );
    items.remove( item );
    if( relayout && ( state == Settling || state == Showing || state == Shown ))
        {
        computeLayout();
        updateTitles();
        if( state != Settling )
            startTransition( 1.0 );
        }
    requestFrame();
    }

//****************************************
// layout
//****************************************

void Overview::computeLayout()
    {
    QDesktopWidget* desktop = QApplication::desktop();
    const int screens = desktop->numScreens();
    QValueVector< QPtrList< OverviewItem > > per_screen( screens );
    for( OverviewItem* item = items.first();
         item != NULL;
         item = items.next())
        {
        if( !item->in_layout )
            continue;
        // the screen with the largest part of the window
        int best = 0;
        int best_area = -1;
        for( int i = 0;
             i < screens;
             ++i )
            {
            QRect is = desktop->screenGeometry( i ).intersect( item->geom );
            int area = is.isValid() ? is.width() * is.height() : 0;
            if( area > best_area )
                {
                best_area = area;
                best = i;
                }
            }
        if( best_area <= 0 )
            best = desktop->screenNumber( item->geom.center());
        if( best < 0 || best >= screens )
            best = 0;
        item->screen = best;
        per_screen[ best ].append( item );
        }
    for( int i = 0;
         i < screens;
         ++i )
        {
        QRect sg = desktop->screenGeometry( i );
        QRect area = ws->clientArea( PlacementArea, sg.center(), ws->currentDesktop()).intersect( sg );
        if( !area.isValid())
            area = sg;
        int mx = QMAX( 32, area.width() * 4 / 100 );
        int my = QMAX( 32, area.height() * 5 / 100 );
        area.addCoords( mx, my, -mx, -my );
        layoutScreen( area, per_screen[ i ] );
        }
    }

static bool lessY( OverviewItem* a, OverviewItem* b )
    {
    return a->geom.center().y() < b->geom.center().y();
    }

static bool lessX( OverviewItem* a, OverviewItem* b )
    {
    return a->geom.center().x() < b->geom.center().x();
    }

template< typename T >
static void sortItems( QValueVector< OverviewItem* >& v, T less )
    {
    // insertion sort, stable, few items
    for( unsigned int i = 1;
         i < v.size();
         ++i )
        {
        OverviewItem* x = v[ i ];
        int j = i - 1;
        while( j >= 0 && less( x, v[ j ] ))
            {
            v[ j + 1 ] = v[ j ];
            --j;
            }
        v[ j + 1 ] = x;
        }
    }

/*
 Row based: the windows sorted by their vertical position are cut into
 rows of roughly equal width, each row sorted by the horizontal position;
 all windows get the same scale (never above 1), the number of rows that
 allows the largest scale wins.
*/
void Overview::layoutScreen( const QRect& area, QPtrList< OverviewItem >& list )
    {
    const int n = list.count();
    if( n == 0 )
        return;
    const int title_space = r->title_height + TitleGap;
    QValueVector< OverviewItem* > by_y;
    for( OverviewItem* item = list.first();
         item != NULL;
         item = list.next())
        by_y.append( item );
    sortItems( by_y, lessY );
    double total_w = 0;
    for( int i = 0;
         i < n;
         ++i )
        total_w += by_y[ i ]->geom.width();

    double best_scale = -1;
    QValueVector< QValueVector< OverviewItem* > > best_rows;
    for( int rows = 1;
         rows <= n;
         ++rows )
        {
        double ideal = total_w / rows;
        QValueVector< QValueVector< OverviewItem* > > layout;
        QValueVector< OverviewItem* > row;
        double row_w = 0;
        for( int i = 0;
             i < n;
             ++i )
            {
            OverviewItem* item = by_y[ i ];
            if( !row.isEmpty() && (int) layout.size() < rows - 1
                && row_w + item->geom.width() / 2.0 > ideal )
                {
                layout.append( row );
                row.clear();
                row_w = 0;
                }
            row.append( item );
            row_w += item->geom.width();
            }
        layout.append( row );
        if( (int) layout.size() < rows )
            break; // more rows aren't possible
        double scale = 1.0;
        double sum_h = 0;
        for( unsigned int j = 0;
             j < layout.size();
             ++j )
            {
            double w = 0;
            double h = 0;
            for( unsigned int k = 0;
                 k < layout[ j ].size();
                 ++k )
                {
                w += layout[ j ][ k ]->geom.width();
                h = QMAX( h, (double) layout[ j ][ k ]->geom.height());
                }
            scale = QMIN( scale, ( area.width() - Spacing * ( layout[ j ].size() - 1.0 )) / w );
            sum_h += h;
            }
        scale = QMIN( scale, ( area.height() - layout.size() * title_space
            - Spacing * ( layout.size() - 1.0 )) / sum_h );
        if( scale > best_scale * 1.0001 )
            {
            best_scale = scale;
            best_rows = layout;
            }
        }
    if( best_scale <= 0 )
        best_scale = 0.05;

    const double s = best_scale;
    double total_h = 0;
    QValueVector< double > row_h( best_rows.size());
    for( unsigned int j = 0;
         j < best_rows.size();
         ++j )
        {
        double h = 0;
        for( unsigned int k = 0;
             k < best_rows[ j ].size();
             ++k )
            h = QMAX( h, best_rows[ j ][ k ]->geom.height() * s );
        row_h[ j ] = h;
        total_h += h + title_space;
        }
    total_h += Spacing * ( best_rows.size() - 1.0 );
    double y = area.y() + ( area.height() - total_h ) / 2;
    for( unsigned int j = 0;
         j < best_rows.size();
         ++j )
        {
        QValueVector< OverviewItem* > row = best_rows[ j ];
        sortItems( row, lessX );
        double w = Spacing * ( row.size() - 1.0 );
        for( unsigned int k = 0;
             k < row.size();
             ++k )
            w += row[ k ]->geom.width() * s;
        double x = area.x() + ( area.width() - w ) / 2;
        for( unsigned int k = 0;
             k < row.size();
             ++k )
            {
            OverviewItem* item = row[ k ];
            double iw = item->geom.width() * s;
            double ih = item->geom.height() * s;
            // whole pixels in the overview: sharper and stable
            item->slot = QRect( (int) floor( x + 0.5 ), (int) floor( y + ( row_h[ j ] - ih ) / 2 + 0.5 ),
                QMAX( 1, (int) floor( iw + 0.5 )), QMAX( 1, (int) floor( ih + 0.5 )));
            x += iw + Spacing;
            }
        y += row_h[ j ] + title_space + Spacing;
        }
    }

void Overview::updateTitles()
    {
    QFontMetrics fm( r->font );
    for( OverviewItem* item = items.first();
         item != NULL;
         item = items.next())
        {
        if( !item->in_layout || item->client == NULL )
            continue;
        if( item->title_picture != None )
            XRenderFreePicture( dpy, item->title_picture );
        item->title_picture = None;
        item->caption = item->client->caption();
        const int pad = 10;
        int max_w = QMAX( 60, item->slot.width() + 2 * Spacing / 3 ) - 2 * pad;
        QString text = KStringHandler::rPixelSqueeze( item->caption, fm, max_w );
        int w = fm.width( text ) + 2 * pad;
        int h = r->title_height;
        item->title_pixmap.resize( w, h );
        item->title_pixmap.fill( Qt::black );
        QPainter p( &item->title_pixmap );
        p.setFont( r->font );
        p.setPen( Qt::white );
        p.drawText( 0, 0, w, h, Qt::AlignCenter, text );
        p.end();
        XRenderPictureAttributes pa;
        pa.component_alpha = True;
        item->title_picture = XRenderCreatePicture( dpy, item->title_pixmap.handle(),
            XRenderFindVisualFormat( dpy, (Visual*) item->title_pixmap.x11Visual()), CPComponentAlpha, &pa );
        if( !composite && item->icon_picture == None )
            {
            QPixmap icon = item->client->icon();
            item->icon_picture = argbPicture( dpy, icon, &item->icon_pixmap );
            item->icon_w = icon.width();
            item->icon_h = icon.height();
            if( item->icon_picture != None )
                XRenderSetPictureFilter( dpy, item->icon_picture, FilterBilinear, NULL, 0 );
            }
        }
    }

//****************************************
// the filter
//****************************************

// the first matching window in the reading order
OverviewItem* Overview::firstMatch() const
    {
    OverviewItem* first = NULL;
    QPtrListIterator< OverviewItem > it( items );
    for( ;
         it.current() != NULL;
         ++it )
        {
        OverviewItem* item = it.current();
        if( item->in_layout && item->matches
            && ( first == NULL || item->screen < first->screen
                || ( item->screen == first->screen && ( item->slot.y() < first->slot.y()
                    || ( item->slot.y() == first->slot.y() && item->slot.x() < first->slot.x())))))
            first = item;
        }
    return first;
    }

void Overview::updateFilter()
    {
    double t = now();
    for( OverviewItem* item = items.first();
         item != NULL;
         item = items.next())
        {
        if( !item->in_layout )
            continue;
        bool m = filter.isEmpty() || item->caption.contains( filter, false );
        if( m != item->matches )
            {
            item->matches = m;
            item->fade_from = item->fade;
            item->fade_to = m ? 1.0 : 0.25;
            item->fade_start = t;
            }
        }
    if( !filter.isEmpty() || ( selected != NULL && !selected->matches ))
        select( firstMatch());
    // the text
    if( r->filter_picture != None )
        XRenderFreePicture( dpy, r->filter_picture );
    r->filter_picture = None;
    if( !filter.isEmpty())
        {
        QFont f = r->font;
        f.setPointSizeFloat( f.pointSizeFloat() * 1.3 );
        QFontMetrics fm( f );
        int w = fm.width( filter ) + 32;
        int h = fm.height() + 14;
        r->filter_pixmap.resize( w, h );
        r->filter_pixmap.fill( Qt::black );
        QPainter p( &r->filter_pixmap );
        p.setFont( f );
        p.setPen( Qt::white );
        p.drawText( 0, 0, w, h, Qt::AlignCenter, filter );
        p.end();
        XRenderPictureAttributes pa;
        pa.component_alpha = True;
        r->filter_picture = XRenderCreatePicture( dpy, r->filter_pixmap.handle(),
            XRenderFindVisualFormat( dpy, (Visual*) r->filter_pixmap.x11Visual()), CPComponentAlpha, &pa );
        }
    requestFrame();
    }

//****************************************
// animation and rendering
//****************************************

// target 1 = overview, 0 = real screen
void Overview::startTransition( double target )
    {
    double full = options->overviewAnimationTime;
    if( !composite )
        full = 0; // no contents, nothing to animate
    trans_from = progress;
    trans_to = target;
    // a reversal half way takes half the time; a relayout the full time
    double dist = fabs( target - progress );
    trans_duration = dist > 0.001 ? full * QMAX( 0.4, dist ) : full;
    trans_start = now();
    if( r != NULL )
        r->stat_msc = 0;
    for( OverviewItem* item = items.first();
         item != NULL;
         item = items.next())
        {
        item->from = item->cur;
        if( item->in_layout && target > 0.5 )
            item->to = RectF( item->slot );
        else
            item->to = RectF( item->geom );
        }
    requestFrame();
    }

/*
 Frame pacing: without Present a frame every 16 ms at most.  With Present
 up to two frames are queued in the server for consecutive refreshes, so
 a frame that takes more than half a refresh to draw doesn't halve the
 frame rate; each frame shows the animation at the time it will be
 displayed.
*/
void Overview::requestFrame()
    {
    if( r == NULL )
        return;
    frame_wanted = true;
    if( frame_timer.isActive() || state == Finishing )
        return;
    double delay = 0;
    if( r->present )
        {
        if( r->queued >= ( r->mscKnown( now()) ? 2 : 1 ))
            return; // the next completion asks again
        }
    else
        delay = QMAX( 0.0, last_frame + 16.0 - now());
    frame_timer.start( (int) delay, true );
    }

void Overview::frameTimeout()
    {
    if( r == NULL || !frame_wanted )
        return;
    frame_wanted = false;
    renderFrame();
    if( frame_wanted )
        requestFrame();
    }

// no completion events from Present: don't hang
void Overview::presentWatchdog()
    {
    if( r == NULL )
        return;
    r->queued = 0;
    r->next_msc = 0;
    for( int i = 0;
         i < OverviewRenderer::Buffers;
         ++i )
        r->busy[ i ] = false;
    if( state == Finishing )
        finish();
    else if( frame_wanted )
        requestFrame();
    }

// level 1, 2... of the window, each half of the previous one
void Overview::updateMips( OverviewItem* item, int level )
    {
    for( int l = 1;
         l <= level;
         ++l )
        {
        if( item->mip_picture[ l ] != None && !item->mip_dirty[ l ] )
            continue;
        int w = QMAX( 1, ( item->mip_w[ l - 1 ] + 1 ) / 2 );
        int h = QMAX( 1, ( item->mip_h[ l - 1 ] + 1 ) / 2 );
        if( item->mip_picture[ l ] == None )
            {
            item->mip_pixmap[ l ] = XCreatePixmap( dpy, root, w, h, item->depth );
            item->mip_picture[ l ] = XRenderCreatePicture( dpy, item->mip_pixmap[ l ], item->format, 0, NULL );
            item->mip_w[ l ] = w;
            item->mip_h[ l ] = h;
            }
        Picture src = item->mip_picture[ l - 1 ];
        // exactly 2x2 boxes: bilinear sampling between four pixels
        XRenderPictureAttributes pa;
        pa.repeat = RepeatPad;
        XRenderChangePicture( dpy, src, CPRepeat, &pa );
        setTransform( dpy, src, 2, 2, 0, 0 );
        XRenderSetPictureFilter( dpy, src, FilterBilinear, NULL, 0 );
        XRenderComposite( dpy, PictOpSrc, src, None, item->mip_picture[ l ], 0, 0, 0, 0, 0, 0, w, h );
        pa.repeat = RepeatNone;
        XRenderChangePicture( dpy, src, CPRepeat, &pa );
        item->mip_dirty[ l ] = false;
        if( l + 1 < MipLevels )
            item->mip_dirty[ l + 1 ] = true;
        }
    }

void Overview::renderFrame()
    {
    if( r == NULL )
        return;
    // the buffer
    int buf = -1;
    for( int i = 0;
         i < ( r->present ? (int) OverviewRenderer::Buffers : 1 ) && buf < 0;
         ++i )
        if( !r->busy[ i ] )
            buf = i;
    if( buf < 0 )
        { // all still used by the server, wait for an idle event
        frame_wanted = true;
        return;
        }
    r->current = buf;
    Picture dst = r->pictures[ buf ];
    const double t_start = now();
    // the time when this frame will be seen
    const double t0 = t_start + ( r->present ? r->queued * r->period : 0 );
    // the transition
    double lin = trans_duration > 0 ? clamp01(( t0 - trans_start ) / trans_duration ) : 1.0;
    double e = easeOutQuad( lin );
    progress = mix( trans_from, trans_to, e );
    for( OverviewItem* item = items.first();
         item != NULL;
         item = items.next())
        {
        item->cur = mix( item->from, item->to, e );
        if( item->fade_start > 0 )
            {
            double f = clamp01(( t0 - item->fade_start ) / FadeTime );
            item->fade = mix( item->fade_from, item->fade_to, f );
            if( f >= 1 )
                item->fade_start = 0;
            }
        }
    bool more = lin < 1.0;
    if( state == Showing && !more )
        {
        state = Shown;
        title_start = t0;
        if( debug )
            {
            double t = t0 - stat_first_frame;
            kdWarning( 1212 ) << "overview: zoom-out " << frame_count << " frames in " << t << " ms, "
                << ( frame_count > 1 ? frame_count * 1000.0 / t : 0 ) << " fps, render avg "
                << ( frame_count ? stat_render / frame_count : 0 ) << " max " << stat_max_render << " ms, skipped " << stat_skipped << " missed refreshes " << stat_gaps << endl;
            }
        }
    if( state == Shown )
        {
        title_alpha = title_start > 0 ? clamp01(( t0 - title_start ) / FadeTime ) : 1;
        if( title_alpha < 1 )
            more = true;
        }
    else if( state != Showing )
        title_alpha = 0;
    for( OverviewItem* item = items.first();
         item != NULL && !more;
         item = items.next())
        if( item->fade_start > 0 )
            more = true;


    // the background
    XRenderColor black = { 0, 0, 0, 0xffff };
    bool have_desktop = false;
    if( r->snapshot_picture != None )
        {
        XRenderComposite( dpy, PictOpSrc, r->snapshot_picture, None, dst, 0, 0, 0, 0, 0, 0, r->width, r->height );
        have_desktop = true;
        }
    else
        {
        for( OverviewItem* item = items.first();
             item != NULL;
             item = items.next())
            if( item->kind == OverviewItem::Desktop )
                have_desktop = true;
        if( !have_desktop )
            XRenderFillRectangle( dpy, PictOpSrc, dst, &black, 0, 0, r->width, r->height );
        }
    const double dim = progress * ( composite ? DimAlpha : 0.75 );
    QColor highlight = KGlobalSettings::highlightColor();

    // the windows, bottom first
    bool dimmed = false;
    for( OverviewItem* item = items.first();
         item != NULL;
         item = items.next())
        {
        if( !dimmed && item->kind != OverviewItem::Desktop )
            {
            if( dim > 1.0 / 512 )
                {
                XRenderColor c = renderColor( Qt::black, dim );
                XRenderFillRectangle( dpy, PictOpOver, dst, &c, 0, 0, r->width, r->height );
                }
            dimmed = true;
            }
        double alpha = 1.0;
        RectF rect( item->geom );
        bool scaled = false;
        switch( item->kind )
            {
            case OverviewItem::Desktop:
            case OverviewItem::Dock:
                break;
            case OverviewItem::Other:
                alpha = 1.0 - progress;
                break;
            case OverviewItem::Managed:
                if( item->in_layout )
                    {
                    rect = item->cur;
                    alpha = item->fade;
                    scaled = true;
                    }
                else
                    alpha = 1.0 - progress;
                break;
            }
        if( alpha <= 1.0 / 512 )
            continue;
        if( scaled && progress > 0.001 )
            { // a soft shadow and the selection
            double a = QMIN( 1.0, progress ) * alpha;
            roundedRect( dpy, dst, RectF( rect.x - 3, rect.y - 1, rect.w + 6, rect.h + 7 ), 6, Qt::black, 0.10 * a );
            roundedRect( dpy, dst, RectF( rect.x - 1, rect.y, rect.w + 2, rect.h + 3 ), 3, Qt::black, 0.18 * a );
            if( item == selected && title_alpha > 0 )
                roundedRect( dpy, dst, RectF( rect.x - 8, rect.y - 8, rect.w + 16, rect.h + 16 ), 10,
                    highlight, 0.75 * title_alpha * alpha );
            }
        if( !composite )
            { // icons on tiles
            if( !item->in_layout )
                continue;
            roundedRect( dpy, dst, rect, 8, QColor( 64, 64, 64 ), 0.95 * alpha );
            if( item->icon_picture != None && item->icon_w > 0 )
                {
                double size = QMIN( 64.0, QMIN( rect.w, rect.h ) * 0.6 );
                double s = size / QMAX( item->icon_w, item->icon_h );
                double iw = item->icon_w * s;
                double ih = item->icon_h * s;
                int x = (int) floor( rect.cx() - iw / 2 );
                int y = (int) floor( rect.cy() - ih / 2 );
                setTransform( dpy, item->icon_picture, 1 / s, 1 / s, 0, 0 );
                XRenderComposite( dpy, PictOpOver, item->icon_picture, None, dst, 0, 0, 0, 0, x, y,
                    (int) ceil( iw ), (int) ceil( ih ));
                }
            continue;
            }
        if( item->mip_picture[ 0 ] == None )
            continue;
        // the source: the mip level that is downscaled at most by half
        int level = mipLevel( rect.w / item->geom.width());
        updateMips( item, level );
        Picture src = item->mip_picture[ level ];
        const double sw = item->mip_w[ level ];
        const double sh = item->mip_h[ level ];
        const double sx = rect.w / sw;
        const double sy = rect.h / sh;
        const int dx = (int) floor( rect.x );
        const int dy = (int) floor( rect.y );
        const int dw = (int) ceil( rect.x + rect.w ) - dx;
        const int dh = (int) ceil( rect.y + rect.h ) - dy;
        const bool identity = level == 0 && fabs( sx - 1 ) < 1e-9 && fabs( sy - 1 ) < 1e-9
            && rect.x == dx && rect.y == dy;
        if( identity )
            {
            setTransform( dpy, src, 1, 1, 0, 0 );
            XRenderSetPictureFilter( dpy, src, FilterNearest, NULL, 0 );
            }
        else
            {
            setTransform( dpy, src, 1 / sx, 1 / sy, ( dx - rect.x ) / sx, ( dy - rect.y ) / sy );
            XRenderSetPictureFilter( dpy, src, FilterBilinear, NULL, 0 );
            }
        if( item->shaped )
            {
            QValueVector< XRectangle > clip( item->shape.size());
            const double fx = rect.w / item->geom.width();
            const double fy = rect.h / item->geom.height();
            for( unsigned int i = 0;
                 i < item->shape.size();
                 ++i )
                {
                const XRectangle& sr = item->shape[ i ];
                int x1 = (int) floor( rect.x + sr.x * fx + 0.5 );
                int y1 = (int) floor( rect.y + sr.y * fy + 0.5 );
                int x2 = (int) floor( rect.x + ( sr.x + sr.width ) * fx + 0.5 );
                int y2 = (int) floor( rect.y + ( sr.y + sr.height ) * fy + 0.5 );
                clip[ i ].x = x1;
                clip[ i ].y = y1;
                clip[ i ].width = QMAX( 0, x2 - x1 );
                clip[ i ].height = QMAX( 0, y2 - y1 );
                }
            XRenderSetPictureClipRectangles( dpy, dst, 0, 0, &clip[ 0 ], clip.size());
            }
        Picture mask = None;
        if( alpha < 1.0 )
            mask = solidPicture( dpy, Qt::black, alpha );
        XRenderComposite( dpy, item->alpha_format || mask != None ? PictOpOver : PictOpSrc,
            src, mask, dst, 0, 0, 0, 0, dx, dy, dw, dh );
        if( mask != None )
            XRenderFreePicture( dpy, mask );
        if( item->shaped )
            {
            XRenderPictureAttributes pa;
            pa.clip_mask = None;
            XRenderChangePicture( dpy, dst, CPClipMask, &pa );
            }
        }
    if( !dimmed && dim > 1.0 / 512 )
        {
        XRenderColor c = renderColor( Qt::black, dim );
        XRenderFillRectangle( dpy, PictOpOver, dst, &c, 0, 0, r->width, r->height );
        }

    // the titles
    if( title_alpha > 1.0 / 512 )
        {
        for( OverviewItem* item = items.first();
             item != NULL;
             item = items.next())
            {
            if( !item->in_layout || item->title_picture == None )
                continue;
            double a = title_alpha * item->fade;
            int w = item->title_pixmap.width();
            int h = item->title_pixmap.height();
            int x = (int) floor( item->cur.cx() - w / 2.0 + 0.5 );
            int y = (int) floor( item->cur.y + item->cur.h + TitleGap + 0.5 );
            roundedRect( dpy, dst, RectF( x, y, w, h ), h / 2.0, QColor( 24, 24, 24 ), 0.78 * a );
            Picture white = solidPicture( dpy, Qt::white, a );
            XRenderComposite( dpy, PictOpOver, white, item->title_picture, dst, 0, 0, 0, 0, x, y, w, h );
            XRenderFreePicture( dpy, white );
            }
        }
    if( r->filter_picture != None && progress > 0.5 && state != Hiding && state != Finishing )
        {
        QDesktopWidget* desktop = QApplication::desktop();
        QRect sg = desktop->screenGeometry( desktop->primaryScreen());
        QRect area = ws->clientArea( PlacementArea, sg.center(), ws->currentDesktop()).intersect( sg );
        int w = r->filter_pixmap.width();
        int h = r->filter_pixmap.height();
        int x = area.x() + ( area.width() - w ) / 2;
        int y = area.y() + 12;
        roundedRect( dpy, dst, RectF( x, y, w, h ), h / 2.0, QColor( 24, 24, 24 ), 0.85 );
        Picture white = solidPicture( dpy, Qt::white, 1.0 );
        XRenderComposite( dpy, PictOpOver, white, r->filter_picture, dst, 0, 0, 0, 0, x, y, w, h );
        XRenderFreePicture( dpy, white );
        }

    if( !dump_dir.isEmpty())
        dumpFrame();
    present();
    ++frame_count;
    if( debug )
        {
        XSync( dpy, False );
        double t = now() - t_start;
        stat_render += t;
        stat_max_render = QMAX( stat_max_render, t );
        }
    last_frame = t_start;

    if( state == Hiding && !more )
        {
        if( debug )
            {
            double t = t0 - stat_first_frame;
            kdWarning( 1212 ) << "overview: zoom-in " << frame_count << " frames in " << t << " ms, "
                << ( frame_count > 1 ? frame_count * 1000.0 / t : 0 ) << " fps, render avg "
                << ( frame_count ? stat_render / frame_count : 0 ) << " max " << stat_max_render << " ms, skipped " << stat_skipped << " missed refreshes " << stat_gaps << endl;
            }
        state = Finishing;
        if( !r->present || r->queued == 0 )
            {
            XSync( dpy, False );
            finish();
            }
        // else finish() after the completion of the last presentation
        return;
        }
    if( more )
        frame_wanted = true;
    }

void Overview::present()
    {
    int buf = r->current;
#ifdef OVERVIEW_PRESENT
    if( r->present )
        {
        r->busy[ buf ] = true;
        if( !r->mscKnown( now()))
            r->next_msc = 0; // as soon as possible
        ++r->queued;
        XPresentPixmap( dpy, overlay, r->buffers[ buf ], ++r->serial, None, None, 0, 0,
            None, None, None, PresentOptionNone, r->next_msc, 0, 0, NULL, 0 );
        if( r->next_msc != 0 )
            ++r->next_msc;
        watchdog_timer.start( 200, true );
        XFlush( dpy );
        return;
        }
#endif
    XCopyArea( dpy, r->buffers[ buf ], overlay, r->gc, 0, 0, r->width, r->height, 0, 0 );
    XFlush( dpy );
    }

void Overview::dumpFrame()
    {
    XImage* img = XGetImage( dpy, r->buffers[ r->current ], 0, 0, r->width, r->height, AllPlanes, ZPixmap );
    if( img == NULL )
        return;
    static const char* names[] = { "hidden", "settling", "showing", "shown", "hiding", "finishing" };
    QString name;
    name.sprintf( "/frame-%04d-%s.ppm", dump_index++, names[ state ] );
    name.prepend( dump_dir );
    FILE* f = fopen( QFile::encodeName( name ), "wb" );
    if( f != NULL )
        {
        fprintf( f, "P6\n%d %d\n255\n", r->width, r->height );
        QByteArray line( r->width * 3 );
        for( int y = 0;
             y < r->height;
             ++y )
            {
            for( int x = 0;
                 x < r->width;
                 ++x )
                {
                unsigned long p = XGetPixel( img, x, y );
                line[ x * 3 ] = ( p >> 16 ) & 0xff;
                line[ x * 3 + 1 ] = ( p >> 8 ) & 0xff;
                line[ x * 3 + 2 ] = p & 0xff;
                }
            fwrite( line.data(), 1, line.size(), f );
            }
        fclose( f );
        }
    XDestroyImage( img );
    FILE* log = fopen( QFile::encodeName( dump_dir + "/frames.txt" ), "a" );
    if( log != NULL )
        {
        fprintf( log, "%s %.1f %.4f\n", QFile::encodeName( name ).data(), now() - stat_first_frame, progress );
        fclose( log );
        }
    }

//****************************************
// events
//****************************************

bool Overview::handleDamage( XEvent* e )
    {
#ifdef OVERVIEW_COMPOSITE
    XDamageNotifyEvent* de = (XDamageNotifyEvent*) e;
    for( OverviewItem* item = items.first();
         item != NULL;
         item = items.next())
        if( item->damage == de->damage )
            {
            XDamageSubtract( dpy, de->damage, None, None );
            for( int i = 1;
                 i < MipLevels;
                 ++i )
                item->mip_dirty[ i ] = true;
            break;
            }
    last_damage = now();
    if( state != Finishing )
        requestFrame();
#else
    Q_UNUSED( e );
#endif
    return true;
    }

bool Overview::handlePresent( XEvent* e )
    {
#ifdef OVERVIEW_PRESENT
    XGenericEventCookie* cookie = &e->xcookie;
    if( !XGetEventData( dpy, cookie ))
        return true;
    if( r != NULL )
        {
        if( cookie->evtype == PresentCompleteNotify )
            {
            XPresentCompleteNotifyEvent* ce = (XPresentCompleteNotifyEvent*) cookie->data;
            if( ce->kind == PresentCompleteKindPixmap )
                {
                r->queued = QMAX( 0, r->queued - 1 );
                if( ce->mode == PresentCompleteModeSkip )
                    ++stat_skipped;
                else
                    {
                    if( r->stat_msc != 0 && ce->msc > r->stat_msc + 1 && ( state == Showing || state == Hiding ))
                        stat_gaps += ce->msc - r->stat_msc - 1;
                    r->stat_msc = ce->msc;
                    }
                r->last_complete = now();
                if( r->last_ust != 0 && ce->msc > r->last_msc && ce->ust > r->last_ust )
                    {
                    double p = ( ce->ust - r->last_ust ) / 1000.0 / ( ce->msc - r->last_msc );
                    if( p > 4 && p < 50 )
                        r->period = p;
                    }
                r->last_msc = ce->msc;
                r->last_ust = ce->ust;
                if( r->next_msc <= ce->msc )
                    r->next_msc = ce->msc + 1;
                if( r->queued == 0 )
                    watchdog_timer.stop();
                else
                    watchdog_timer.start( 200, true );
                if( state == Finishing )
                    {
                    if( r->queued == 0 )
                        finish();
                    }
                else if( frame_wanted )
                    requestFrame();
                }
            }
        else if( cookie->evtype == PresentIdleNotify )
            {
            XPresentIdleNotifyEvent* ie = (XPresentIdleNotifyEvent*) cookie->data;
            for( int i = 0;
                 i < OverviewRenderer::Buffers;
                 ++i )
                if( r->buffers[ i ] == ie->pixmap )
                    r->busy[ i ] = false;
            if( frame_wanted )
                requestFrame();
            }
        }
    XFreeEventData( dpy, cookie );
#else
    Q_UNUSED( e );
#endif
    return true;
    }

OverviewItem* Overview::itemAt( const QPoint& p ) const
    {
    if( state != Showing && state != Shown )
        return NULL;
    QPtrListIterator< OverviewItem > it( items );
    for( it.toLast();
         it.current() != NULL;
         --it )
        {
        OverviewItem* item = it.current();
        if( !item->in_layout )
            continue;
        RectF rect( item->slot );
        // the title belongs to the window too
        RectF area( rect.x - 8, rect.y - 8, rect.w + 16, rect.h + 16 + r->title_height + TitleGap );
        if( area.contains( p ))
            return item;
        }
    return NULL;
    }

void Overview::select( OverviewItem* item )
    {
    if( item == selected )
        return;
    selected = item;
    requestFrame();
    }

void Overview::moveSelection( int dx, int dy )
    {
    if( selected == NULL )
        {
        OverviewItem* active = NULL;
        OverviewItem* first = NULL;
        for( OverviewItem* item = items.first();
             item != NULL;
             item = items.next())
            if( item->in_layout && item->matches )
                {
                if( first == NULL )
                    first = item;
                if( item->client == ws->activeClient())
                    active = item;
                }
        select( active != NULL ? active : first );
        return;
        }
    QPoint c = selected->slot.center();
    OverviewItem* best = NULL;
    double best_d = 0;
    for( OverviewItem* item = items.first();
         item != NULL;
         item = items.next())
        {
        if( !item->in_layout || !item->matches || item == selected )
            continue;
        QPoint d = item->slot.center() - c;
        double along = d.x() * dx + d.y() * dy;
        if( along <= 0 )
            continue;
        double across = fabs( (double) d.x() * dy ) + fabs( (double) d.y() * dx );
        double dist = along + 2 * across;
        if( best == NULL || dist < best_d )
            {
            best = item;
            best_d = dist;
            }
        }
    if( best != NULL )
        select( best );
    }

bool Overview::handleKey( XKeyEvent* e )
    {
    KKeyNative key( (XEvent*) e );
    if( cut.contains( key ))
        {
        hide();
        return true;
        }
    if( state == Hiding || state == Finishing )
        return true;
    char buf[ 32 ];
    KeySym sym = NoSymbol;
    int len = XLookupString( e, buf, sizeof( buf ) - 1, &sym, NULL );
    buf[ QMAX( 0, len ) ] = '\0';
    switch( sym )
        {
        case XK_Escape:
            if( !filter.isEmpty())
                {
                filter = QString::null;
                updateFilter();
                }
            else
                hide();
            return true;
        case XK_Return:
        case XK_KP_Enter:
            if( selected == NULL || !selected->matches )
                select( firstMatch());
            hide( selected != NULL ? selected->client : NULL );
            return true;
        case XK_Left:
            moveSelection( -1, 0 );
            return true;
        case XK_Right:
            moveSelection( 1, 0 );
            return true;
        case XK_Up:
            moveSelection( 0, -1 );
            return true;
        case XK_Down:
            moveSelection( 0, 1 );
            return true;
        case XK_Tab:
        case XK_ISO_Left_Tab:
            {
            // in the order of the layout
            QValueVector< OverviewItem* > order;
            for( OverviewItem* item = items.first();
                 item != NULL;
                 item = items.next())
                if( item->in_layout && item->matches )
                    order.append( item );
            if( order.isEmpty())
                return true;
            for( unsigned int i = 1;
                 i < order.size();
                 ++i )
                for( unsigned int j = i;
                     j > 0;
                     --j )
                    {
                    OverviewItem* a = order[ j - 1 ];
                    OverviewItem* b = order[ j ];
                    bool less = b->screen < a->screen || ( b->screen == a->screen
                        && ( b->slot.y() < a->slot.y() || ( b->slot.y() == a->slot.y() && b->slot.x() < a->slot.x())));
                    if( !less )
                        break;
                    order[ j - 1 ] = b;
                    order[ j ] = a;
                    }
            int idx = -1;
            for( unsigned int i = 0;
                 i < order.size();
                 ++i )
                if( order[ i ] == selected )
                    idx = i;
            bool back = sym == XK_ISO_Left_Tab || ( e->state & ShiftMask );
            if( idx < 0 )
                idx = back ? order.size() - 1 : 0;
            else
                idx = ( idx + ( back ? order.size() - 1 : 1 )) % order.size();
            select( order[ idx ] );
            return true;
            }
        case XK_BackSpace:
            if( !filter.isEmpty())
                {
                filter.truncate( filter.length() - 1 );
                updateFilter();
                }
            return true;
        default:
            break;
        }
    if( len > 0 && !( e->state & ( ControlMask | Mod1Mask | Mod4Mask )))
        {
        QString text = QString::fromLocal8Bit( buf );
        bool printable = !text.isEmpty();
        for( unsigned int i = 0;
             i < text.length();
             ++i )
            if( !text[ i ].isPrint())
                printable = false;
        if( printable )
            {
            filter += text;
            updateFilter();
            }
        }
    return true;
    }

bool Overview::handleButton( XButtonEvent* e )
    {
    if( state == Hiding || state == Finishing )
        return true;
    QPoint p( e->x_root, e->y_root );
    if( e->type == ButtonPress )
        {
        if( e->button == Button1 && pressed_button == 0 )
            {
            pressed = itemAt( p );
            pressed_button = e->button;
            }
        return true;
        }
    if( e->button != Button1 || pressed_button != (int) e->button )
        return true;
    pressed_button = 0;
    OverviewItem* item = itemAt( p );
    if( item == pressed )
        hide( item != NULL ? item->client : NULL );
    pressed = NULL;
    return true;
    }

void Overview::handleMotion( const QPoint& p )
    {
    if( state == Hiding || state == Finishing )
        return;
    // the hot corner again closes it, after the pointer left it
    if( !corners.isEmpty())
        {
        if( cornerHit( p ))
            {
            if( corner_armed )
                {
                corner_armed = false;
                hide();
                return;
                }
            }
        else
            {
            bool near = false;
            for( QValueList< QPoint >::ConstIterator it = corner_points.begin();
                 it != corner_points.end();
                 ++it )
                if(( p - *it ).manhattanLength() < 3 )
                    near = true;
            if( !near )
                corner_armed = true;
            }
        }
    OverviewItem* item = itemAt( p );
    if( item != NULL )
        select( item );
    }

bool Overview::x11Event( XEvent* e )
    {
    if(( e->type == EnterNotify || e->type == LeaveNotify ) && corners.contains( e->xcrossing.window ))
        {
        if( e->type == LeaveNotify )
            {
            corner_timer.stop();
            corner_armed = true;
            return true;
            }
        if( state != Hidden || !corner_armed || e->xcrossing.mode != NotifyNormal
            || !cornerAllowed( e->xcrossing.state ))
            return true;
        if( options->overviewHotCornerDelay > 0 )
            corner_timer.start( options->overviewHotCornerDelay, true );
        else
            {
            corner_armed = false;
            show();
            }
        return true;
        }
    if( state == Hidden )
        return false;
#ifdef OVERVIEW_COMPOSITE
    if( composite && e->type == damage_event + XDamageNotify )
        return handleDamage( e );
#endif
#ifdef OVERVIEW_PRESENT
    if( e->type == GenericEvent && e->xgeneric.extension == present_opcode )
        return handlePresent( e );
#endif
    switch( e->type )
        {
        case KeyPress:
            return handleKey( &e->xkey );
        case KeyRelease:
            return true;
        case ButtonPress:
        case ButtonRelease:
            return handleButton( &e->xbutton );
        case MotionNotify:
            {
            // only the latest position matters
            XEvent next;
            while( XCheckTypedEvent( dpy, MotionNotify, &next ))
                e = &next;
            handleMotion( QPoint( e->xmotion.x_root, e->xmotion.y_root ));
            return true;
            }
        default:
            break;
        }
    return false;
    }

#else // HAVE_XRENDER

// no XRender at build time: no overview

Overview::Overview( Workspace* w )
    : ws( w ), dpy( qt_xdisplay()), root( qt_xrootwin()), state( Hidden ), r( NULL )
    {
    }

Overview::~Overview()
    {
    }

void Overview::toggle() {}
void Overview::show() {}
void Overview::hide( Client* ) {}
bool Overview::isActive() const { return false; }
bool Overview::x11Event( XEvent* ) { return false; }
void Overview::reconfigure() {}
void Overview::setShortcut( const KShortcut& ) {}
void Overview::clientRemoved( Client* ) {}
void Overview::abort() {}
void Overview::frameTimeout() {}
void Overview::settleTimeout() {}
void Overview::cornerTimeout() {}
void Overview::presentWatchdog() {}

#endif // HAVE_XRENDER

} // namespace

#include "overview.moc"
