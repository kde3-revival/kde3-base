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
#include "compositing.h"
#include "effects.h"

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
#if defined( OVERVIEW_COMPOSITE ) && defined( KWIN_PANORAMIX_CHECK )
#define OVERVIEW_PANORAMIX_CHECK
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrandr.h>
#endif
#if defined( OVERVIEW_COMPOSITE ) && defined( KWIN_OVERVIEW_XI2 )
#define OVERVIEW_XI2
#include <X11/XKBlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xrandr.h>
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

using namespace Compositing;

namespace
{

// more extension state (the rest in compositing.h), checked once
// XInput 2.1 raw key events (also while another client has a grab)
bool have_xi_raw = false;
// XInput 2.3 barrier events and XFixes 5 pointer barriers
bool have_barriers = false;
// RandR 1.5 monitors
bool have_monitors = false;
#ifdef OVERVIEW_XI2
int xi_opcode = 0;
int randr_event = 0;
#endif

// the duration of the filter fades and of the title fade-in
const double FadeTime = 150.0;
// background dimming in the overview
const double DimAlpha = 0.45;
// the Super key opens the overview when released within this time (ms)
const unsigned int SuperTapTime = 600;
// the length of the hot corner strip of the primary monitor along an
// edge shared with another monitor (and of its pointer barrier)
const int CornerStrip = 32;
// layout metrics
const int Spacing = 28;
const int TitleGap = 10;
const int MipLevels = 4; // 1, 1/2, 1/4, 1/8
// a dragged thumbnail shrinks to at most this size (like GNOME), in this time
const double DragSize = 256.0;
const double DragShrinkTime = 200.0;
// the drag starts after the pointer moved at least this far
const int DragThreshold = 6;
// the close button: its diameter, its center inside the top right corner
const int CloseSize = 28;
const int CloseInset = 4;
// the corner ripples (GNOME's timing, shortened): delay, duration (ms),
// start and final scale of RippleSize, start opacity
const double RippleSize = 52.0;
const struct { double delay, duration, scale0, scale1, opacity; } Ripples[ 3 ] =
    {{ 0, 500, 0.25, 1.5, 1.0 }, { 30, 600, 0.0, 1.25, 0.7 }, { 210, 600, 0.0, 1.0, 0.3 }};
const double RippleTime = 810.0;

// the mip level that is downscaled by at most a half
int mipLevel( double scale )
    {
    int level = 0;
    while( level < MipLevels - 1 && scale * ( 1 << ( level + 1 )) <= 1.0 )
        ++level;
    return level;
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
              title_picture( None ), icon_pixmap( None ), icon_picture( None ), seen( true ),
              snapshot( false ), close_from( 0 ), close_to( 0 ), close_start( 0 ), close_fade( 0 )
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
        bool snapshot; // a copy of the screen, not redirected
        double close_from, close_to, close_start, close_fade; // the close button fading
    };

class OverviewRenderer
    {
    public:
        OverviewRenderer()
            : width( 0 ), height( 0 ), format( NULL ),
              snapshot( None ), snapshot_picture( None ), filter_picture( None )
            {
            close_pixmap[ 0 ] = close_pixmap[ 1 ] = None;
            close_picture[ 0 ] = close_picture[ 1 ] = None;
            }
        int width, height;
        XRenderPictFormat* format;
        Pixmap snapshot; // the screen, without Composite
        Picture snapshot_picture;
        QPixmap filter_pixmap;
        Picture filter_picture;
        // anti-aliased rounded rectangles drawn from cached masks: the corners
        // of a rectangle with this radius (Picture full), its middle column
        // and row (col, row, padded): no polygons in every frame (glamor
        // rasterizes those on the CPU and uploads the mask each time)
        struct RoundMask
            {
            RoundMask() : ri( 0 ), full_pixmap( None ), full( None ), col_pixmap( None ), col( None ),
                row_pixmap( None ), row( None ) {}
            int ri; // the corner cells, ceil( radius ) + 1
            Pixmap full_pixmap;
            Picture full;
            Pixmap col_pixmap;
            Picture col;
            Pixmap row_pixmap;
            Picture row;
            };
        QMap< int, RoundMask > round_masks; // by radius * 2
        // the close button, normal and hovered (ARGB, made once)
        Pixmap close_pixmap[ 2 ];
        Picture close_picture[ 2 ];
        QFont font;
        int title_height;
        QColor highlight;
    };


/*
 An anti-aliased rounded rectangle at any subpixel position from cached
 masks, in nine cells that cover each pixel once: the corners sampled
 (translated, bilinear) from a small mask of a rounded rectangle with that
 radius, the edges from its middle column and row (padded along the edge),
 the middle filled.  hollow leaves the middle out (under an opaque
 window).  Tiny rectangles are polygons.
*/
static void fillRounded( Display* dpy, OverviewRenderer* r, Picture dst, const RectF& rect, double radius,
    const QColor& c, double alpha, bool hollow = false )
    {
    if( alpha <= 1.0 / 512 || rect.w <= 0 || rect.h <= 0 )
        return;
    radius = QMIN( radius, QMIN( rect.w, rect.h ) / 2 );
    const int key = (int) floor( radius * 2 );
    radius = key / 2.0;
    const int ri = (int) ceil( radius ) + 1;
    const int x0 = (int) floor( rect.x );
    const int y0 = (int) floor( rect.y );
    const int x1 = (int) ceil( rect.x + rect.w );
    const int y1 = (int) ceil( rect.y + rect.h );
    if( x1 - x0 < 4 || y1 - y0 < 4 )
        {
        roundedRect( dpy, dst, rect, radius, c, alpha );
        return;
        }
    if( !r->round_masks.contains( key ))
        { // the rounded rectangle at 2,2 with the size 2 * ri, in a transparent margin
        OverviewRenderer::RoundMask m;
        m.ri = ri;
        const int n = 2 * ri + 4;
        XRenderPictFormat* a8 = XRenderFindStandardFormat( dpy, PictStandardA8 );
        Window root = DefaultRootWindow( dpy );
        XRenderPictureAttributes pa;
        pa.repeat = RepeatPad;
        m.full_pixmap = XCreatePixmap( dpy, root, n, n, 8 );
        m.full = XRenderCreatePicture( dpy, m.full_pixmap, a8, CPRepeat, &pa );
        m.col_pixmap = XCreatePixmap( dpy, root, 1, n, 8 );
        m.col = XRenderCreatePicture( dpy, m.col_pixmap, a8, CPRepeat, &pa );
        m.row_pixmap = XCreatePixmap( dpy, root, n, 1, 8 );
        m.row = XRenderCreatePicture( dpy, m.row_pixmap, a8, CPRepeat, &pa );
        XRenderColor clear = { 0, 0, 0, 0 };
        XRenderFillRectangle( dpy, PictOpSrc, m.full, &clear, 0, 0, n, n );
        roundedRect( dpy, m.full, RectF( 2, 2, 2 * ri, 2 * ri ), radius, Qt::white, 1.0 );
        XRenderComposite( dpy, PictOpSrc, m.full, None, m.col, 2 + ri, 0, 0, 0, 0, 0, 1, n );
        XRenderComposite( dpy, PictOpSrc, m.full, None, m.row, 0, 2 + ri, 0, 0, 0, 0, n, 1 );
        XRenderSetPictureFilter( dpy, m.full, FilterBilinear, NULL, 0 );
        XRenderSetPictureFilter( dpy, m.col, FilterBilinear, NULL, 0 );
        XRenderSetPictureFilter( dpy, m.row, FilterBilinear, NULL, 0 );
        r->round_masks[ key ] = m;
        }
    const OverviewRenderer::RoundMask& m = r->round_masks[ key ];
    // where a pixel of the cells samples the mask: x + tx
    const double tx[ 3 ] = { 2 - rect.x, 0, 2 + 2 * ri - ( rect.x + rect.w ) };
    const double ty[ 3 ] = { 2 - rect.y, 0, 2 + 2 * ri - ( rect.y + rect.h ) };
    // (no middle when the corners meet: a pill, a circle)
    const int xm = ( x0 + x1 ) / 2;
    const int ym = ( y0 + y1 ) / 2;
    const int cx[ 4 ] = { x0, QMIN( x0 + ri, xm ), QMAX( x1 - ri, xm ), x1 };
    const int cy[ 4 ] = { y0, QMIN( y0 + ri, ym ), QMAX( y1 - ri, ym ), y1 };
    Picture src = solidPicture( dpy, c, alpha );
    for( int j = 0;
         j < 3;
         ++j )
        for( int i = 0;
             i < 3;
             ++i )
            {
            Picture mask = i == 1 ? ( j == 1 ? None : m.col ) : ( j == 1 ? m.row : m.full );
            if(( i == 1 && j == 1 && hollow ) || cx[ i + 1 ] <= cx[ i ] || cy[ j + 1 ] <= cy[ j ] )
                continue;
            if( mask != None )
                setTransform( dpy, mask, 1, 1, tx[ i ], ty[ j ] );
            XRenderComposite( dpy, PictOpOver, src, mask, dst, 0, 0, cx[ i ], cy[ j ],
                cx[ i ], cy[ j ], cx[ i + 1 ] - cx[ i ], cy[ j + 1 ] - cy[ j ] );
            }
    XRenderFreePicture( dpy, src );
    }

Overview::Overview( Workspace* w )
    : ws( w ),
      dpy( qt_xdisplay()),
      root( qt_xrootwin()),
      state( Hidden ),
      composite( false ),
      selected( NULL ),
      pressed( NULL ),
      pressed_button( 0 ),
      press_consumed( false ),
      dragged( NULL ),
      drag_fx( 0 ),
      drag_fy( 0 ),
      drag_w( 0 ),
      drag_h( 0 ),
      drag_start( 0 ),
      drag_target( -1 ),
      drag_target_start( 0 ),
      hovered( NULL ),
      close_hover( false ),
      pressed_close( NULL ),
      dialog( NULL ),
      trans_start( 0 ),
      trans_duration( 0 ),
      trans_from( 0 ),
      trans_to( 0 ),
      progress( 0 ),
      title_start( 0 ),
      title_alpha( 0 ),
      desktop_dim( 0 ),
      overlay( None ),
      own_overlay( false ),
      cursor( None ),
      grabbed( false ),
      keep_redirected( false ),
      temp_redirected( false ),
      r( NULL ),
      presenter( NULL ),
      settle_start( 0 ),
      last_damage( 0 ),
      last_frame( 0 ),
      frame_wanted( false ),
      barriers_enabled( true ),
      corner_armed( true ),
      ripple_start( 0 ),
      ripple_window( None ),
      ripple_bg( None ),
      ripple_back( None ),
      super_down( false ),
      super_alone( false ),
      super_time( 0 ),
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
    connect( &settle_timer, SIGNAL( timeout()), SLOT( settleTimeout()));
    connect( &corner_timer, SIGNAL( timeout()), SLOT( cornerTimeout()));
    connect( &monitors_timer, SIGNAL( timeout()), SLOT( monitorsChanged()));
    connect( &dialog_timer, SIGNAL( timeout()), SLOT( dialogTimeout()));
    connect( &ripple_timer, SIGNAL( timeout()), SLOT( rippleTimeout()));
    checkExtensions();
    presenter = new Presenter( dpy );
    connect( presenter, SIGNAL( completed( bool, int )), SLOT( presentCompleted( bool, int )));
    connect( presenter, SIGNAL( idle()), SLOT( presentIdle()));
#ifdef OVERVIEW_XI2
    if( have_monitors ) // monitors arranged anew without a new screen size
        XRRSelectInput( dpy, root, RRScreenChangeNotifyMask | RRCrtcChangeNotifyMask | RROutputChangeNotifyMask );
#endif
    reconfigure();
    }

