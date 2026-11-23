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
#include <kwin.h>

#include "workspace.h"
#include "client.h"
#include "options.h"
#include "utils.h"
#include "compositing.h"
#include "effects.h"
#include "compositor.h"
#include "render.h"

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
// minimized windows: darkened by this much, at rest this much smaller
// (they fade in there while the others zoom out), the icon of those
// without contents at most this large
const double MinimizedDim = 0.30;
const double MinimizedRest = 0.85;
const int PlaceholderIcon = 96;
// a window restored from the overview shows its live contents after it
// has painted and then stayed quiet for this long; the zoom-in waits for
// that at most this long after its end (else the kept ones stay)
const double RestoreQuiet = 40.0;
const double RestoreWait = 200.0;

// the mip level that is downscaled by at most a half
int mipLevel( double scale )
    {
    int level = 0;
    while( level < MipLevels - 1 && scale * ( 1 << ( level + 1 )) <= 1.0 )
        ++level;
    return level;
    }

/*
 Anti-aliased shapes as alpha masks, computed on the CPU (4x4 samples per
 pixel) and drawn through the renderer: rounded rectangles (made once per
 radius and cached, see fillRounded()), the ripples' quarter discs and
 rims (small, every frame), the close button's cross (once).  No
 polygons: glamor rasterizes those on the CPU too and uploads the mask
 every time, the GLES renderer has none.
*/
struct AlphaMask
    {
    AlphaMask( int w_, int h_ ) : w( QMAX( 1, w_ )), h( QMAX( 1, h_ )), data( w * h ) { data.fill( 0 ); }
    int w, h;
    QMemArray< unsigned char > data;
    // the coverage of inside( x, y ) (pixel coordinates, the mask at ox, oy)
    template< class Inside > void add( Inside inside, double ox, double oy, double alpha = 1.0 )
        {
        for( int y = 0;
             y < h;
             ++y )
            for( int x = 0;
                 x < w;
                 ++x )
                {
                int n = 0;
                for( int sy = 0;
                     sy < 4;
                     ++sy )
                    for( int sx = 0;
                         sx < 4;
                         ++sx )
                        if( inside( ox + x + ( sx + 0.5 ) / 4, oy + y + ( sy + 0.5 ) / 4 ))
                            ++n;
                if( n > 0 )
                    {
                    const int v = data[ y * w + x ] + (int) floor( n / 16.0 * 255 * alpha + 0.5 );
                    data[ y * w + x ] = (unsigned char) QMIN( 255, v );
                    }
                }
        }
    Render::Texture* texture( Render::Renderer* r ) const { return r->textureFromAlpha( data.data(), w, h, w ); }
    };

struct InRoundedRect
    {
    InRoundedRect( const RectF& r_, double radius ) : r( r_ ), rad( radius ) {}
    bool operator()( double x, double y ) const
        {
        if( x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h )
            return false;
        const double cx = x < r.x + rad ? r.x + rad : x > r.x + r.w - rad ? r.x + r.w - rad : x;
        const double cy = y < r.y + rad ? r.y + rad : y > r.y + r.h - rad ? r.y + r.h - rad : y;
        return ( x - cx ) * ( x - cx ) + ( y - cy ) * ( y - cy ) <= rad * rad;
        }
    RectF r;
    double rad;
    };

// the quarter of a ring (inner 0: a disc) around cx, cy opening towards dx, dy
struct InQuarterRing
    {
    InQuarterRing( double cx_, double cy_, double dx_, double dy_, double outer_, double inner_ )
        : cx( cx_ ), cy( cy_ ), dx( dx_ ), dy( dy_ ), outer( outer_ ), inner( inner_ ) {}
    bool operator()( double x, double y ) const
        {
        const double u = ( x - cx ) * dx;
        const double v = ( y - cy ) * dy;
        if( u < 0 || v < 0 )
            return false;
        const double d = u * u + v * v;
        return d <= outer * outer && d >= inner * inner;
        }
    double cx, cy, dx, dy, outer, inner;
    };

// a line of width w (a thin parallelogram)
struct InLine
    {
    InLine( double x1_, double y1_, double x2_, double y2_, double w_ ) : x1( x1_ ), y1( y1_ ), w( w_ )
        {
        dx = x2_ - x1;
        dy = y2_ - y1;
        len = sqrt( dx * dx + dy * dy );
        }
    bool operator()( double x, double y ) const
        {
        const double along = (( x - x1 ) * dx + ( y - y1 ) * dy ) / len;
        const double across = (( x - x1 ) * -dy + ( y - y1 ) * dx ) / len;
        return along >= 0 && along <= len && fabs( across ) <= w / 2;
        }
    double x1, y1, dx, dy, len, w;
    };

// inside either of two lines (a cross)
struct InCross
    {
    InCross( const InLine& a_, const InLine& b_ ) : a( a_ ), b( b_ ) {}
    bool operator()( double x, double y ) const { return a( x, y ) || b( x, y ); }
    InLine a, b;
    };

// an anti-aliased rounded rectangle drawn at once (small ones)
void roundedRect( Render::Renderer* r, const RectF& rect, double radius, const QColor& c, double alpha )
    {
    if( alpha <= 1.0 / 512 || rect.w <= 0 || rect.h <= 0 )
        return;
    radius = QMIN( radius, QMIN( rect.w, rect.h ) / 2 );
    const QRect b = rect.bounds();
    AlphaMask m( b.width(), b.height());
    m.add( InRoundedRect( rect, radius ), b.x(), b.y());
    Render::Texture* t = m.texture( r );
    if( t == NULL )
        return;
    r->fillMasked( RectF( b ), c, alpha, t, RectF( 0, 0, m.w, m.h ));
    delete t;
    }

// an alpha texture from text drawn white on black (titles)
Render::Texture* alphaTexture( Render::Renderer* r, const QPixmap& pm )
    {
    if( pm.isNull() || r == NULL )
        return NULL;
    QImage img = pm.convertToImage().convertDepth( 32 );
    QMemArray< unsigned char > a( img.width() * img.height());
    for( int y = 0;
         y < img.height();
         ++y )
        {
        const QRgb* line = (const QRgb*) img.scanLine( y );
        for( int x = 0;
             x < img.width();
             ++x )
            a[ y * img.width() + x ] = qGray( line[ x ] );
        }
    return r->textureFromAlpha( a.data(), img.width(), img.height(), img.width());
    }

// a texture from a QPixmap (icons)
Render::Texture* imageTexture( Render::Renderer* r, const QPixmap& pm )
    {
    if( pm.isNull() || r == NULL )
        return NULL;
    return r->textureFromImage( pm.convertToImage());
    }

} // namespace