Overview::~Overview()
    {
    abort();
    stopRipple();
    destroyCorners();
    setKeepRedirected( false );
    delete presenter;
    }

double Overview::now()
    {
    return Compositing::now();
    }

bool Overview::checkExtensions()
    {
    static bool checked = false;
    if( checked )
        return have_render;
    checked = true;
    Compositing::checkExtensions( dpy );
    const bool panoramix = composite_panoramix;
#ifdef OVERVIEW_XI2
    if( have_render )
        {
        int xi_event, xi_error;
        int xi_major = 2, xi_minor = 3;
        if( XQueryExtension( dpy, "XInputExtension", &xi_opcode, &xi_event, &xi_error )
            && XIQueryVersion( dpy, &xi_major, &xi_minor ) == Success && xi_major >= 2 )
            {
            have_xi_raw = xi_major > 2 || xi_minor >= 1;
            int fixes_event, fixes_error;
            int fixes_major = 0, fixes_minor = 0;
            have_barriers = ( xi_major > 2 || xi_minor >= 3 )
                && XFixesQueryExtension( dpy, &fixes_event, &fixes_error )
                && XFixesQueryVersion( dpy, &fixes_major, &fixes_minor ) && fixes_major >= 5;
            }
        int rr_error;
        int rr_major = 0, rr_minor = 0;
        have_monitors = XRRQueryExtension( dpy, &randr_event, &rr_error )
            && XRRQueryVersion( dpy, &rr_major, &rr_minor ) && ( rr_major > 1 || rr_minor >= 5 );
        }
#endif
    if( !have_render )
        kdWarning( 1212 ) << "Window overview disabled: the X server lacks RENDER 0.10" << endl;
    else if( !have_composite && !panoramix )
        kdWarning( 1212 ) << "Window overview without window contents: no Composite 0.3/Damage/XFixes 2" << endl;
    if( debug )
        kdWarning( 1212 ) << "overview: render " << have_render << " composite " << have_composite
            << " present " << have_present << " xi2 raw " << have_xi_raw << " barriers " << have_barriers
            << " monitors " << have_monitors << endl;
    return have_render;
    }

bool Overview::isActive() const
    {
    return state != Hidden;
    }

bool Overview::isRedirected( Window frame ) const
    {
    return state == Hidden && !temp_redirected && redirected.contains( frame );
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
    selectXI2();
    if( state == Hidden )
        setKeepRedirected( options->overviewKeepRedirected );
    // else when the overview closes
    }

/*
 Redirecting a window gives it a new (empty) pixmap and unredirecting
 drops it: both expose the whole window, i.e. every application would
 repaint everything when the overview opens and again when it closes.
 With KeepRedirected (the default) the frame of every managed window is
 redirected (Automatic) when it is created, before it is mapped, and stays
 so; the overview itself then never makes the applications see anything
 but the focus change of the window that gets activated.  The cost is one
 pixmap per mapped frame, and the server copies the drawing to the screen.
 Full screen windows are not redirected (page flipping, no extra copy);
 the overview uses a copy of the screen for them and for override-redirect
 windows (menus, tooltips), which are not redirected either.
*/
void Overview::setKeepRedirected( bool keep )
    {
#ifdef OVERVIEW_COMPOSITE
    if( !have_composite || keep == keep_redirected )
        return;
    keep_redirected = keep;
    updateRedirection();
#else
    Q_UNUSED( keep );
#endif
    }

void Overview::frameCreated( Window frame )
    {
#ifdef OVERVIEW_COMPOSITE
    if( keep_redirected )
        {
        XCompositeRedirectWindow( dpy, frame, CompositeRedirectAutomatic );
        redirected[ frame ] = true;
        }
#else
    Q_UNUSED( frame );
#endif
    }

// which frames should be redirected (not while shown: the overview may
// need them, it is done again when it closes)
void Overview::updateRedirection()
    {
#ifdef OVERVIEW_COMPOSITE
    if( !have_composite || state != Hidden )
        return;
    // (frames of windows being managed are not in the stacking order yet;
    // they stay as frameCreated() left them)
    const ClientList& clients = ws->stackingOrder();
    for( ClientList::ConstIterator it = clients.begin();
         it != clients.end();
         ++it )
        {
        Window frame = (*it)->frameId();
        bool want = keep_redirected && !(*it)->isFullScreen();
        bool have = redirected.contains( frame );
        if( want && !have )
            {
            XCompositeRedirectWindow( dpy, frame, CompositeRedirectAutomatic );
            redirected[ frame ] = true;
            }
        else if( !want && have )
            {
            XCompositeUnredirectWindow( dpy, frame, CompositeRedirectAutomatic );
            redirected.remove( frame );
            }
        if( debug && want != have )
            kdWarning( 1212 ) << "overview: " << ( want ? "redirected " : "unredirected " )
                << (*it)->caption() << endl;
        }
#endif
    }

void Overview::stackingChanged()
    {
    updateRedirection();
    updateCorners();
    }

//****************************************
// hot corners
//****************************************

/*
 A corner is a 1x1 InputOnly window in the chosen corner of every monitor
 where nothing is beyond it, horizontally or vertically: the pointer stops
 there by itself.  The top left corner of the right one of two monitors
 side by side is no such corner, the pointer passes on to the left monitor;
 for the primary monitor (where GNOME has its hot corner) it is a hot
 corner nevertheless: a strip of CornerStrip pixels along the shared edge,
 with a pointer barrier (XFixes 5) on that edge stopping the pointer there,
 like GNOME does.  Below the strip the pointer passes freely; the barrier
 lets it through when the pointer is grabbed by another client (something
 being dragged), when the corner is off (a full screen window) and after
 the corner fired until the pointer leaves it (pushing on crosses), and
 KWin's own moves and resizes switch the barriers off.  GNOME measures the pressure against the barrier;
 here the pointer is simply held in the strip and the usual corner delay
 applies, so that the hot corner of the primary monitor works like all the
 others.  The monitors are RandR's (with the primary one), else Qt's.
*/
void Overview::createCorners()
    {
    QValueList< QRect > screens;
    int primary = -1;
#ifdef OVERVIEW_XI2
    if( have_monitors )
        {
        int count = 0;
        XRRMonitorInfo* info = XRRGetMonitors( dpy, root, True, &count );
        for( int i = 0;
             info != NULL && i < count;
             ++i )
            {
            screens.append( QRect( info[ i ].x, info[ i ].y, info[ i ].width, info[ i ].height ));
            if( info[ i ].primary )
                primary = i;
            }
        if( info != NULL )
            XRRFreeMonitors( info );
        }
#endif
    if( screens.isEmpty())
        {
        QDesktopWidget* desktop = QApplication::desktop();
        for( int i = 0;
             i < desktop->numScreens();
             ++i )
            screens.append( desktop->screenGeometry( i ));
        primary = desktop->primaryScreen();
        }
    if( primary < 0 || primary >= int( screens.count()))
        primary = 0; // no primary output: the first monitor, like RandR's Xinerama
    const int pos = options->overviewHotCornerPosition;
    const bool right = pos == 1 || pos == 3;
    const bool bottom = pos == 2 || pos == 3;
    for( int i = 0;
         i < int( screens.count());
         ++i )
        {
        QRect g = screens[ i ];
        QPoint p( right ? g.right() : g.left(), bottom ? g.bottom() : g.top());
        // only real corners: nothing beyond them horizontally or vertically
        QPoint outx( right ? p.x() + 1 : p.x() - 1, p.y());
        QPoint outy( p.x(), bottom ? p.y() + 1 : p.y() - 1 );
        bool beyondx = false;
        bool beyondy = false;
        for( int j = 0;
             j < int( screens.count());
             ++j )
            {
            if( screens[ j ].contains( outx ))
                beyondx = true;
            if( screens[ j ].contains( outy ))
                beyondy = true;
            }
        if( debug )
            kdWarning( 1212 ) << "overview: monitor " << i << " " << g.x() << "," << g.y() << " "
                << g.width() << "x" << g.height() << ( i == primary ? " primary" : "" ) << ", corner "
                << p.x() << "," << p.y() << ( beyondx ? " monitor beside it" : "" )
                << ( beyondy ? " monitor above/below it" : "" ) << endl;
        if( !beyondx && !beyondy )
            {
            addCorner( QRect( p, QSize( 1, 1 )), p );
            continue;
            }
        // (a strip without a barrier would be crossed, and trigger, when
        // moving slowly to the other monitor)
        if( i != primary || !have_barriers )
            continue;
        const int strip = QMIN( CornerStrip, QMIN( g.width(), g.height()));
        if( beyondx )
            { // along the vertical edge, the barrier on the line between the monitors
            QRect area( p.x(), bottom ? p.y() - strip + 1 : p.y(), 1, strip );
            addCorner( area, p );
#ifdef OVERVIEW_XI2
            BarrierLine line;
            line.x1 = line.x2 = right ? p.x() + 1 : p.x();
            line.y1 = area.top();
            line.y2 = area.bottom();
            line.directions = right ? BarrierNegativeX : BarrierPositiveX;
            barrier_lines.append( line );
#endif
            }
        if( beyondy )
            {
            QRect area( right ? p.x() - strip + 1 : p.x(), p.y(), strip, 1 );
            addCorner( area, p );
#ifdef OVERVIEW_XI2
            BarrierLine line;
            line.x1 = area.left();
            line.x2 = area.right();
            line.y1 = line.y2 = bottom ? p.y() + 1 : p.y();
            line.directions = bottom ? BarrierNegativeY : BarrierPositiveY;
            barrier_lines.append( line );
#endif
            }
        }
    updateCorners();
    setBarriersEnabled( barriers_enabled );
    }

void Overview::setBarriersEnabled( bool enable )
    {
    barriers_enabled = enable;
#ifdef OVERVIEW_XI2
    if( !have_barriers )
        return;
    if( enable && barriers.isEmpty())
        {
        for( QValueList< BarrierLine >::ConstIterator it = barrier_lines.begin();
             it != barrier_lines.end();
             ++it )
            {
            barriers.append( XFixesCreatePointerBarrier( dpy, root, (*it).x1, (*it).y1, (*it).x2, (*it).y2,
                (*it).directions, 0, NULL ));
            if( debug )
                kdWarning( 1212 ) << "overview: barrier " << (*it).x1 << "," << (*it).y1 << " - "
                    << (*it).x2 << "," << (*it).y2 << endl;
            }
        }
    else if( !enable )
        {
        for( QValueList< XID >::ConstIterator it = barriers.begin();
             it != barriers.end();
             ++it )
            XFixesDestroyPointerBarrier( dpy, *it );
        barriers.clear();
        }
#endif
    }

void Overview::addCorner( const QRect& area, const QPoint& point )
    {
    if( corner_areas.contains( area ))
        return;
    corner_points.append( point );
    XSetWindowAttributes attr;
    attr.override_redirect = True;
    // a click in the corner opens the overview too (it would be lost anyway)
    attr.event_mask = EnterWindowMask | LeaveWindowMask | ButtonPressMask;
    Window w = XCreateWindow( dpy, root, area.x(), area.y(), area.width(), area.height(), 0,
        CopyFromParent, InputOnly, CopyFromParent, CWOverrideRedirect | CWEventMask, &attr );
    corners.append( w );
    corner_areas.append( area );
    corner_mapped.append( false );
    if( debug )
        kdWarning( 1212 ) << "overview: hot corner " << area.x() << "," << area.y() << " "
            << area.width() << "x" << area.height() << endl;
    }

// no corner above a full screen window: it would eat its clicks there
void Overview::updateCorners()
    {
    const ClientList& clients = ws->stackingOrder();
    bool mapped_now = false;
    QValueList< bool >::Iterator mapped = corner_mapped.begin();
    QValueList< QRect >::ConstIterator area = corner_areas.begin();
    for( QValueList< Window >::ConstIterator it = corners.begin();
         it != corners.end();
         ++it, ++mapped, ++area )
        {
        bool covered = false;
        for( ClientList::ConstIterator c = clients.begin();
             c != clients.end() && !covered;
             ++c )
            if( (*c)->isFullScreen() && (*c)->isOnCurrentDesktop() && (*c)->isShown( true )
                && (*c)->geometry().intersects( *area ))
                covered = true;
        // mapped again where it was: not above override-redirect windows
        // (menus) that appeared since (above the electric borders and the
        // clients it is, from its creation and raiseCorners())
        if( covered && *mapped )
            XUnmapWindow( dpy, *it );
        else if( !covered && !*mapped )
            {
            XMapWindow( dpy, *it );
            mapped_now = true;
            }
        *mapped = !covered;
        }
    // a corner mapped below the pointer is entered without the pointer
    // moving there: it must leave first
    if( mapped_now )
        {
        Window root_return, child;
        int rx, ry, wx, wy;
        unsigned int mask;
        if( XQueryPointer( dpy, root, &root_return, &child, &rx, &ry, &wx, &wy, &mask )
            && cornerHit( QPoint( rx, ry )))
            corner_armed = false;
        }
    }

void Overview::raiseCorners()
    {
    QValueList< bool >::ConstIterator mapped = corner_mapped.begin();
    for( QValueList< Window >::ConstIterator it = corners.begin();
         it != corners.end();
         ++it, ++mapped )
        if( *mapped )
            XRaiseWindow( dpy, *it );
    }

void Overview::destroyCorners()
    {
    corner_timer.stop();
    for( QValueList< Window >::ConstIterator it = corners.begin();
         it != corners.end();
         ++it )
        XDestroyWindow( dpy, *it );
    corners.clear();
    corner_areas.clear();
    corner_points.clear();
    corner_mapped.clear();
    // the barriers go, enabled or not (stays as it is for new ones)
    const bool enabled = barriers_enabled;
    setBarriersEnabled( false );
    barriers_enabled = enabled;
    barrier_lines.clear();
    }

// the monitors were arranged anew (RandR)
void Overview::monitorsChanged()
    {
    if( debug )
        kdWarning( 1212 ) << "overview: the monitors changed" << endl;
    abort(); // its layout is per monitor
    destroyCorners();
    if( have_render && options->overviewHotCorner )
        createCorners();
    selectXI2();
    }

bool Overview::cornerHit( const QPoint& p ) const
    {
    for( QValueList< QRect >::ConstIterator it = corner_areas.begin();
         it != corner_areas.end();
         ++it )
        if( (*it).contains( p ))
            return true;
    return false;
    }

bool Overview::cornerAllowed( const QPoint& corner, unsigned int button_state ) const
    {
    if( button_state & ( Button1Mask | Button2Mask | Button3Mask | Button4Mask | Button5Mask ))
        return false; // dragging something
    if( blocked())
        return false;
    // a full screen window on that monitor (the corner is unmapped then anyway)
    const ClientList& clients = ws->stackingOrder();
    for( ClientList::ConstIterator it = clients.begin();
         it != clients.end();
         ++it )
        if( (*it)->isFullScreen() && (*it)->isOnCurrentDesktop() && (*it)->isShown( true )
            && (*it)->geometry().contains( corner ))
            return false;
    return true;
    }

// KWin's own popup menus, the Alt+Tab box and interactive moves have
// their grabs; taking them over would orphan them
bool Overview::blocked() const
    {
    return QApplication::activePopupWidget() != NULL || ws->tabBoxActive()
        || ws->moveResizeClient() != NULL;
    }

void Overview::cornerTimeout()
    {
    Window root_return, child;
    int rx, ry, wx, wy;
    unsigned int mask;
    if( !XQueryPointer( dpy, root, &root_return, &child, &rx, &ry, &wx, &wy, &mask ))
        return;
    if( debug )
        kdWarning( 1212 ) << "overview: corner delay over, pointer " << rx << "," << ry << " hit "
            << cornerHit( QPoint( rx, ry )) << " allowed " << cornerAllowed( QPoint( rx, ry ), mask )
            << " state " << state << endl;
    if( cornerHit( QPoint( rx, ry )) && cornerAllowed( QPoint( rx, ry ), mask ) && state == Hidden )
        {
        corner_armed = false;
        startRipple( QPoint( rx, ry ));
        show();
        if( state == Hidden )
            stopRipple(); // not shown
        }
    }

//****************************************
// the corner ripples
//****************************************

void Overview::startRipple( const QPoint& p )
    {
    stopRipple();
    if( !options->overviewHotCornerRipple )
        return;
    QValueList< QPoint >::ConstIterator point = corner_points.begin();
    for( QValueList< QRect >::ConstIterator it = corner_areas.begin();
         it != corner_areas.end();
         ++it, ++point )
        if( (*it).contains( p ))
            {
            ripple_point = *point;
            ripple_start = now();
            if( debug )
                kdWarning( 1212 ) << "overview: ripples from " << ripple_point.x() << "," << ripple_point.y() << endl;
            return;
            }
    }

// where the ripples are drawn
QRect Overview::rippleBox() const
    {
    const int pos = options->overviewHotCornerPosition;
    const int size = (int) ceil( RippleSize * 1.5 ) + 2;
    return QRect( pos == 1 || pos == 3 ? ripple_point.x() - size + 1 : ripple_point.x(),
        pos == 2 || pos == 3 ? ripple_point.y() - size + 1 : ripple_point.y(), size, size );
    }