class OverviewItem
    {
    public:
        enum Kind { Desktop, Dock, Managed, Other };
        OverviewItem()
            : kind( Other ), client( NULL ), id( None ), depth( 0 ), format( NULL ), visual( NULL ),
              alpha_format( false ), shaped( false ), pixmap( None ), damage( None ),
              in_layout( false ), screen( 0 ), matches( true ),
              fade_from( 1 ), fade_to( 1 ), fade_start( 0 ), fade( 1 ),
              title_tex( NULL ), icon_tex( NULL ), seen( true ),
              snapshot( false ), close_from( 0 ), close_to( 0 ), close_start( 0 ), close_fade( 0 ),
              minimized( false ), kept( false ), placeholder( false ), restored( false ),
              pending_live( false ), painted( 0 ), external( false ), opacity( 1.0 )
            {
            for( int i = 0;
                 i < MipLevels;
                 ++i )
                {
                mip[ i ] = NULL;
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
        Visual* visual; // (of format: the pixmap's textures need it)
        bool alpha_format;
        bool shaped;
        QValueVector< XRectangle > shape; // relative to geom
        Pixmap pixmap; // mip level 0 is the named window pixmap
        // the contents and their downscaled copies (renderer textures);
        // external: level 0 is the compositor's (asked for every frame)
        Render::Texture* mip[ MipLevels ];
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
        Render::Texture* title_tex; // (alpha)
        Render::Texture* icon_tex;
        int icon_w, icon_h;
        bool seen;
        bool snapshot; // a copy of the screen, not redirected
        double close_from, close_to, close_start, close_fade; // the close button fading
        // a minimized window (not on the screen, its frame unmapped): shown
        // with the contents the window animations kept when it was
        // minimized (kept: mip level 0 is a picture of their pixmap, which
        // isn't ours) or as a card with its icon (placeholder)
        bool minimized;
        bool kept;
        bool placeholder;
        // was minimized, restored while the overview zooms in
        bool restored;
        // waits for its live contents: shows the kept ones (or the card, or
        // nothing for one mapped meanwhile) until it has painted; painted is
        // the time of the last damage then
        bool pending_live;
        double painted;
        bool external;
        double opacity; // composited: the window's opacity (_NET_WM_WINDOW_OPACITY)
        bool hasContents() const { return mip[ 0 ] != NULL || external; }
    };

class OverviewRenderer
    {
    public:
        OverviewRenderer()
            : width( 0 ), height( 0 ), format( NULL ),
              snapshot( None ), snapshot_tex( NULL ), filter_tex( NULL ), badge_tex( NULL )
            {
            close_tex[ 0 ] = close_tex[ 1 ] = NULL;
            }
        int width, height;
        XRenderPictFormat* format;
        Pixmap snapshot; // the screen, without Composite
        Render::Texture* snapshot_tex;
        QPixmap filter_pixmap;
        Render::Texture* filter_tex; // (alpha)
        // anti-aliased rounded rectangles drawn from cached masks: the corners
        // of a rectangle with this radius (Picture full), its middle column
        // and row (col, row, padded): no polygons in every frame (glamor
        // rasterizes those on the CPU and uploads the mask each time)
        struct RoundMask
            {
            RoundMask() : ri( 0 ), full( NULL ), col( NULL ), row( NULL ) {}
            int ri; // the corner cells, ceil( radius ) + 1
            Render::Texture* full;
            Render::Texture* col;
            Render::Texture* row;
            };
        QMap< int, RoundMask > round_masks; // by radius * 2
        // the close button, normal and hovered (ARGB, made once)
        Render::Texture* close_tex[ 2 ];
        // the badge of minimized windows (ARGB, made once)
        Render::Texture* badge_tex;
        QFont font;
        int title_height;
        QColor highlight;
    };

static RectF restRect( const OverviewItem* item );
static void drawIcon( Render::Renderer* rr, OverviewItem* item, const RectF& rect, double max_size,
    double fraction, double alpha );


/*
 An anti-aliased rounded rectangle at any subpixel position from cached
 masks, in nine cells that cover each pixel once: the corners sampled
 (translated, bilinear) from a small mask of a rounded rectangle with that
 radius, the edges from its middle column and row (padded along the edge),
 the middle filled.  hollow leaves the middle out (under an opaque
 window).  Tiny rectangles are polygons.
*/
static void fillRounded( Render::Renderer* rr, OverviewRenderer* r, const RectF& rect, double radius,
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
        roundedRect( rr, rect, radius, c, alpha );
        return;
        }
    if( !r->round_masks.contains( key ))
        { // the rounded rectangle at 2,2 with the size 2 * ri, in a transparent margin
        OverviewRenderer::RoundMask m;
        m.ri = ri;
        const int n = 2 * ri + 4;
        AlphaMask full( n, n );
        full.add( InRoundedRect( RectF( 2, 2, 2 * ri, 2 * ri ), radius ), 0, 0 );
        AlphaMask col( 1, n );
        AlphaMask row( n, 1 );
        for( int i = 0;
             i < n;
             ++i )
            {
            col.data[ i ] = full.data[ i * n + 2 + ri ];
            row.data[ i ] = full.data[ ( 2 + ri ) * n + i ];
            }
        m.full = full.texture( rr );
        m.col = col.texture( rr );
        m.row = row.texture( rr );
        if( m.full == NULL || m.col == NULL || m.row == NULL )
            {
            delete m.full;
            delete m.col;
            delete m.row;
            return;
            }
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
    for( int j = 0;
         j < 3;
         ++j )
        for( int i = 0;
             i < 3;
             ++i )
            {
            Render::Texture* mask = i == 1 ? ( j == 1 ? NULL : m.col ) : ( j == 1 ? m.row : m.full );
            if(( i == 1 && j == 1 && hollow ) || cx[ i + 1 ] <= cx[ i ] || cy[ j + 1 ] <= cy[ j ] )
                continue;
            const RectF cell( cx[ i ], cy[ j ], cx[ i + 1 ] - cx[ i ], cy[ j + 1 ] - cy[ j ] );
            if( mask == NULL )
                rr->fill( cell, c, alpha );
            else
                { // (the column and the row are padded along their edge)
                const RectF src( i == 1 ? 0 : cell.x + tx[ i ], j == 1 ? 0 : cell.y + ty[ j ],
                    i == 1 ? 1 : cell.w, j == 1 ? 1 : cell.h );
                Render::Draw d;
                d.color = c;
                d.color_alpha = alpha;
                d.dst = cell;
                d.mask = mask;
                d.mask_src = src;
                d.mask_filter = Render::BilinearFilter;
                d.mask_repeat = Render::PadRepeat;
                rr->draw( d );
                }
            }
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
      comp( false ),
      own_renderer( NULL ),
      rr( NULL ),
      target( NULL ),
      made_copy( false ),
      comp_t0( 0 ),
      comp_more( false ),
      comp_last( false ),
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
        XRRSelectInput( dpy, root, RRScreenChangeNotifyMask | RRCrtcChangeNotifyMask | RROutputChangeNotifyMask
            | RROutputPropertyNotifyMask ); // (the same as Workspace::initScreens(): one selection per client)
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
    delete own_renderer;
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
bool Overview::drawRipples( double t, int ox, int oy )
    {
    if( ripple_start <= 0 || t - ripple_start >= RippleTime )
        return false;
    const int pos = options->overviewHotCornerPosition;
    // the corner point of the pixel grid, the quarter opening into the monitor
    const double cx = ripple_point.x() + ( pos == 1 || pos == 3 ? 1 : 0 ) - ox;
    const double cy = ripple_point.y() + ( pos == 2 || pos == 3 ? 1 : 0 ) - oy;
    const double dx = pos == 1 || pos == 3 ? -1 : 1;
    const double dy = pos == 2 || pos == 3 ? -1 : 1;
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
        // a quarter disc, brighter at its rim (masks of the quarter's square)
        const int n = (int) ceil( radius ) + 1;
        const double mx = dx > 0 ? cx : cx - n;
        const double my = dy > 0 ? cy : cy - n;
        const int ix = (int) floor( mx );
        const int iy = (int) floor( my );
        AlphaMask disc( n + 1, n + 1 );
        disc.add( InQuarterRing( cx, cy, dx, dy, radius, 0 ), ix, iy );
        AlphaMask rim( n + 1, n + 1 );
        rim.add( InQuarterRing( cx, cy, dx, dy, radius, QMAX( 0.0, radius - 2.0 )), ix, iy );
        Render::Texture* m = disc.texture( rr );
        if( m != NULL )
            rr->fillMasked( RectF( ix, iy, n + 1, n + 1 ), Qt::white, 0.22 * opacity, m, RectF( 0, 0, n + 1, n + 1 ));
        delete m;
        m = rim.texture( rr );
        if( m != NULL )
            rr->fillMasked( RectF( ix, iy, n + 1, n + 1 ), Qt::white, 0.35 * opacity, m, RectF( 0, 0, n + 1, n + 1 ));
        delete m;
        }
    return true;
    }

// (uncomposited) the screen there in the last frame, without the ripples
void Overview::saveRippleBackground()
    {
    if( ripple_bg != None || !keep_redirected || r == NULL || comp )
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
    rr->finish(); // (what is drawn so far)
    ripple_bg = XCreatePixmap( dpy, root, box.width(), box.height(), DefaultDepth( dpy, DefaultScreen( dpy )));
    GC gc = XCreateGC( dpy, ripple_bg, 0, NULL );
    XCopyArea( dpy, presenter->framePixmap(), ripple_bg, gc, box.x(), box.y(), box.width(), box.height(), 0, 0 );
    XFreeGC( dpy, gc );
    }

// the overview goes, the ripples aren't over: on in a window of their own
void Overview::showRippleWindow()
    {
    if( debug )
        kdWarning( 1212 ) << "overview: ripples at " << now() - ripple_start << " ms, background "
            << ( ripple_bg != None ) << endl;
    if( ripple_bg == None || now() - ripple_start >= RippleTime )
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
    GC gc = XCreateGC( dpy, ripple_window, 0, NULL );
    XCopyArea( dpy, ripple_bg, ripple_back, gc, 0, 0, box.width(), box.height(), 0, 0 );
    bool more = false;
    Render::Renderer* rend = renderer();
    Render::Texture* t = rend != NULL ? rend->textureFromPixmap( ripple_back, box.width(), box.height(),
        DefaultDepth( dpy, DefaultScreen( dpy )), DefaultVisual( dpy, DefaultScreen( dpy ))) : NULL;
    if( t != NULL )
        {
        rr = rend;
        rr->setTarget( t );
        rr->clearClip();
        more = drawRipples( now(), box.x(), box.y());
        rr->finish();
        delete t;
        }
    if( !more )
        {
        XFreeGC( dpy, gc );
        stopRipple();
        return;
        }
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
        for( OverviewItem* item = items.first();
             item != NULL;
             item = items.next())
            item->restored = false; // a normal window now
        was_minimized.clear();
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
    // with KWin's compositor: in its frames (not over the screen locker,
    // which it shows directly)
    Compositor* compositor = ws->compositor();
    comp = compositor != NULL && compositor->isActive();
    if( comp && compositor->lockerShown())
        return;
    rr = renderer();
    if( rr == NULL )
        {
        kdWarning( 1212 ) << "overview: no renderer" << endl;
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
    if( comp )
        compositor->setUnredirectBlocked( true ); // (all windows in its textures)
    else if( composite )
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
        r->snapshot_tex = rr->textureFromPixmap( r->snapshot, r->width, r->height,
            DefaultDepth( dpy, DefaultScreen( dpy )), DefaultVisual( dpy, DefaultScreen( dpy )));
        }

    if( !comp )
        presenter->start( overlay, 0, 0, r->width, r->height, composite && have_present );

    buildItems( true );
    computeLayout();
    updateTitles();

    // frame 0: everything where it is (the minimized windows invisible)
    trans_from = trans_to = progress = 0;
    trans_duration = 0;
    for( OverviewItem* item = items.first();
         item != NULL;
         item = items.next())
        item->from = item->to = item->cur = restRect( item );
    state = Settling;
    renderFrame();
    if( own_overlay )
        XMapRaised( dpy, overlay ); // after the first frame is in the buffer
    // wait for the repaints caused by a new redirection before moving anything
    settle_start = last_damage = now();
    if( !temp_redirected )
        settle_start = last_damage = settle_start - 1000; // nothing to wait for
    if( !comp ) // (composited: once frame 0 is drawn, see frameDone())
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
                if( item->in_layout && item->hasContents())
                    updateMips( item, mipLevel( double( item->slot.width()) / item->mip_w[ 0 ] ));
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
    // (restored along with c, see buildItems())
    was_minimized.clear();
    const ClientList& clients = ws->stackingOrder();
    for( ClientList::ConstIterator it = clients.begin();
         it != clients.end();
         ++it )
        if( (*it)->isMinimized())
            was_minimized.append( *it );
    if( c != NULL && ws->hasClient( c ))
        {
        if( !c->isOnCurrentDesktop())
            { // moved to another desktop meanwhile: switching can't be animated
            abort();
            ws->activateClient( c, true );
            return;
            }
        // a dialog of a minimized window (asking before closing it): with
        // the window, not alone (it is restored along with it)
        if( c->isMinimized() && c->isTransient())
            {
            ClientList mains = c->mainClients();
            for( ClientList::ConstIterator it = mains.begin();
                 it != mains.end();
                 ++it )
                if( (*it)->isMinimized())
                    (*it)->unminimize();
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
    // (the textures before the renderer: the compositor's may be stopping)
    delete r->snapshot_tex;
    if( r->snapshot != None )
        XFreePixmap( dpy, r->snapshot );
    delete r->filter_tex;
    delete r->close_tex[ 0 ];
    delete r->close_tex[ 1 ];
    delete r->badge_tex;
    for( QMap< int, OverviewRenderer::RoundMask >::ConstIterator it = r->round_masks.begin();
         it != r->round_masks.end();
         ++it )
        {
        delete (*it).full;
        delete (*it).col;
        delete (*it).row;
        }
    if( comp )
        {
        Compositor* c = ws->compositor();
        if( c != NULL )
            { // (its scene again, as the last frame showed it)
            c->setUnredirectBlocked( false );
            c->addRepaintFull();
            if( ripple_start > 0 )
                c->requestFrame(); // (the ripples on above the scene)
            }
        }
    else if( ripple_start > 0 )
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
    target = NULL;
    frame_clip = QRegion();
    ungrabInput();
    state = Hidden;
    selected = pressed = dragged = hovered = pressed_close = NULL;
    close_hover = false;
    closing.clear();
    was_minimized.clear();
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

/*
 The minimized windows shown (ShowMinimized): those the taskbar shows, i.e.
 not skipping the taskbar or the pager; a dialog by the rule of its main
 window, and not when that is minimized too (it is restored along with
 it, like the taskbar shows only the main window).
*/
static bool minimizedInOverview( const Client* c )
    {
    if( !c->isMinimized() || !c->isOnCurrentDesktop() || c->isSpecialWindow() || c->isTopMenu()
        || !( c->isNormalWindow() || c->isDialog()))
        return false;
    if( c->isTransient())
        {
        ClientList mains = c->mainClients();
        if( !mains.isEmpty())
            {
            bool shown = false;
            for( ClientList::ConstIterator it = mains.begin();
                 it != mains.end();
                 ++it )
                {
                if( (*it)->isMinimized())
                    return false;
                if( !(*it)->skipTaskbar() && !(*it)->skipPager())
                    shown = true;
                }
            return shown;
            }
        }
    return !c->skipTaskbar() && !c->skipPager();
    }

// where an item is at the real screen (progress 0): a minimized window
// smaller in its slot (invisible there)
static RectF restRect( const OverviewItem* item )
    {
    if( !item->minimized )
        return RectF( item->geom );
    const RectF s( item->slot );
    return RectF( s.cx() - s.w * MinimizedRest / 2, s.cy() - s.h * MinimizedRest / 2,
        s.w * MinimizedRest, s.h * MinimizedRest );
    }

// a large icon for the card of a minimized window without contents
static QPixmap largeIcon( Client* c )
    {
    QPixmap pm = KWin::icon( c->window(), PlaceholderIcon, PlaceholderIcon, true, KWin::NETWM | KWin::WMHints );
    if( pm.isNull())
        pm = c->icon();
    return pm;
    }

OverviewItem* Overview::createItem( Window w, const XWindowAttributes& attr )
    {
    OverviewItem* item = new OverviewItem;
    item->id = w;
    item->geom = QRect( attr.x, attr.y, attr.width + 2 * attr.border_width, attr.height + 2 * attr.border_width );
    item->depth = attr.depth;
    item->format = XRenderFindVisualFormat( dpy, attr.visual );
    item->visual = attr.visual;
    item->alpha_format = item->format != NULL && item->format->type == PictTypeDirect
        && item->format->direct.alphaMask != 0;
    item->client = ws->findClient( FrameIdMatchPredicate( w ));
    if( comp && ws->compositor() != NULL )
        item->opacity = ws->compositor()->windowOpacity( w );
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

/*
 A minimized window: its frame is not mapped (and isn't mapped for this),
 its contents are those the window animations kept when it was minimized
 (Effects::retainedContents()), else it is a card with its icon (minimized
 before KWin started, the kept contents dropped for the memory limit, the
 animations off).  Its size is that of the frame, i.e. the last one.
*/
OverviewItem* Overview::createMinimizedItem( Client* c )
    {
    XWindowAttributes attr;
    if( !XGetWindowAttributes( dpy, c->frameId(), &attr ) || attr.width <= 0 || attr.height <= 0 )
        return NULL;
    OverviewItem* item = createItem( c->frameId(), attr );
    item->client = c;
    item->kind = OverviewItem::Managed;
    item->in_layout = true;
    item->minimized = true;
#ifdef OVERVIEW_COMPOSITE
    Pixmap pixmap;
    QSize size;
    int depth;
    Visual* visual;
    if( composite && ws->windowEffects() != NULL
        && ws->windowEffects()->retainedContents( c, &pixmap, &size, &depth, &visual ))
        {
        XRenderPictFormat* format = XRenderFindVisualFormat( dpy, visual );
        if( format != NULL && format->depth == depth && renderer() != NULL )
            { // (the texture keeps the contents when Effects frees the pixmap)
            item->format = format;
            item->visual = visual;
            item->depth = depth;
            item->alpha_format = format->type == PictTypeDirect && format->direct.alphaMask != 0;
            item->mip[ 0 ] = renderer()->textureFromPixmap( pixmap, size.width(), size.height(), depth, visual );
            item->mip_w[ 0 ] = size.width();
            item->mip_h[ 0 ] = size.height();
            item->kept = item->mip[ 0 ] != NULL;
            }
        }
    // (now: the window may paint as soon as it is restored; an unmapped
    // window reports nothing)
    if( composite )
        item->damage = XDamageCreate( dpy, item->id, XDamageReportNonEmpty );
#endif
    if( composite && !item->kept )
        {
        item->placeholder = true;
        QPixmap icon = largeIcon( c );
        item->icon_tex = imageTexture( renderer(), icon );
        item->icon_w = icon.width();
        item->icon_h = icon.height();
        }
    if( debug )
        kdWarning( 1212 ) << "overview: minimized " << c->caption() << ( item->kept ? " (kept contents)" : "" ) << endl;
    return item;
    }

// a window restored meanwhile has painted: its live contents from now on
void Overview::makeLive( OverviewItem* item )
    {
    item->pending_live = false;
    if( !item->kept && !item->placeholder )
        return; // (they are already, it was waited for them to be painted)
    if( debug )
        kdWarning( 1212 ) << "overview: live contents of " << item->caption << endl;
    trapErrors( dpy );
    releasePictures( item );
    item->kept = item->placeholder = false;
    XWindowAttributes attr;
    if( XGetWindowAttributes( dpy, item->id, &attr ) && attr.map_state == IsViewable )
        {
        item->depth = attr.depth;
        item->format = XRenderFindVisualFormat( dpy, attr.visual );
        item->visual = attr.visual;
        item->alpha_format = item->format != NULL && item->format->type == PictTypeDirect
            && item->format->direct.alphaMask != 0;
        bindContents( item, false );
        }
    untrapErrors( dpy );
    }

// the contents of the window: its redirected pixmap, or a copy of the screen
// (only while the screen still shows the real windows); false when neither
bool Overview::bindContents( OverviewItem* item, bool can_copy )
    {
#ifdef OVERVIEW_COMPOSITE
    if( !composite || item->format == NULL || item->hasContents() || item->kept || item->placeholder )
        return true;
    if( comp )
        { // the compositor's contents, asked for in every frame
        Compositor* c = ws->compositor();
        if( c->windowTexture( item->id, NULL ) == NULL )
            return false;
        item->external = true;
        item->mip_w[ 0 ] = item->geom.width();
        item->mip_h[ 0 ] = item->geom.height();
        for( int i = 1;
             i < MipLevels;
             ++i )
            item->mip_dirty[ i ] = true;
        if( item->damage == None )
            item->damage = XDamageCreate( dpy, item->id, XDamageReportNonEmpty );
        return true;
        }
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
        item->visual = DefaultVisual( dpy, DefaultScreen( dpy )); // (r->format's)
        item->depth = depth;
        item->alpha_format = false;
        item->snapshot = true;
        }
    item->mip[ 0 ] = renderer()->textureFromPixmap( item->pixmap, item->geom.width(), item->geom.height(),
        item->depth, item->visual );
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
    releasePictures( item );
#ifdef OVERVIEW_COMPOSITE
    if( item->damage != None )
        XDamageDestroy( dpy, item->damage );
#endif
    item->damage = None;
    delete item->title_tex;
    item->title_tex = NULL;
    delete item->icon_tex;
    item->icon_tex = NULL;
    }

// the contents (a kept pixmap of the window animations is not ours)
void Overview::releasePictures( OverviewItem* item )
    {
    for( int i = 0;
         i < MipLevels;
         ++i )
        {
        if( i > 0 || !item->external )
            delete item->mip[ i ];
        item->mip[ i ] = NULL;
        }
    item->external = false;
    if( item->pixmap != None )
        XFreePixmap( dpy, item->pixmap );
    item->pixmap = None;
    for( int i = 0;
         i < MipLevels;
         ++i )
        {
        item->mip_dirty[ i ] = true;
        item->mip_w[ i ] = item->mip_h[ i ] = 0;
        }
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
            if( w == overlay || ( comp && ws->compositor() != NULL && w == ws->compositor()->overlayWindow()))
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
            if( item != NULL && item->minimized )
                { // restored (picked in the overview, or by itself): zooms in
                  // to its place with the kept contents until it has painted
                item->minimized = false;
                item->restored = true;
                item->pending_live = composite; // (else icons, nothing to wait for)
                item->painted = 0;
#ifdef OVERVIEW_COMPOSITE
                if( composite && item->damage == None )
                    item->damage = XDamageCreate( dpy, item->id, XDamageReportNonEmpty );
#endif
                if( debug )
                    kdWarning( 1212 ) << "overview: restored " << item->caption << endl;
                }
            if( item != NULL )
                {
                QRect geom( attr.x, attr.y, attr.width + 2 * attr.border_width, attr.height + 2 * attr.border_width );
                if( geom.size() != item->geom.size() && !item->snapshot && !item->kept && !item->placeholder )
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
                else if( geom.size() != item->geom.size() && ( item->kept || item->placeholder ))
                    { // (the kept contents scaled) the new shape
                    OverviewItem* fresh = createItem( w, attr );
                    item->shaped = fresh->shaped;
                    item->shape = fresh->shape;
                    delete fresh;
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
                // restored along with the picked window: not drawn until painted
                if( composite && !initial && item->client != NULL && was_minimized.contains( item->client ))
                    item->pending_live = true;
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
    // the minimized windows (not mapped), above the desktop below the others
    QPtrList< OverviewItem > minimized;
    if( initial && options->overviewShowMinimized )
        {
        const ClientList& clients = ws->stackingOrder();
        for( ClientList::ConstIterator it = clients.begin();
             it != clients.end();
             ++it )
            if( minimizedInOverview( *it ))
                {
                OverviewItem* item = createMinimizedItem( *it );
                if( item != NULL )
                    minimized.append( item );
                }
        }
    else
        {
        for( OverviewItem* item = items.first();
             item != NULL;
             item = items.next())
            if( item->minimized && !item->seen && item->client != NULL && ws->hasClient( item->client )
                && item->client->isMinimized() && item->client->isOnCurrentDesktop())
                {
                XWindowAttributes attr;
                if( item->client->geometry().size() != item->geom.size()
                    && XGetWindowAttributes( dpy, item->id, &attr ))
                    { // resized when moved to another monitor: the new shape
                    OverviewItem* fresh = createItem( item->id, attr );
                    item->shaped = fresh->shaped;
                    item->shape = fresh->shape;
                    delete fresh;
                    }
                item->geom = item->client->geometry(); // (moved to another monitor)
                item->seen = true;
                minimized.append( item );
                }
        }
    if( !minimized.isEmpty())
        {
        int pos = 0;
        for( OverviewItem* item = list.first();
             item != NULL && item->kind == OverviewItem::Desktop;
             item = list.next())
            ++pos;
        for( OverviewItem* item = minimized.first();
             item != NULL;
             item = minimized.next())
            list.insert( pos++, item );
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
    was_minimized.remove( c );
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
        delete item->title_tex;
        item->title_tex = NULL;
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
        item->title_tex = alphaTexture( renderer(), item->title_pixmap );
        if( !composite && item->icon_tex == NULL )
            {
            QPixmap icon = item->client->icon();
            item->icon_tex = imageTexture( renderer(), icon );
            item->icon_w = icon.width();
            item->icon_h = icon.height();
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
    delete r->filter_tex;
    r->filter_tex = NULL;
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
        r->filter_tex = alphaTexture( renderer(), r->filter_pixmap );
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
            item->to = restRect( item );
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
    if( comp )
        { // (a frame of the compositor)
        if( state != Finishing && ws->compositor() != NULL )
            ws->compositor()->requestFrame();
        return;
        }
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
    if( rr == NULL )
        return;
    bool drawn = false;
    for( int l = 1;
         l <= level;
         ++l )
        {
        if( item->mip[ l ] != NULL && !item->mip_dirty[ l ] )
            continue;
        int w = QMAX( 1, ( item->mip_w[ l - 1 ] + 1 ) / 2 );
        int h = QMAX( 1, ( item->mip_h[ l - 1 ] + 1 ) / 2 );
        if( item->mip[ l ] == NULL )
            {
            item->mip[ l ] = rr->createOffscreen( w, h, item->alpha_format );
            if( item->mip[ l ] == NULL )
                break; // (the target goes back to the frame below)
            item->mip_w[ l ] = w;
            item->mip_h[ l ] = h;
            }
        Render::Texture* src = mipTexture( item, l - 1 );
        if( src == NULL )
            break;
        // exactly 2x2 boxes: bilinear sampling between four pixels
        rr->clearClip();
        rr->setTarget( item->mip[ l ] );
        drawn = true;
        rr->drawTexture( src, RectF( 0, 0, 2 * w, 2 * h ), RectF( 0, 0, w, h ), 1.0, Render::BilinearFilter,
            Render::BlendSource, Render::PadRepeat );
        made_copy = true;
        item->mip_dirty[ l ] = false;
        if( l + 1 < MipLevels )
            item->mip_dirty[ l + 1 ] = true;
        }
    if( drawn && target != NULL )
        { // (back to the frame)
        rr->setTarget( target );
        resetClip();
        }
    }

// the contents at a mip level (0: the window's, the compositor's when external)
Render::Texture* Overview::mipTexture( OverviewItem* item, int level )
    {
    if( level == 0 && item->external )
        {
        Compositor* c = ws->compositor();
        QRect g;
        Render::Texture* t = c != NULL ? c->windowTexture( item->id, &g ) : NULL;
        if( t != NULL && ( g.width() != item->mip_w[ 0 ] || g.height() != item->mip_h[ 0 ] ))
            return NULL; // (resized: buildItems() takes the new size)
        return t;
        }
    return item->mip[ level ];
    }

Render::Renderer* Overview::renderer()
    {
    if( comp )
        {
        Compositor* c = ws->compositor();
        return c != NULL && c->isActive() ? c->renderer() : NULL;
        }
    if( own_renderer == NULL )
        own_renderer = Render::Renderer::create( dpy, Render::Renderer::XRender, NULL );
    return own_renderer;
    }

// the frame's clip again (after a clip of a window's shape)
void Overview::resetClip()
    {
    if( frame_clip.isNull())
        rr->clearClip();
    else
        rr->setClip( frame_clip );
    }

void Overview::renderFrame()
    {
    if( r == NULL )
        return;
    if( comp )
        { // (the compositor draws: a frame there)
        ws->compositor()->requestFrame();
        return;
        }
    // the buffer
    if( presenter->beginFrame() == None )
        { // all still used by the server, wait for an idle event
        frame_wanted = true;
        return;
        }
    Render::Renderer* rend = renderer();
    Render::Texture* t = rend != NULL ? rend->textureFromPixmap( presenter->framePixmap(), r->width, r->height,
        DefaultDepth( dpy, DefaultScreen( dpy )), DefaultVisual( dpy, DefaultScreen( dpy ))) : NULL;
    if( t == NULL )
        {
        abort();
        return;
        }
    const double t_start = now();
    // the time when this frame will be seen
    const double t0 = presenter->frameTime( t_start );
    bool more = advance( t_start, t0 );
    rr = rend;
    target = t;
    rr->setTarget( target );
    drawFrame( t0, state == Hiding && !more, &more );
    rr->finish();
    target = NULL;
    delete t;
    if( !dump_dir.isEmpty())
        dumpFrame();
    present();
    if( debug )
        {
        XSync( dpy, False );
        double t = now() - t_start;
        stat_render += t;
        stat_max_render = QMAX( stat_max_render, t );
        }
    last_frame = t_start;
    frameDone( t0, more );
    }

// after a frame (shown at t0; more: the next one differs)
void Overview::frameDone( double t0, bool more )
    {
    ++frame_count;
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
        if( comp )
            { // the windows back to the scene after this frame
            QTimer::singleShot( 0, this, SLOT( compositedFinished()));
            return;
            }
        if( !presenter->usesPresent() || presenter->queued() == 0 )
            {
            XSync( dpy, False );
            finish();
            }
        // else finish() after the completion of the last presentation
        return;
        }
    if( comp )
        {
        if( state == Settling && !settle_timer.isActive())
            settle_timer.start( 0 ); // (frame 0 drawn: the zoom-out starts)
        if( more )
            requestFrame();
        return;
        }
    if( more )
        frame_wanted = true;
    }

void Overview::compositedFinished()
    {
    if( comp && state == Finishing )
        finish();
    }

//****************************************
// in the compositor's frames
//****************************************

bool Overview::composited() const
    {
    return comp && ( r != NULL || ripple_start > 0 );
    }

bool Overview::replacesScene() const
    {
    return comp && r != NULL;
    }

QRegion Overview::prePaint( double t0 )
    {
    if( !comp )
        return QRegion();
    comp_t0 = t0;
    if( r == NULL )
        { // the ripples after the overview, above the scene
        if( ripple_start <= 0 )
            return QRegion();
        const QRegion box( rippleBox());
        if( t0 - ripple_start >= RippleTime )
            stopRipple(); // (this frame without them)
        else if( ws->compositor() != NULL )
            ws->compositor()->requestFrame();
        return box;
        }
    comp_more = advance( now(), t0 );
    comp_last = state == Hiding && !comp_more;
    return QRegion( 0, 0, r->width, r->height );
    }

void Overview::paintScreen( Render::Renderer* rend, Render::Texture* t, const QRegion& region )
    {
    if( !comp || r == NULL )
        return;
    const double t_start = now();
    rr = rend;
    target = t;
    frame_clip = region == QRegion( 0, 0, r->width, r->height ) ? QRegion() : region;
    rr->setTarget( target );
    resetClip();
    bool more = comp_more;
    drawFrame( comp_t0, comp_last, &more );
    rr->clearClip();
    target = NULL;
    frame_clip = QRegion();
    if( debug )
        {
        double d = now() - t_start;
        stat_render += d;
        stat_max_render = QMAX( stat_max_render, d );
        }
    last_frame = t_start;
    frameDone( comp_t0, more );
    }

void Overview::paintOverlay( Render::Renderer* rend, Render::Texture* t, const QRegion& region )
    {
    if( !comp || r != NULL || ripple_start <= 0 )
        return;
    rr = rend;
    target = t;
    rr->setTarget( target );
    frame_clip = region & QRegion( rippleBox());
    resetClip();
    drawRipples( comp_t0, 0, 0 );
    rr->clearClip();
    target = NULL;
    frame_clip = QRegion();
    }

void Overview::compositorStopping()
    {
    if( !comp )
        return;
    abort();
    stopRipple();
    comp = false;
    rr = NULL;
    }


// the state at time t0 (the frame seen then); true while something moves
bool Overview::advance( double t_start, double t0 )
    {
    // the transition
    double lin = trans_duration > 0 ? clamp01(( t0 - trans_start ) / trans_duration ) : 1.0;
    double e = easeOutQuad( lin );
    progress = mix( trans_from, trans_to, e );
    // the restored windows that have painted (and stopped, or the zoom-in
    // can't wait longer) show their live contents
    const double restore_deadline = trans_start + trans_duration + RestoreWait;
    bool pending = false;
    bool pending_painted = false;
    for( OverviewItem* item = items.first();
         item != NULL;
         item = items.next())
        {
        if( item->pending_live && item->painted > 0 && ( t_start - item->painted >= RestoreQuiet
            || state != Hiding || t0 >= restore_deadline ))
            makeLive( item );
        if( item->pending_live )
            {
            pending = true;
            if( item->painted > 0 )
                pending_painted = true; // (live soon: a frame then)
            }
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
    // the last frame of the zoom-in shows the restored windows painted
    if( pending && ( pending_painted || t0 < restore_deadline ) && state == Hiding )
        more = true;
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
    return more;
    }

// the whole frame into the renderer's target (last: the last one of the zoom-in)
void Overview::drawFrame( double t0, bool last, bool* more )
    {
    // the background
    bool have_desktop = false;
    rr->clearClip();
    if( r->snapshot_tex != NULL )
        {
        rr->drawTexture( r->snapshot_tex, 0, 0, 1.0, Render::BlendSource );
        have_desktop = true;
        }
    else
        {
        for( OverviewItem* item = items.first();
             item != NULL;
             item = items.next())
            if( item->kind == OverviewItem::Desktop )
                have_desktop = true;
        if( !have_desktop && comp )
            { // the compositor's background (the root window's)
            ws->compositor()->drawBackground( frame_clip.isNull() ? QRegion( 0, 0, r->width, r->height ) : frame_clip );
            resetClip();
            }
        else if( !have_desktop )
            rr->fill( RectF( 0, 0, r->width, r->height ), Qt::black, 1.0, Render::BlendSource );
        }
    const double dim = progress * ( composite ? DimAlpha : 0.75 );
    // a desktop window covering the screen is drawn dimmed, no full screen
    // blend for the dimming
    desktop_dim = 0;
    OverviewItem* first = items.first();
    if( composite && dim > 1.0 / 512 && first != NULL && first->kind == OverviewItem::Desktop
        && first->geom == QRect( 0, 0, r->width, r->height ) && !first->shaped && !first->alpha_format
        && first->hasContents())
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
            drawBackdrop( dim, t0 );
            dimmed = true;
            }
        if( item != dragged )
            drawItem( item );
        }
    if( !dimmed )
        drawBackdrop( dim, t0 );

    // the titles
    if( title_alpha > 1.0 / 512 )
        {
        for( OverviewItem* item = items.first();
             item != NULL;
             item = items.next())
            {
            if( !item->in_layout || item->title_tex == NULL || item == dragged )
                continue;
            double a = title_alpha * item->fade;
            int w = item->title_pixmap.width();
            int h = item->title_pixmap.height();
            int x = (int) floor( item->cur.cx() - w / 2.0 + 0.5 );
            int y = (int) floor( item->cur.y + item->cur.h + TitleGap + 0.5 );
            fillRounded( rr, r, RectF( x, y, w, h ), h / 2.0, QColor( 24, 24, 24 ), 0.78 * a );
            rr->fillMasked( RectF( x, y, w, h ), Qt::white, a, item->title_tex, RectF( 0, 0, w, h ));
            }
        }
    if( title_alpha > 1.0 / 512 )
        for( OverviewItem* item = items.first();
             item != NULL;
             item = items.next())
            if( item->in_layout && item->minimized && item != dragged )
                drawMinimizedBadge( item );
    if( title_alpha > 1.0 / 512 && dragged == NULL )
        for( OverviewItem* item = items.first();
             item != NULL;
             item = items.next())
            if( item->in_layout && item->close_fade > 1.0 / 512 )
                drawCloseButton( item );
    if( dragged != NULL )
        drawItem( dragged );
    if( r->filter_tex != NULL && progress > 0.5 && state != Hiding && state != Finishing )
        {
        QDesktopWidget* desktop = QApplication::desktop();
        QRect sg = desktop->screenGeometry( desktop->primaryScreen());
        QRect area = ws->clientArea( PlacementArea, sg.center(), ws->currentDesktop()).intersect( sg );
        int w = r->filter_pixmap.width();
        int h = r->filter_pixmap.height();
        int x = area.x() + ( area.width() - w ) / 2;
        int y = area.y() + 12;
        fillRounded( rr, r, RectF( x, y, w, h ), h / 2.0, QColor( 24, 24, 24 ), 0.85 );
        rr->fillMasked( RectF( x, y, w, h ), Qt::white, 1.0, r->filter_tex, RectF( 0, 0, w, h ));
        }

    if( ripple_start > 0 )
        {
        if( last && !comp )
            saveRippleBackground(); // the last frame: the screen when the overlay goes
        if( drawRipples( t0, 0, 0 ) && !last )
            *more = true;
        }
    }

// the dimmed background over the desktop, the monitor a thumbnail is
// dragged to
void Overview::drawBackdrop( double dim, double t )
    {
    if( dim > 1.0 / 512 && desktop_dim == 0 )
        rr->fill( RectF( 0, 0, r->width, r->height ), Qt::black, dim );
    if( dragged != NULL && drag_target >= 0 )
        {
        double a = clamp01(( t - drag_target_start ) / FadeTime );
        RectF area( layoutArea( drag_target ));
        fillRounded( rr, r, RectF( area.x + 12, area.y + 12, area.w - 24, area.h - 24 ), 16,
            r->highlight, 0.18 * a );
        }
    }

void Overview::drawItem( OverviewItem* item )
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
                if( item->minimized )
                    alpha *= clamp01( progress ); // fades in at its place
                scaled = true;
                }
            else
                alpha = 1.0 - progress;
            break;
        }
    if( alpha <= 1.0 / 512 )
        return;
    if( item->pending_live && !item->kept && !item->placeholder )
        return; // mapped meanwhile (restored along), not painted yet
    // minimized windows darkened (until restored at the end of the zoom-in)
    const double dim = item->minimized || ( item->restored && state == Hiding )
        ? MinimizedDim * clamp01( progress ) : 0.0;
    // composited: the window's own shadow, scaled with it, fading out while
    // zooming out (frame 0 and the last frame are the compositor's scene)
    if( comp && !item->minimized && !item->placeholder && !item->kept )
        {
        Compositor* c = ws->compositor();
        CompositorShadow s;
        const double sa = alpha * ( scaled ? 1.0 - clamp01( progress ) : 1.0 );
        if( sa > 1.0 / 512 && c->windowShadow( item->id, &s ) && s.valid )
            {
            const double fx = rect.w / item->geom.width();
            const double fy = rect.h / item->geom.height();
            QRegion shape;
            if( item->shaped )
                for( unsigned int i = 0;
                     i < item->shape.size();
                     ++i )
                    {
                    const XRectangle& sr = item->shape[ i ];
                    const int x1 = (int) floor( rect.x + sr.x * fx + 0.5 );
                    const int y1 = (int) floor( rect.y + sr.y * fy + 0.5 );
                    shape |= QRegion( x1, y1, (int) floor( rect.x + ( sr.x + sr.width ) * fx + 0.5 ) - x1,
                        (int) floor( rect.y + ( sr.y + sr.height ) * fy + 0.5 ) - y1 );
                    }
            else
                shape = QRegion( rect.bounds());
            QRegion clip = ( frame_clip.isNull() ? QRegion( 0, 0, r->width, r->height ) : frame_clip ) - shape;
            if( !clip.isEmpty())
                c->drawShadow( rr, s, rect, sa, clip );
            resetClip();
            }
        }
    if( scaled && progress > 0.001 )
        { // a soft shadow and the selection (not below an opaque window)
        double a = QMIN( 1.0, progress ) * alpha;
        const bool hollow = composite && alpha >= 1.0 && !item->alpha_format && !item->shaped
            && item->hasContents() && item->opacity >= 1.0;
        fillRounded( rr, r, RectF( rect.x - 3, rect.y - 1, rect.w + 6, rect.h + 7 ), 6, Qt::black, 0.10 * a, hollow );
        fillRounded( rr, r, RectF( rect.x - 1, rect.y, rect.w + 2, rect.h + 3 ), 3, Qt::black, 0.18 * a, hollow );
        if( item == selected && item != dragged && title_alpha > 0 )
            fillRounded( rr, r, RectF( rect.x - 8, rect.y - 8, rect.w + 16, rect.h + 16 ), 10,
                r->highlight, 0.75 * title_alpha * alpha, hollow );
        }
    if( !composite )
        { // icons on tiles
        if( !item->in_layout )
            return;
        fillRounded( rr, r, rect, 8, QColor( 64, 64, 64 ).dark( 100 + int( dim * 100 )), 0.95 * alpha );
        drawIcon( rr, item, rect, 64.0, 0.6, alpha );
        return;
        }
    if( item->placeholder )
        {
        drawPlaceholder( item, rect, alpha );
        return;
        }
    if( !item->hasContents())
        return;
    // the source: the mip level that is downscaled at most by half
    int level = mipLevel( rect.w / item->mip_w[ 0 ] );
    updateMips( item, level );
    Render::Texture* src = NULL;
    while( level >= 0 && ( src = mipTexture( item, level )) == NULL )
        --level;
    if( src == NULL )
        return;
    const double sw = item->mip_w[ level ];
    const double sh = item->mip_h[ level ];
    const int dx = (int) floor( rect.x );
    const int dy = (int) floor( rect.y );
    const bool identity = level == 0 && fabs( rect.w - sw ) < 1e-9 && fabs( rect.h - sh ) < 1e-9
        && rect.x == dx && rect.y == dy;
    if( item->shaped )
        {
        QRegion clip;
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
            if( x2 > x1 && y2 > y1 )
                clip |= QRegion( x1, y1, x2 - x1, y2 - y1 );
            }
        if( !frame_clip.isNull())
            clip &= frame_clip;
        rr->setClip( clip );
        }
    const double opacity = alpha * item->opacity;
    // at scale 1 like the server's automatic compositing (Source, also for
    // ARGB windows; composited: like the compositor, which blends those),
    // else blended (the edges are anti-aliased)
    Render::Blend blend = opacity < 1.0 || ( item->alpha_format && ( !identity || comp ))
        ? Render::BlendOver : Render::BlendSource;
    if( item->kind == OverviewItem::Desktop && desktop_dim > 0 )
        blend = Render::BlendSource; // the background dimmed
    if( identity )
        rr->drawTexture( src, RectF( 0, 0, sw, sh ), RectF( dx, dy, sw, sh ), opacity, Render::NearestFilter, blend );
    else
        rr->drawTexture( src, RectF( 0, 0, sw, sh ), rect, opacity, Render::BilinearFilter, blend );
    if( dim * alpha > 1.0 / 512 )
        { // (inside the shape; the edge pixels a little darker while moving)
        const int dw = (int) ceil( rect.x + rect.w ) - dx;
        const int dh = (int) ceil( rect.y + rect.h ) - dy;
        rr->fill( RectF( dx, dy, dw, dh ), Qt::black, dim * alpha );
        }
    if( item->shaped )
        resetClip();
    }

// the icon of the item centered in rect, at most max_size large and
// fraction of the rectangle
static void drawIcon( Render::Renderer* rr, OverviewItem* item, const RectF& rect, double max_size,
    double fraction, double alpha )
    {
    if( item->icon_tex == NULL || item->icon_w <= 0 || item->icon_h <= 0 )
        return;
    double size = QMIN( max_size, QMIN( rect.w, rect.h ) * fraction );
    if( size < 1 )
        return;
    double s = size / QMAX( item->icon_w, item->icon_h );
    double iw = item->icon_w * s;
    double ih = item->icon_h * s;
    int x = (int) floor( rect.cx() - iw / 2 );
    int y = (int) floor( rect.cy() - ih / 2 );
    rr->drawTexture( item->icon_tex, RectF( 0, 0, item->icon_w, item->icon_h ), RectF( x, y, iw, ih ), alpha,
        Render::BilinearFilter, Render::BlendOver );
    }

// a minimized window without contents: a card with its icon (its title
// below it like for all others)
void Overview::drawPlaceholder( OverviewItem* item, const RectF& rect, double alpha )
    {
    fillRounded( rr, r, rect, 10, QColor( 58, 58, 62 ), alpha );
    fillRounded( rr, r, RectF( rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2 ), 9,
        QColor( 255, 255, 255 ), 0.04 * alpha );
    drawIcon( rr, item, rect, PlaceholderIcon, 0.45, alpha );
    }

// a small ARGB texture (transparent) as the target, for drawing a shape
// into it once; endOffscreen() goes back to the frame
Render::Texture* Overview::offscreenShape( int size )
    {
    Render::Texture* t = rr->createOffscreen( size, size, true );
    if( t == NULL )
        return NULL;
    rr->clearClip();
    rr->setTarget( t );
    rr->fill( RectF( 0, 0, size, size ), Qt::black, 0.0, Render::BlendSource );
    return t;
    }

void Overview::endOffscreen()
    {
    made_copy = true;
    if( target != NULL )
        rr->setTarget( target );
    resetClip();
    }

// the badge of a minimized window: in the top left corner, like the close
// button in the top right one
void Overview::drawMinimizedBadge( OverviewItem* item )
    {
    const int size = CloseSize + 2;
    if( r->badge_tex == NULL )
        { // made once: a dark circle with a light rim and a white bar
        r->badge_tex = offscreenShape( size );
        if( r->badge_tex == NULL )
            return;
        roundedRect( rr, RectF( 1, 1, CloseSize, CloseSize ), CloseSize / 2.0, Qt::white, 0.35 );
        roundedRect( rr, RectF( 2, 2, CloseSize - 2, CloseSize - 2 ), CloseSize / 2.0 - 1, QColor( 40, 40, 40 ), 1.0 );
        const double c = size / 2.0;
        const double arm = CloseSize * 0.2;
        roundedRect( rr, RectF( c - arm, c + arm * 0.35, 2 * arm, 2.4 ), 1.2, Qt::white, 1.0 );
        endOffscreen();
        }
    const double a = title_alpha * item->fade;
    if( a <= 1.0 / 512 )
        return;
    const QPoint center( (int) floor( item->cur.x + CloseInset + 0.5 ), (int) floor( item->cur.y + CloseInset + 0.5 ));
    rr->drawTexture( r->badge_tex, center.x() - size / 2, center.y() - size / 2, a );
    }

// the center of the close button of the thumbnail
static QPoint closeCenter( const RectF& rect )
    {
    return QPoint( (int) floor( rect.x + rect.w - CloseInset + 0.5 ), (int) floor( rect.y + CloseInset + 0.5 ));
    }

void Overview::drawCloseButton( OverviewItem* item )
    {
    const int size = CloseSize + 2;
    if( r->close_tex[ 0 ] == NULL )
        { // made once: a dark circle with a light rim and a white X
        for( int i = 0;
             i < 2;
             ++i )
            {
            r->close_tex[ i ] = offscreenShape( size );
            if( r->close_tex[ i ] == NULL )
                break;
            const double c = size / 2.0;
            roundedRect( rr, RectF( 1, 1, CloseSize, CloseSize ), CloseSize / 2.0, Qt::white, 0.35 );
            roundedRect( rr, RectF( 2, 2, CloseSize - 2, CloseSize - 2 ), CloseSize / 2.0 - 1,
                i == 0 ? QColor( 40, 40, 40 ) : QColor( 96, 96, 96 ), 1.0 );
            const double arm = CloseSize * 0.18;
            AlphaMask cross( size, size );
            cross.add( InCross( InLine( c - arm, c - arm, c + arm, c + arm, 2.2 ),
                InLine( c - arm, c + arm, c + arm, c - arm, 2.2 )), 0, 0 );
            Render::Texture* m = cross.texture( rr );
            if( m != NULL )
                rr->fillMasked( RectF( 0, 0, size, size ), Qt::white, 1.0, m, RectF( 0, 0, size, size ));
            delete m;
            }
        endOffscreen();
        if( r->close_tex[ 0 ] == NULL || r->close_tex[ 1 ] == NULL )
            { // both or none (made again next time)
            delete r->close_tex[ 0 ];
            delete r->close_tex[ 1 ];
            r->close_tex[ 0 ] = r->close_tex[ 1 ] = NULL;
            return;
            }
        }
    const QPoint center = closeCenter( item->cur );
    const double a = item->close_fade * title_alpha * item->fade;
    if( a <= 1.0 / 512 )
        return;
    rr->drawTexture( r->close_tex[ item == hovered && close_hover ? 1 : 0 ], center.x() - size / 2,
        center.y() - size / 2, a );
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
            if( item->pending_live )
                item->painted = now();
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