// the ripples at time t, drawn offset by ox, oy; false when they are over
bool Overview::drawRipples( XID dst, double t, int ox, int oy )
    {
    if( ripple_start <= 0 || t - ripple_start >= RippleTime )
        return false;
    const int pos = options->overviewHotCornerPosition;
    // the corner point of the pixel grid, the quarter opening into the monitor
    const double cx = ripple_point.x() + ( pos == 1 || pos == 3 ? 1 : 0 ) - ox;
    const double cy = ripple_point.y() + ( pos == 2 || pos == 3 ? 1 : 0 ) - oy;
    const double dx = pos == 1 || pos == 3 ? -1 : 1;
    const double dy = pos == 2 || pos == 3 ? -1 : 1;
    XRenderPictFormat* a8 = XRenderFindStandardFormat( dpy, PictStandardA8 );
    for( int i = 0;
         i < 3;
         ++i )
        {
        double f = ( t - ripple_start - Ripples[ i ].delay ) / Ripples[ i ].duration;
        if( f < 0 || f >= 1 )
            continue;
        const double radius = RippleSize * mix( Ripples[ i ].scale0, Ripples[ i ].scale1, easeOutQuad( f ));
        const double opacity = Ripples[ i ].opacity * ( 1 - f );
        if( radius < 1 || opacity <= 1.0 / 512 )
            continue;
        // a quarter disc, brighter at its rim
        const int steps = 12;
        XPointDouble pts[ 2 * ( steps + 1 ) + 1 ];
        pts[ 0 ].x = cx;
        pts[ 0 ].y = cy;
        for( int j = 0;
             j <= steps;
             ++j )
            {
            double a = M_PI / 2 * j / steps;
            pts[ j + 1 ].x = cx + dx * radius * cos( a );
            pts[ j + 1 ].y = cy + dy * radius * sin( a );
            }
        Picture fill = solidPicture( dpy, Qt::white, 0.22 * opacity );
        XRenderCompositeDoublePoly( dpy, PictOpOver, fill, dst, a8, 0, 0, 0, 0, pts, steps + 2, WindingRule );
        XRenderFreePicture( dpy, fill );
        const double inner = QMAX( 0.0, radius - 2.0 );
        int n = 0;
        for( int j = 0;
             j <= steps;
             ++j, ++n )
            {
            double a = M_PI / 2 * j / steps;
            pts[ n ].x = cx + dx * radius * cos( a );
            pts[ n ].y = cy + dy * radius * sin( a );
            }
        for( int j = steps;
             j >= 0;
             --j, ++n )
            {
            double a = M_PI / 2 * j / steps;
            pts[ n ].x = cx + dx * inner * cos( a );
            pts[ n ].y = cy + dy * inner * sin( a );
            }
        Picture rim = solidPicture( dpy, Qt::white, 0.35 * opacity );
        XRenderCompositeDoublePoly( dpy, PictOpOver, rim, dst, a8, 0, 0, 0, 0, pts, n, WindingRule );
        XRenderFreePicture( dpy, rim );
        }
    return true;
    }

void Overview::saveRippleBackground( XID src )
    {
    if( ripple_bg != None || !keep_redirected || r == NULL )
        return; // below a window there the client would get exposed when it goes
    QRect box = rippleBox();
    // nor above windows that are not redirected (full screen ones)
    const ClientList& clients = ws->stackingOrder();
    for( ClientList::ConstIterator it = clients.begin();
         it != clients.end();
         ++it )
        if( (*it)->isOnCurrentDesktop() && (*it)->isShown( true ) && (*it)->geometry().intersects( box )
            && !redirected.contains( (*it)->frameId()))
            return;
    ripple_bg = XCreatePixmap( dpy, root, box.width(), box.height(), DefaultDepth( dpy, DefaultScreen( dpy )));
    Picture bg = XRenderCreatePicture( dpy, ripple_bg, r->format, 0, NULL );
    XRenderComposite( dpy, PictOpSrc, src, None, bg, box.x(), box.y(), 0, 0, 0, 0, box.width(), box.height());
    XRenderFreePicture( dpy, bg );
    }

// the overview goes, the ripples aren't over: on in a window of their own
void Overview::showRippleWindow()
    {
    if( debug )
        kdWarning( 1212 ) << "overview: ripples at " << now() - ripple_start << " ms, background "
            << ( ripple_bg != None ) << endl;
    if( ripple_bg == None || r == NULL || now() - ripple_start >= RippleTime )
        {
        stopRipple();
        return;
        }
    if( ripple_window != None )
        XDestroyWindow( dpy, ripple_window ); // (not expected)
    if( ripple_back != None )
        XFreePixmap( dpy, ripple_back );
    QRect box = rippleBox();
    XSetWindowAttributes attr;
    attr.override_redirect = True;
    attr.background_pixmap = None;
    ripple_window = XCreateWindow( dpy, root, box.x(), box.y(), box.width(), box.height(), 0, CopyFromParent,
        InputOutput, CopyFromParent, CWOverrideRedirect | CWBackPixmap, &attr );
    // no input: the pointer is in the corner
    XShapeCombineRectangles( dpy, ripple_window, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted );
    ripple_back = XCreatePixmap( dpy, root, box.width(), box.height(), DefaultDepth( dpy, DefaultScreen( dpy )));
    rippleTimeout(); // the contents first
    XMapRaised( dpy, ripple_window );
    rippleTimeout();
    ripple_timer.start( 16 );
    }

void Overview::rippleTimeout()
    {
    if( ripple_window == None )
        {
        ripple_timer.stop();
        return;
        }
    QRect box = rippleBox();
    XRenderPictFormat* format = XRenderFindVisualFormat( dpy, DefaultVisual( dpy, DefaultScreen( dpy )));
    Picture bg = XRenderCreatePicture( dpy, ripple_bg, format, 0, NULL );
    Picture back = XRenderCreatePicture( dpy, ripple_back, format, 0, NULL );
    XRenderComposite( dpy, PictOpSrc, bg, None, back, 0, 0, 0, 0, 0, 0, box.width(), box.height());
    bool more = drawRipples( back, now(), box.x(), box.y());
    XRenderFreePicture( dpy, bg );
    XRenderFreePicture( dpy, back );
    if( !more )
        {
        stopRipple();
        return;
        }
    GC gc = XCreateGC( dpy, ripple_window, 0, NULL );
    XCopyArea( dpy, ripple_back, ripple_window, gc, 0, 0, box.width(), box.height(), 0, 0 );
    XFreeGC( dpy, gc );
    XFlush( dpy );
    }

void Overview::stopRipple()
    {
    ripple_timer.stop();
    ripple_start = 0;
    if( ripple_window != None )
        XDestroyWindow( dpy, ripple_window );
    if( ripple_bg != None )
        XFreePixmap( dpy, ripple_bg );
    if( ripple_back != None )
        XFreePixmap( dpy, ripple_back );
    ripple_window = None;
    ripple_bg = ripple_back = None;
    XFlush( dpy );
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
        if( state == Finishing || r == NULL )
            return; // too late, the windows are being given back
        state = Showing;
        computeLayout();
        updateTitles();
        startTransition( 1.0 );
        return;
        }
    if( state != Hidden || blocked())
        {
        if( debug )
            kdWarning( 1212 ) << "overview: not shown, state " << state << " blocked " << blocked() << endl;
        return;
        }
    // the window animations draw the same windows (and the overview
    // changes their redirection meanwhile)
    if( ws->windowEffects() != NULL )
        ws->windowEffects()->abort();
    if( !grabInput())
        {
        kdWarning( 1212 ) << "overview: cannot grab the keyboard or the pointer" << endl;
        return;
        }
    // a Super press before (the overview shown otherwise) is no tap now
    super_alone = false;
    // the ripples of an earlier corner still in their window: over (a corner
    // that opens it now started new ones, without a window)
    if( ripple_window != None )
        stopRipple();
    // the pointer resting in a corner (entered while not allowed) must
    // leave it before the corner closes the overview
        {
        Window root_return, child;
        int rx, ry, wx, wy;
        unsigned int mask;
        if( XQueryPointer( dpy, root, &root_return, &child, &rx, &ry, &wx, &wy, &mask ))
            corner_armed = !cornerHit( QPoint( rx, ry ));
        }
    composite = have_composite;
    frame_count = 0;
    stat_render = stat_max_render = 0;
    stat_skipped = stat_gaps = 0;
    stat_first_frame = now();
    filter = QString::null;
    selected = pressed = dragged = hovered = pressed_close = NULL;
    close_hover = false;
    closing.clear();
    dialog = NULL;
    pressed_button = 0;
    press_consumed = false;
    progress = 0;
    title_alpha = 0;

    r = new OverviewRenderer;
    r->width = DisplayWidth( dpy, DefaultScreen( dpy ));
    r->height = DisplayHeight( dpy, DefaultScreen( dpy ));
    r->format = XRenderFindVisualFormat( dpy, DefaultVisual( dpy, DefaultScreen( dpy )));
    r->font = KGlobalSettings::generalFont();
    r->title_height = QFontMetrics( r->font ).height() + 8;
    r->highlight = KGlobalSettings::highlightColor();

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

    presenter->start( overlay, 0, 0, r->width, r->height, composite && have_present );

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
        if( !c->isOnCurrentDesktop())
            { // moved to another desktop meanwhile: switching can't be animated
            abort();
            ws->activateClient( c, true );
            return;
            }
        ws->activateClient( c, true );
        if( state == Hidden || r == NULL )
            return; // aborted from within activateClient()
        }
    // the new stacking order, windows that appeared meanwhile, new sizes
    buildItems( false );
    title_alpha = 0;
    title_start = 0;
    pressed = pressed_close = NULL;
    pressed_button = 0; // (its release may come while zooming in)
    press_consumed = false;
    setHover( NULL, false );
    if( dragged != NULL )
        { // zooms in from where it is
        placeDragged( now());
        dragged = NULL;
        drag_target = -1;
        press_consumed = true;
        if( grabbed )
            XChangeActivePointerGrab( dpy, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                cursor, CurrentTime );
        }
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
    presenter->stop();
    if( r->snapshot_picture != None )
        XRenderFreePicture( dpy, r->snapshot_picture );
    if( r->snapshot != None )
        XFreePixmap( dpy, r->snapshot );
    if( r->filter_picture != None )
        XRenderFreePicture( dpy, r->filter_picture );
    for( int i = 0;
         i < 2;
         ++i )
        {
        if( r->close_picture[ i ] != None )
            XRenderFreePicture( dpy, r->close_picture[ i ] );
        if( r->close_pixmap[ i ] != None )
            XFreePixmap( dpy, r->close_pixmap[ i ] );
        }
    for( QMap< int, OverviewRenderer::RoundMask >::ConstIterator it = r->round_masks.begin();
         it != r->round_masks.end();
         ++it )
        {
        XRenderFreePicture( dpy, (*it).full );
        XRenderFreePicture( dpy, (*it).col );
        XRenderFreePicture( dpy, (*it).row );
        XFreePixmap( dpy, (*it).full_pixmap );
        XFreePixmap( dpy, (*it).col_pixmap );
        XFreePixmap( dpy, (*it).row_pixmap );
        }
    if( ripple_start > 0 )
        showRippleWindow(); // (or no more ripples)
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
    selected = pressed = dragged = hovered = pressed_close = NULL;
    close_hover = false;
    closing.clear();
    dialog = NULL;
    dialog_timer.stop();
    pressed_button = 0;
    press_consumed = false;
    filter = QString::null;
    frame_wanted = false;
    // don't open again right away if the pointer is still in the corner
    Window root_return, child;
    int rx, ry, wx, wy;
    unsigned int mask;
    if( XQueryPointer( dpy, root, &root_return, &child, &rx, &ry, &wx, &wy, &mask ))
        corner_armed = !cornerHit( QPoint( rx, ry ));
    // what changed while shown
    setKeepRedirected( options->overviewKeepRedirected );
    updateRedirection();
    updateCorners();
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
    // frames are shaped only for shaped clients or masked decorations (saves a round trip)
    if( Shape::available() && ( item->client == NULL || item->client->shape()
        || !item->client->mask().isEmpty()))
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

// the contents of the window: its redirected pixmap, or a copy of the screen
// (only while the screen still shows the real windows); false when neither
bool Overview::bindContents( OverviewItem* item, bool can_copy )
    {
#ifdef OVERVIEW_COMPOSITE
    if( !composite || item->format == NULL || item->mip_picture[ 0 ] != None )
        return true;
    // redirected by us, by the temporary redirection or by a compositing manager
    bool named = false;
    if( temp_redirected || redirected.contains( item->id ))
        named = true;
    else
        { // kompmgr? (inside the error trap of buildItems())
        int errors = trappedErrors();
        item->pixmap = XCompositeNameWindowPixmap( dpy, item->id );
        XSync( dpy, False );
        named = trappedErrors() == errors;
        if( !named )
            item->pixmap = None; // never valid
        }
    if( named )
        {
        if( item->pixmap == None )
            item->pixmap = XCompositeNameWindowPixmap( dpy, item->id );
        }
    else
        {
        if( !can_copy )
            return false;
        // full screen and override-redirect windows: a static copy
        const int depth = DefaultDepth( dpy, DefaultScreen( dpy ));
        item->pixmap = XCreatePixmap( dpy, root, item->geom.width(), item->geom.height(), depth );
        XGCValues gcv;
        gcv.subwindow_mode = IncludeInferiors;
        GC gc = XCreateGC( dpy, item->pixmap, GCSubwindowMode, &gcv );
        XCopyArea( dpy, root, item->pixmap, gc, item->geom.x(), item->geom.y(),
            item->geom.width(), item->geom.height(), 0, 0 );
        XFreeGC( dpy, gc );
        item->format = r->format;
        item->depth = depth;
        item->alpha_format = false;
        item->snapshot = true;
        }
    item->mip_picture[ 0 ] = XRenderCreatePicture( dpy, item->pixmap, item->format, 0, NULL );
    item->mip_w[ 0 ] = item->geom.width();
    item->mip_h[ 0 ] = item->geom.height();
    for( int i = 1;
         i < MipLevels;
         ++i )
        item->mip_dirty[ i ] = true;
    if( item->damage == None && !item->snapshot )
        item->damage = XDamageCreate( dpy, item->id, XDamageReportNonEmpty );
#else
    Q_UNUSED( item );
    Q_UNUSED( can_copy );
#endif
    return true;
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
            const bool existing = item != NULL;
            if( item != NULL )
                {
                QRect geom( attr.x, attr.y, attr.width + 2 * attr.border_width, attr.height + 2 * attr.border_width );
                if( geom.size() != item->geom.size() && !item->snapshot )
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
            // the screen shows the overview after the first frame: no copies then
            if( !bindContents( item, initial ))
                {
                if( existing )
                    item->seen = false; // forgotten and deleted below
                else
                    {
                    releaseContents( item );
                    delete item;
                    }
                continue;
                }
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
            forgetItem( item );
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
    redirected.remove( c->frameId()); // the frame is destroyed
    closing.remove( c );
    if( dialog == c )
        dialog = NULL;
    if( state == Hidden || r == NULL )
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
    forgetItem( item );
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

// no pointer to the item any more (it goes away)
void Overview::forgetItem( OverviewItem* item )
    {
    if( item == selected )
        selected = NULL;
    if( item == pressed )
        pressed = NULL;
    if( item == hovered )
        {
        hovered = NULL;
        close_hover = false;
        }
    if( item == pressed_close )
        {
        pressed_close = NULL;
        press_consumed = true;
        }
    if( item == dragged )
        { // closed while dragged: the release does nothing
        dragged = NULL;
        drag_target = -1;
        press_consumed = true;
        if( grabbed )
            XChangeActivePointerGrab( dpy, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                cursor, CurrentTime );
        }
    }

void Overview::clientAdded( Client* c )
    {
    if( state == Hidden || closing.isEmpty())
        return;
    for( QValueList< Client* >::ConstIterator it = closing.begin();
         it != closing.end();
         ++it )
        if( c->transientFor() == *it || Client::belongToSameApplication( c, *it ))
            {
            if( debug )
                kdWarning( 1212 ) << "overview: " << c->caption() << " appeared for " << (*it)->caption() << endl;
            dialog = c;
            dialog_timer.start( 0, true ); // not from within Workspace::addClient()
            return;
            }
    }

/*
 Closing a window from the overview (its close button, Ctrl+W, Delete) is
 like its titlebar close button: the application may ask first (save
 changes?).  Such a dialog, unlike in GNOME, closes the overview at once
 and gets activated: it is usable right away, and the overview doesn't
 show windows that appear while it is open.  The window is gone when the
 application closes it, and the overview is laid out anew then.
*/
void Overview::dialogTimeout()
    {
    Client* c = dialog;
    if( c != NULL && dragged != NULL )
        { // after the drop
        dialog_timer.start( 100, true );
        return;
        }
    dialog = NULL;
    if( c != NULL && ws->hasClient( c ) && ( state == Showing || state == Shown ) && c->isOnCurrentDesktop())
        hide( c );
    }

void Overview::closeItem( OverviewItem* item )
    {
    if( item == NULL || item->client == NULL || !ws->hasClient( item->client ) || !item->client->isCloseable())
        return;
    if( closing.contains( item->client ))
        return; // asked already (Delete repeating)
    if( debug )
        kdWarning( 1212 ) << "overview: closing " << item->caption << endl;
    closing.append( item->client );
    item->client->closeWindow();
    }

//****************************************
// layout
//****************************************

void Overview::computeLayout()
    {
    if( r == NULL )
        return;
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
        QRect area = layoutArea( i );
        int mx = QMAX( 32, area.width() * 4 / 100 );
        int my = QMAX( 32, area.height() * 5 / 100 );
        area.addCoords( mx, my, -mx, -my );
        layoutScreen( area, per_screen[ i ] );
        }
    if( debug )
        for( OverviewItem* item = items.first();
             item != NULL;
             item = items.next())
            if( item->in_layout )
                kdWarning( 1212 ) << "overview: slot " << ( item->client != NULL ? item->client->caption() : QString::null )
                    << " " << item->slot.x() << "," << item->slot.y() << " " << item->slot.width() << "x"
                    << item->slot.height() << " monitor " << item->screen << endl;
    }

// the work area of a monitor
QRect Overview::layoutArea( int screen ) const
    {
    QRect sg = QApplication::desktop()->screenGeometry( screen );
    QRect area = ws->clientArea( PlacementArea, sg.center(), ws->currentDesktop()).intersect( sg );
    return area.isValid() ? area : sg;
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
    if( n == 0 || r == NULL )
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
    if( r == NULL )
        return;
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
    if( r == NULL )
        return;
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
        presenter->resetStats();
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
    if( presenter->usesPresent())
        {
        if( !presenter->canQueue())
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

// a presentation completed (or the server stopped telling: the watchdog)
void Overview::presentCompleted( bool skipped, int missed )
    {
    if( r == NULL )
        return;
    if( skipped )
        ++stat_skipped;
    else if( state == Showing || state == Hiding || dragged != NULL )
        stat_gaps += missed;
    if( state == Finishing )
        {
        if( presenter->queued() == 0 )
            finish();
        }
    else if( frame_wanted )
        requestFrame();
    }

void Overview::presentIdle()
    {
    if( r != NULL && frame_wanted )
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
    Picture dst = presenter->beginFrame();
    if( dst == None )
        { // all still used by the server, wait for an idle event
        frame_wanted = true;
        return;
        }
    const double t_start = now();
    // the time when this frame will be seen
    const double t0 = presenter->frameTime( t_start );
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
        if( item->close_start > 0 )
            {
            double f = clamp01(( t0 - item->close_start ) / FadeTime );
            item->close_fade = mix( item->close_from, item->close_to, f );
            if( f >= 1 )
                item->close_start = 0;
            }
        }
    bool more = lin < 1.0;
    if( dragged != NULL )
        {
        placeDragged( t0 );
        if( t0 - drag_start < DragShrinkTime )
            more = true;
        }
    if( state == Showing && !more )
        {
        state = Shown;
        title_start = t0;
        updateHover();
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
        if( item->fade_start > 0 || item->close_start > 0 )
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
    // a desktop window covering the screen is drawn dimmed, no full screen
    // blend for the dimming
    desktop_dim = 0;
    OverviewItem* first = items.first();
    if( composite && dim > 1.0 / 512 && first != NULL && first->kind == OverviewItem::Desktop
        && first->geom == QRect( 0, 0, r->width, r->height ) && !first->shaped && !first->alpha_format
        && first->mip_picture[ 0 ] != None )
        {
        desktop_dim = dim;
        for( OverviewItem* item = items.next();
             item != NULL;
             item = items.next())
            if( item->kind == OverviewItem::Desktop )
                desktop_dim = 0;
        }

    // the windows, bottom first (the dragged one last)
    bool dimmed = false;
    for( OverviewItem* item = items.first();
         item != NULL;
         item = items.next())
        {
        if( !dimmed && item->kind != OverviewItem::Desktop )
            {
            drawBackdrop( dst, dim, t0 );
            dimmed = true;
            }
        if( item != dragged )
            drawItem( item, dst );
        }
    if( !dimmed )
        drawBackdrop( dst, dim, t0 );

    // the titles
    if( title_alpha > 1.0 / 512 )
        {
        for( OverviewItem* item = items.first();
             item != NULL;
             item = items.next())
            {
            if( !item->in_layout || item->title_picture == None || item == dragged )
                continue;
            double a = title_alpha * item->fade;
            int w = item->title_pixmap.width();
            int h = item->title_pixmap.height();
            int x = (int) floor( item->cur.cx() - w / 2.0 + 0.5 );
            int y = (int) floor( item->cur.y + item->cur.h + TitleGap + 0.5 );
            fillRounded( dpy, r, dst, RectF( x, y, w, h ), h / 2.0, QColor( 24, 24, 24 ), 0.78 * a );
            Picture white = solidPicture( dpy, Qt::white, a );
            XRenderComposite( dpy, PictOpOver, white, item->title_picture, dst, 0, 0, 0, 0, x, y, w, h );
            XRenderFreePicture( dpy, white );
            }
        }
    if( title_alpha > 1.0 / 512 && dragged == NULL )
        for( OverviewItem* item = items.first();
             item != NULL;
             item = items.next())
            if( item->in_layout && item->close_fade > 1.0 / 512 )
                drawCloseButton( item, dst );
    if( dragged != NULL )
        drawItem( dragged, dst );
    if( r->filter_picture != None && progress > 0.5 && state != Hiding && state != Finishing )
        {
        QDesktopWidget* desktop = QApplication::desktop();
        QRect sg = desktop->screenGeometry( desktop->primaryScreen());
        QRect area = ws->clientArea( PlacementArea, sg.center(), ws->currentDesktop()).intersect( sg );
        int w = r->filter_pixmap.width();
        int h = r->filter_pixmap.height();
        int x = area.x() + ( area.width() - w ) / 2;
        int y = area.y() + 12;
        fillRounded( dpy, r, dst, RectF( x, y, w, h ), h / 2.0, QColor( 24, 24, 24 ), 0.85 );
        Picture white = solidPicture( dpy, Qt::white, 1.0 );
        XRenderComposite( dpy, PictOpOver, white, r->filter_picture, dst, 0, 0, 0, 0, x, y, w, h );
        XRenderFreePicture( dpy, white );
        }

    if( ripple_start > 0 )
        {
        if( state == Hiding && !more )
            saveRippleBackground( dst ); // the last frame: the screen when the overlay goes
        if( drawRipples( dst, t0, 0, 0 ) && !( state == Hiding && !more ))
            frame_wanted = true;
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
        if( !presenter->usesPresent() || presenter->queued() == 0 )
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

// the dimmed background over the desktop, the monitor a thumbnail is
// dragged to
void Overview::drawBackdrop( XID dst, double dim, double t )
    {
    if( dim > 1.0 / 512 && desktop_dim == 0 )
        {
        XRenderColor c = renderColor( Qt::black, dim );
        XRenderFillRectangle( dpy, PictOpOver, dst, &c, 0, 0, r->width, r->height );
        }
    if( dragged != NULL && drag_target >= 0 )
        {
        double a = clamp01(( t - drag_target_start ) / FadeTime );
        RectF area( layoutArea( drag_target ));
        fillRounded( dpy, r, dst, RectF( area.x + 12, area.y + 12, area.w - 24, area.h - 24 ), 16,
            r->highlight, 0.18 * a );
        }
    }

void Overview::drawItem( OverviewItem* item, XID dst )
    {
    double alpha = 1.0;
    RectF rect( item->geom );
    bool scaled = false;
    switch( item->kind )
        {
        case OverviewItem::Desktop:
            alpha = 1.0 - desktop_dim;
            break;
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
        return;
    if( scaled && progress > 0.001 )
        { // a soft shadow and the selection (not below an opaque window)
        double a = QMIN( 1.0, progress ) * alpha;
        const bool hollow = composite && alpha >= 1.0 && !item->alpha_format && !item->shaped
            && item->mip_picture[ 0 ] != None;
        fillRounded( dpy, r, dst, RectF( rect.x - 3, rect.y - 1, rect.w + 6, rect.h + 7 ), 6, Qt::black, 0.10 * a, hollow );
        fillRounded( dpy, r, dst, RectF( rect.x - 1, rect.y, rect.w + 2, rect.h + 3 ), 3, Qt::black, 0.18 * a, hollow );
        if( item == selected && item != dragged && title_alpha > 0 )
            fillRounded( dpy, r, dst, RectF( rect.x - 8, rect.y - 8, rect.w + 16, rect.h + 16 ), 10,
                r->highlight, 0.75 * title_alpha * alpha, hollow );
        }
    if( !composite )
        { // icons on tiles
        if( !item->in_layout )
            return;
        fillRounded( dpy, r, dst, rect, 8, QColor( 64, 64, 64 ), 0.95 * alpha );
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
        return;
        }
    if( item->mip_picture[ 0 ] == None )
        return;
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
    // at scale 1 like the server's automatic compositing (Src, also for
    // ARGB windows), else blended (the edges are anti-aliased)
    int op = mask != None || ( item->alpha_format && !identity ) ? PictOpOver : PictOpSrc;
    if( item->kind == OverviewItem::Desktop && desktop_dim > 0 )
        op = PictOpSrc; // the background dimmed
    XRenderComposite( dpy, op, src, mask, dst, 0, 0, 0, 0, dx, dy, dw, dh );
    if( mask != None )
        XRenderFreePicture( dpy, mask );
    if( item->shaped )
        {
        XRenderPictureAttributes pa;
        pa.clip_mask = None;
        XRenderChangePicture( dpy, dst, CPClipMask, &pa );
        }
    }

// the center of the close button of the thumbnail
static QPoint closeCenter( const RectF& rect )
    {
    return QPoint( (int) floor( rect.x + rect.w - CloseInset + 0.5 ), (int) floor( rect.y + CloseInset + 0.5 ));
    }

// a line with the width w (a thin parallelogram, anti-aliased)
static void thickLine( Display* dpy, Picture dst, Picture src, double x1, double y1, double x2, double y2, double w )
    {
    double dx = x2 - x1;
    double dy = y2 - y1;
    double len = sqrt( dx * dx + dy * dy );
    double nx = -dy / len * w / 2;
    double ny = dx / len * w / 2;
    XPointDouble pts[ 4 ] = {{ x1 + nx, y1 + ny }, { x2 + nx, y2 + ny }, { x2 - nx, y2 - ny }, { x1 - nx, y1 - ny }};
    XRenderCompositeDoublePoly( dpy, PictOpOver, src, dst, XRenderFindStandardFormat( dpy, PictStandardA8 ),
        0, 0, 0, 0, pts, 4, WindingRule );
    }

void Overview::drawCloseButton( OverviewItem* item, XID dst )
    {
    const int size = CloseSize + 2;
    if( r->close_picture[ 0 ] == None )
        { // made once: a dark circle with a light rim and a white X
        for( int i = 0;
             i < 2;
             ++i )
            {
            r->close_pixmap[ i ] = XCreatePixmap( dpy, root, size, size, 32 );
            r->close_picture[ i ] = XRenderCreatePicture( dpy, r->close_pixmap[ i ],
                XRenderFindStandardFormat( dpy, PictStandardARGB32 ), 0, NULL );
            XRenderColor clear = { 0, 0, 0, 0 };
            XRenderFillRectangle( dpy, PictOpSrc, r->close_picture[ i ], &clear, 0, 0, size, size );
            const double c = size / 2.0;
            roundedRect( dpy, r->close_picture[ i ], RectF( 1, 1, CloseSize, CloseSize ), CloseSize / 2.0,
                Qt::white, 0.35 );
            roundedRect( dpy, r->close_picture[ i ], RectF( 2, 2, CloseSize - 2, CloseSize - 2 ), CloseSize / 2.0 - 1,
                i == 0 ? QColor( 40, 40, 40 ) : QColor( 96, 96, 96 ), 1.0 );
            Picture white = solidPicture( dpy, Qt::white, 1.0 );
            const double arm = CloseSize * 0.18;
            thickLine( dpy, r->close_picture[ i ], white, c - arm, c - arm, c + arm, c + arm, 2.2 );
            thickLine( dpy, r->close_picture[ i ], white, c - arm, c + arm, c + arm, c - arm, 2.2 );
            XRenderFreePicture( dpy, white );
            }
        }
    const QPoint center = closeCenter( item->cur );
    const double a = item->close_fade * title_alpha * item->fade;
    if( a <= 1.0 / 512 )
        return;
    Picture mask = a < 1.0 ? solidPicture( dpy, Qt::black, a ) : None;
    XRenderComposite( dpy, PictOpOver, r->close_picture[ item == hovered && close_hover ? 1 : 0 ], mask, dst,
        0, 0, 0, 0, center.x() - size / 2, center.y() - size / 2, size, size );
    if( mask != None )
        XRenderFreePicture( dpy, mask );
    }

bool Overview::closeHit( const OverviewItem* item, const QPoint& p ) const
    {
    if( item == NULL || item->close_to < 1 )
        return false;
    QPoint d = p - closeCenter( RectF( item->slot ));
    return d.x() * d.x() + d.y() * d.y() <= ( CloseSize / 2 + 2 ) * ( CloseSize / 2 + 2 );
    }

// the thumbnail under the pointer gets its close button
void Overview::setHover( OverviewItem* item, bool on_close )
    {
    const double t = now();
    if( item != hovered )
        {
        if( hovered != NULL )
            {
            hovered->close_from = hovered->close_fade;
            hovered->close_to = 0;
            hovered->close_start = t;
            }
        hovered = item;
        if( item != NULL && item->client != NULL && ws->hasClient( item->client ) && item->client->isCloseable())
            {
            item->close_from = item->close_fade;
            item->close_to = 1;
            item->close_start = t;
            }
        requestFrame();
        }
    on_close = on_close && hovered != NULL && hovered->close_to >= 1;
    if( on_close != close_hover )
        {
        close_hover = on_close;
        requestFrame();
        }
    }

void Overview::present()
    {
    presenter->endFrame();
    }

void Overview::dumpFrame()
    {
    XImage* img = XGetImage( dpy, presenter->framePixmap(), 0, 0, r->width, r->height, AllPlanes, ZPixmap );
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
    if( dragged != NULL )
        { // only Escape, it cancels the drag (the button is still pressed)
        if( XLookupKeysym( e, 0 ) == XK_Escape )
            endDrag( false, drag_pos );
        return true;
        }
    char buf[ 32 ];
    if(( e->state & ControlMask ) && XLookupKeysym( e, 0 ) == XK_w )
        { // Ctrl+W, whatever the layout
        closeItem( selected );
        return true;
        }
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
        case XK_Delete:
        case XK_KP_Delete:
            closeItem( selected );
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

// the Super key alone while the overview is shown: its grab keeps the raw
// events away (see handleXI2()), the key events come instead
bool Overview::superKey( XKeyEvent* e )
    {
#ifdef OVERVIEW_XI2
    if( !options->overviewSuperKey )
        return false;
    KeySym sym = XkbKeycodeToKeysym( dpy, e->keycode, 0, 0 );
    bool super = sym == XK_Super_L || sym == XK_Super_R;
    if( e->type == KeyPress )
        {
        if( !super )
            super_alone = false;
        else if( !super_down ) // else autorepeat
            {
            super_down = true;
            super_alone = superAlone( e->keycode );
            super_time = e->time;
            }
        return super;
        }
    if( super && super_down )
        {
        super_down = false;
        if( super_alone && (unsigned int)( e->time - super_time ) < SuperTapTime )
            toggle();
        }
    return super;
#else
    Q_UNUSED( e );
    return false;
#endif
    }

bool Overview::handleButton( XButtonEvent* e )
    {
    if( state == Hiding || state == Finishing )
        return true;
    QPoint p( e->x_root, e->y_root );
    if( e->type == ButtonPress )
        {
        super_alone = false; // Super+click
        if( e->button == Button1 && pressed_button == 0 )
            {
            if( hovered != NULL && dragged == NULL && closeHit( hovered, p ))
                {
                pressed_close = hovered;
                pressed = NULL;
                }
            else
                {
                pressed_close = NULL;
                pressed = itemAt( p );
                }
            pressed_button = e->button;
            press_pos = p;
            press_consumed = false;
            }
        return true;
        }
    if( e->button != Button1 || pressed_button != (int) e->button )
        return true;
    pressed_button = 0;
    if( press_consumed )
        { // the drag was cancelled, the window closed meanwhile
        press_consumed = false;
        pressed = pressed_close = NULL;
        return true;
        }
    if( pressed_close != NULL )
        { // released on the close button it was pressed on
        OverviewItem* item = pressed_close;
        pressed_close = NULL;
        if( closeHit( item, p ))
            closeItem( item );
        return true;
        }
    if( dragged != NULL )
        {
        pressed = NULL;
        endDrag( true, p );
        return true;
        }
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
    if( dragged != NULL )
        {
        drag_pos = p;
        int target = dropTarget( p );
        if( target != drag_target )
            {
            drag_target = target;
            drag_target_start = now();
            }
        if( cornerHit( p ))
            corner_armed = false; // a drop there doesn't close it
        requestFrame();
        return;
        }
    if( pressed != NULL && pressed_button == Button1 && !press_consumed && state == Shown
        && QApplication::desktop()->numScreens() > 1 // (nowhere to drop it else)
        && ( p - press_pos ).manhattanLength() >= QMAX( DragThreshold, KGlobalSettings::dndEventDelay()))
        {
        startDrag( p );
        return;
        }
    // the hot corner again closes it, after the pointer left it
    if( !corners.isEmpty())
        {
        if( cornerHit( p ))
            {
            if( corner_armed )
                {
                corner_armed = false;
                startRipple( p );
                hide();
                return;
                }
            }
        else
            {
            bool near = false;
            for( QValueList< QRect >::ConstIterator it = corner_areas.begin();
                 it != corner_areas.end();
                 ++it )
                if( QRect( (*it).x() - 2, (*it).y() - 2, (*it).width() + 4, (*it).height() + 4 ).contains( p ))
                    near = true;
            if( !near )
                corner_armed = true;
            }
        }
    OverviewItem* item = itemAt( p );
    if( hovered != NULL && closeHit( hovered, p ))
        item = hovered; // the button sticks out of the thumbnail
    setHover( state == Shown ? item : NULL, item != NULL && closeHit( item, p ));
    if( item != NULL )
        select( item );
    }

// the close button where the pointer is (it didn't move, the layout did)
void Overview::updateHover()
    {
    Window root_return, child;
    int rx, ry, wx, wy;
    unsigned int mask;
    if( dragged == NULL && state == Shown
        && XQueryPointer( dpy, root, &root_return, &child, &rx, &ry, &wx, &wy, &mask ))
        {
        QPoint p( rx, ry );
        OverviewItem* item = itemAt( p );
        setHover( item, item != NULL && closeHit( item, p ));
        }
    }

/*
 Dragging a thumbnail to another monitor, like GNOME: it follows the
 pointer, shrinking, above everything; the monitor under the pointer is
 highlighted.  Nothing happens to the window until the drop (no
 configure, no expose: the overview only draws its pixmap elsewhere).
 Dropped on another monitor the window moves there once
 (Client::sendToScreen()) and both monitors are laid out anew, animated;
 dropped on its own monitor, or Escape, it flies back to its place.
*/
void Overview::startDrag( const QPoint& p )
    {
    OverviewItem* item = pressed;
    if( item == NULL || !item->in_layout || item->client == NULL )
        return;
    setHover( NULL, false );
    dragged = item;
    drag_pos = p;
    drag_fx = item->cur.w > 0 ? clamp01(( press_pos.x() - item->cur.x ) / item->cur.w ) : 0.5;
    drag_fy = item->cur.h > 0 ? clamp01(( press_pos.y() - item->cur.y ) / item->cur.h ) : 0.5;
    drag_w = item->cur.w;
    drag_h = item->cur.h;
    drag_start = now();
    drag_target = dropTarget( p );
    drag_target_start = drag_start;
    if( grabbed )
        XChangeActivePointerGrab( dpy, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
            QCursor( Qt::SizeAllCursor ).handle(), CurrentTime );
    stat_first_frame = drag_start;
    frame_count = 0;
    stat_render = stat_max_render = 0;
    stat_skipped = stat_gaps = 0;
    if( r != NULL )
        presenter->resetStats();
    if( debug )
        kdWarning( 1212 ) << "overview: dragging " << item->caption << " from monitor " << item->screen << endl;
    requestFrame();
    }

// the dragged thumbnail at time t
void Overview::placeDragged( double t )
    {
    const double full = QMIN( 1.0, DragSize / QMAX( 1.0, QMAX( drag_w, drag_h )));
    const double s = mix( 1.0, full, easeOutQuad( clamp01(( t - drag_start ) / DragShrinkTime )));
    const double w = drag_w * s;
    const double h = drag_h * s;
    dragged->cur = dragged->from = dragged->to = RectF( drag_pos.x() - drag_fx * w, drag_pos.y() - drag_fy * h, w, h );
    }

// the monitor a thumbnail dropped at p goes to; -1 for its own one
int Overview::dropTarget( const QPoint& p ) const
    {
    QDesktopWidget* desktop = QApplication::desktop();
    if( dragged == NULL || desktop->numScreens() < 2 )
        return -1;
    int screen = -1;
    for( int i = 0;
         i < desktop->numScreens();
         ++i )
        if( desktop->screenGeometry( i ).contains( p ))
            {
            screen = i;
            break;
            }
    return screen == dragged->screen ? -1 : screen;
    }

void Overview::endDrag( bool drop, const QPoint& p )
    {
    OverviewItem* item = dragged;
    if( item == NULL )
        return;
    if( debug )
        {
        double t = now() - stat_first_frame;
        kdWarning( 1212 ) << "overview: drag " << frame_count << " frames in " << t << " ms, "
            << ( frame_count > 1 ? frame_count * 1000.0 / t : 0 ) << " fps, render avg "
            << ( frame_count ? stat_render / frame_count : 0 ) << " max " << stat_max_render << " ms, skipped "
            << stat_skipped << " missed refreshes " << stat_gaps << endl;
        }
    placeDragged( now()); // flies from where it is seen
    const int target = drop ? dropTarget( p ) : -1;
    dragged = NULL;
    if( !drop )
        press_consumed = true; // the button is still pressed
    if( grabbed )
        XChangeActivePointerGrab( dpy, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
            cursor, CurrentTime );
    drag_target = -1;
    bool moved = false;
    if( target >= 0 && item->client != NULL && ws->hasClient( item->client ))
        {
        if( debug )
            kdWarning( 1212 ) << "overview: " << item->caption << " to monitor " << target << endl;
        moved = item->client->sendToScreen( item->screen, target );
        if( state == Hidden || r == NULL )
            return; // aborted meanwhile
        }
    if( moved )
        { // the new geometries (also of the dialogs that went along)
        buildItems( false );
        computeLayout();
        updateTitles();
        }
    startTransition( 1.0 );
    updateHover();
    }

//****************************************
// XInput 2: the Super key, the barriers
//****************************************

/*
 The Super key pressed and released alone toggles the overview, like
 GNOME's overlay key.  The key is not grabbed (a passive grab of a
 modifier would take every Meta+X shortcut from the applications and
 KGlobalAccel): XInput 2.1 raw key and button events on the root window
 tell the presses and releases of all keys, also while another client has
 a grab, without changing what anybody else gets.  The overview opens on
 the release when nothing else was pressed meanwhile (Meta+E, Meta+click,
 Meta+wheel), no other key was held at the press (Shift+Super) and the key
 was held shorter than SuperTapTime.  When another client has the keyboard
 grabbed (a menu, the screen locker), show() can't grab it and nothing
 happens.
*/
void Overview::selectXI2()
    {
#ifdef OVERVIEW_XI2
    if( !have_xi_raw )
        return;
    XIEventMask mask;
    unsigned char bits[ XIMaskLen( XI_LASTEVENT ) ] = { 0 };
    if( options->overviewSuperKey )
        {
        XISetMask( bits, XI_RawKeyPress );
        XISetMask( bits, XI_RawKeyRelease );
        XISetMask( bits, XI_RawButtonPress );
        }
    if( have_barriers && !barrier_lines.isEmpty())
        XISetMask( bits, XI_BarrierHit );
    mask.deviceid = XIAllMasterDevices;
    mask.mask_len = sizeof( bits );
    mask.mask = bits;
    XISelectEvents( dpy, root, &mask, 1 );
    super_down = false;
#endif
    }

// nothing but the Super key (keycode) held (Shift+Super is no tap)
bool Overview::superAlone( int keycode )
    {
    char keys[ 32 ];
    XQueryKeymap( dpy, keys );
    keys[ ( keycode / 8 ) & 31 ] &= ~( 1 << ( keycode % 8 ));
    for( int i = 0;
         i < 32;
         ++i )
        if( keys[ i ] != 0 )
            return false;
    return true;
    }

bool Overview::handleXI2( XEvent* e )
    {
#ifdef OVERVIEW_XI2
    XGenericEventCookie* cookie = &e->xcookie;
    if( !XGetEventData( dpy, cookie ))
        return true;
    bool tap = false;
    switch( cookie->evtype )
        {
        case XI_RawKeyPress:
            {
            XIRawEvent* ev = (XIRawEvent*) cookie->data;
            if( ev->flags & XIKeyRepeat )
                break;
            KeySym sym = XkbKeycodeToKeysym( dpy, ev->detail, 0, 0 );
            // (a release lost under a grab of kwin's own must not spoil the
            // next tap: every press is a new one, modifiers don't repeat)
            if( sym == XK_Super_L || sym == XK_Super_R )
                {
                super_down = true;
                super_time = ev->time;
                super_alone = superAlone( ev->detail );
                }
            else
                super_alone = false;
            break;
            }
        case XI_RawKeyRelease:
            {
            XIRawEvent* ev = (XIRawEvent*) cookie->data;
            KeySym sym = XkbKeycodeToKeysym( dpy, ev->detail, 0, 0 );
            if(( sym == XK_Super_L || sym == XK_Super_R ) && super_down )
                {
                super_down = false;
                tap = super_alone && (unsigned int)( ev->time - super_time ) < SuperTapTime;
                if( debug )
                    kdWarning( 1212 ) << "overview: Super released after " << (unsigned int)( ev->time - super_time )
                        << " ms, alone " << super_alone << endl;
                }
            break;
            }
        case XI_RawButtonPress:
            super_alone = false;
            break;
        case XI_BarrierHit:
            {
            // through the barrier when the corner doesn't apply: a window
            // (or anything else) being dragged, a full screen window there
            // (while the overview is shown, its grab keeps these events
            // away: the corner stays, to close it again)
            XIBarrierEvent* ev = (XIBarrierEvent*) cookie->data;
            QPoint p( int( ev->root_x ), int( ev->root_y ));
            // and after the corner has fired, until the pointer leaves it:
            // pushing on goes through to the other monitor
            bool release = ( ev->flags & XIBarrierDeviceIsGrabbed ) || !corner_armed
                || !cornerAllowed( p, 0 );
            if( debug )
                kdWarning( 1212 ) << "overview: barrier hit " << p.x() << "," << p.y() << " flags " << ev->flags
                    << " state " << state << ( release ? " released" : "" ) << endl;
            if( release )
                XIBarrierReleasePointer( dpy, ev->deviceid, ev->barrier, ev->eventid );
            break;
            }
        default:
            break;
        }
    XFreeEventData( dpy, cookie );
    if( tap && options->overviewSuperKey )
        toggle();
#else
    Q_UNUSED( e );
#endif
    return true;
    }

bool Overview::x11Event( XEvent* e )
    {
    if(( e->type == EnterNotify || e->type == LeaveNotify ) && corners.contains( e->xcrossing.window ))
        {
        QPoint p( e->xcrossing.x_root, e->xcrossing.y_root );
        if( debug )
            kdWarning( 1212 ) << "overview: corner " << ( e->type == EnterNotify ? "enter " : "leave " )
                << p.x() << "," << p.y() << " mode " << e->xcrossing.mode << " state " << state
                << " armed " << corner_armed << " allowed " << cornerAllowed( p, e->xcrossing.state ) << endl;
        if( e->type == LeaveNotify )
            {
            corner_timer.stop();
            // not when a grab starts (ours, the overview opening: the
            // pointer is still in the corner, and a real mouse pushed into
            // it keeps sending motion events there, which would close the
            // overview at once), nor while the overview is shown (then
            // handleMotion() re-arms it when the pointer is away)
            if( e->xcrossing.mode == NotifyNormal && state == Hidden )
                corner_armed = true;
            return true;
            }
        if( state != Hidden || !corner_armed || e->xcrossing.mode != NotifyNormal
            || !cornerAllowed( p, e->xcrossing.state ))
            return true;
        if( options->overviewHotCornerDelay > 0 )
            corner_timer.start( options->overviewHotCornerDelay, true );
        else
            {
            corner_armed = false;
            startRipple( p );
            show();
            if( state == Hidden )
                stopRipple();
            }
        return true;
        }
    if( e->type == ButtonPress && corners.contains( e->xbutton.window ))
        {
        corner_timer.stop();
        QPoint p( e->xbutton.x_root, e->xbutton.y_root );
        if( debug )
            kdWarning( 1212 ) << "overview: corner click " << p.x() << "," << p.y() << " state " << state
                << " allowed " << cornerAllowed( p, 0 ) << endl;
        if( state == Hidden && cornerAllowed( p, 0 ))
            {
            corner_armed = false;
            startRipple( p );
            show();
            if( state == Hidden )
                stopRipple();
            }
        return true;
        }
#ifdef OVERVIEW_XI2
    if( e->type == GenericEvent && e->xgeneric.extension == xi_opcode && xi_opcode != 0 )
        return handleXI2( e );
    if( have_monitors && ( e->type == randr_event + RRScreenChangeNotify || e->type == randr_event + RRNotify ))
        {
        monitors_timer.start( 200, true );
        if( dragged != NULL ) // the monitors are numbered anew
            endDrag( false, drag_pos );
        return false; // Qt needs RRScreenChangeNotify too
        }
#endif
    // (the Present events go to Compositing::Presenter::dispatch())
    if( state == Hidden )
        return false;
#ifdef OVERVIEW_COMPOSITE
    if( composite && e->type == damage_event + XDamageNotify )
        return handleDamage( e );
#endif
    switch( e->type )
        {
        case KeyPress:
            if( superKey( &e->xkey ))
                return true;
            return handleKey( &e->xkey );
        case KeyRelease:
            superKey( &e->xkey );
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
bool Overview::isRedirected( Window ) const { return false; }
bool Overview::x11Event( XEvent* ) { return false; }
void Overview::reconfigure() {}
void Overview::setShortcut( const KShortcut& ) {}
void Overview::clientRemoved( Client* ) {}
void Overview::clientAdded( Client* ) {}
void Overview::dialogTimeout() {}
void Overview::rippleTimeout() {}
void Overview::abort() {}
void Overview::frameTimeout() {}
void Overview::settleTimeout() {}
void Overview::cornerTimeout() {}
void Overview::monitorsChanged() {}
void Overview::presentCompleted( bool, int ) {}
void Overview::presentIdle() {}
void Overview::frameCreated( Window ) {}
void Overview::stackingChanged() {}
void Overview::raiseCorners() {}
void Overview::setBarriersEnabled( bool ) {}

#endif // HAVE_XRENDER

} // namespace

#include "overview.moc"
