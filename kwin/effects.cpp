/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2026 The KDE3 revival project

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

/*
 The window animations (see effects.h).

 A session starts with the first animation and ends after the last one:
 our window is mapped over the area of all animations (the union, it only
 grows during a session), the scene below it is read (the top-level
 windows in their stacking order, from their named pixmaps, each with a
 damage object) and every frame draws the scene with the animated windows
 in it.  The first frame is identical to the screen and copied at once,
 before the caller unmaps, maps or resizes the real window; the last one
 is the plain scene, identical to the screen again, and the window goes
 when that frame has been shown.

 Every frame is computed from the time elapsed since the start of the
 animation, never by accumulating per-frame steps, so slow frames make
 the animation choppier but not longer.

 What is drawn in every frame are composites of pictures with scaling
 transformations and bilinear filters and solid fills, which glamor runs
 on the GPU; no trapezoids, no polygons.  The genie is a stack of strips,
 each scaled on its own.
*/

#include <config.h>

#include "effects.h"

#include <qfile.h>
#include <qvaluevector.h>
#include <qapplication.h>
#include <qdesktopwidget.h>
#include <kdebug.h>

#include "workspace.h"
#include "client.h"
#include "options.h"
#include "overview.h"
#include "utils.h"
#include "compositing.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#if defined( HAVE_XRENDER ) && defined( HAVE_XCOMPOSITE ) && defined( HAVE_XDAMAGE ) && defined( HAVE_XFIXES )
#define EFFECTS_COMPOSITE
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#endif
#include <fixx11h.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace KWinInternal
{

#ifdef EFFECTS_COMPOSITE

using namespace Compositing;

namespace
{

const int MipLevels = 4; // 1, 1/2, 1/4, 1/8
// the first repaint of a window shown or resized: damage, then quiet for
// this long; or at most this long
const double QuietTime = 40.0;
const double MaxWait = 300.0;
// the new contents of a maximized window fade in
const double CrossfadeTime = 150.0;
// the pixmaps kept for restoring minimized windows (bytes)
const unsigned long RetainLimit = 384UL << 20;
// the genie: a strip's edges move at most this much (pixels) between its
// ends; strips at least this high
const double GenieStep = 2.0;
const int GenieMinStrip = 2;
// at most about this many strips (2 requests each, a draw call each with
// glamor): larger edge steps for large windows
const int GenieMaxStrips = 192;

// the mip level that is downscaled by at most a half
int mipLevel( double scale )
    {
    int level = 0;
    while( level < MipLevels - 1 && scale * ( 1 << ( level + 1 )) <= 1.0 )
        ++level;
    return level;
    }

inline double smoothstep( double t )
    {
    t = clamp01( t );
    return t * t * ( 3 - 2 * t );
    }

// the rectangle of size w x h scaled to fit into r, centered
RectF fitInto( double w, double h, const QRect& r )
    {
    double s = QMIN( r.width() / QMAX( 1.0, w ), r.height() / QMAX( 1.0, h ));
    return RectF( r.x() + ( r.width() - w * s ) / 2, r.y() + ( r.height() - h * s ) / 2, w * s, h * s );
    }

// shape rectangles relative to the window, scaled into rect, offset by -ox, -oy
QValueVector< XRectangle > scaledShape( const QValueVector< XRectangle >& shape, const QSize& size,
    const RectF& rect, int ox, int oy )
    {
    QValueVector< XRectangle > clip( shape.size());
    const double fx = rect.w / QMAX( 1, size.width());
    const double fy = rect.h / QMAX( 1, size.height());
    for( unsigned int i = 0;
         i < shape.size();
         ++i )
        {
        const XRectangle& sr = shape[ i ];
        int x1 = (int) floor( rect.x + sr.x * fx + 0.5 ) - ox;
        int y1 = (int) floor( rect.y + sr.y * fy + 0.5 ) - oy;
        int x2 = (int) floor( rect.x + ( sr.x + sr.width ) * fx + 0.5 ) - ox;
        int y2 = (int) floor( rect.y + ( sr.y + sr.height ) * fy + 0.5 ) - oy;
        clip[ i ].x = x1;
        clip[ i ].y = y1;
        clip[ i ].width = QMAX( 0, x2 - x1 );
        clip[ i ].height = QMAX( 0, y2 - y1 );
        }
    return clip;
    }

// the bounding shape of a frame, relative to it; false when it is a rectangle
bool readShape( Display* dpy, Window w, const QSize& size, QValueVector< XRectangle >& shape )
    {
    int count, ordering;
    XRectangle* rects = XShapeGetRectangles( dpy, w, ShapeBounding, &count, &ordering );
    bool shaped = false;
    if( rects != NULL )
        {
        if( !( count == 1 && rects[ 0 ].x <= 0 && rects[ 0 ].y <= 0
            && rects[ 0 ].width >= size.width() && rects[ 0 ].height >= size.height()))
            {
            shaped = true;
            shape.resize( count );
            for( int i = 0;
                 i < count;
                 ++i )
                shape[ i ] = rects[ i ];
            }
        XFree( rects );
        }
    return shaped;
    }

// a client whose frame may have a non-rectangular shape (saves round trips)
bool mayBeShaped( Client* c )
    {
    return Shape::available() && ( c == NULL || c->shape() || !c->mask().isEmpty());
    }

} // namespace

/*
 A picture with its downscaled copies (made when needed, for contents
 that don't change: the captured ones).
*/
class EffectPicture
    {
    public:
        EffectPicture() : format( NULL ), alpha( false ), depth( 0 ), visual( NULL ), owned( false )
            {
            for( int i = 0;
                 i < MipLevels;
                 ++i )
                {
                pixmap[ i ] = None;
                picture[ i ] = None;
                w[ i ] = h[ i ] = 0;
                }
            }
        bool isNull() const { return picture[ 0 ] == None; }
        // pixmap 0 is owned (a named pixmap) when owned
        void set( Display* dpy, Pixmap p, const QSize& size, int d, Visual* vis, bool own )
            {
            pixmap[ 0 ] = p;
            depth = d;
            visual = vis;
            owned = own;
            format = XRenderFindVisualFormat( dpy, vis );
            alpha = format != NULL && format->type == PictTypeDirect && format->direct.alphaMask != 0;
            w[ 0 ] = size.width();
            h[ 0 ] = size.height();
            if( format != NULL )
                picture[ 0 ] = XRenderCreatePicture( dpy, p, format, 0, NULL );
            }
        // releases everything; returns pixmap 0 when owned and keep is set
        Pixmap release( Display* dpy, bool keep = false )
            {
            Pixmap ret = None;
            for( int i = 0;
                 i < MipLevels;
                 ++i )
                {
                if( picture[ i ] != None )
                    XRenderFreePicture( dpy, picture[ i ] );
                picture[ i ] = None;
                if( pixmap[ i ] != None && ( i > 0 || owned ))
                    {
                    if( i == 0 && keep )
                        ret = pixmap[ 0 ];
                    else
                        XFreePixmap( dpy, pixmap[ i ] );
                    }
                pixmap[ i ] = None;
                }
            return ret;
            }
        // the level for drawing at this scale, made when needed
        int level( Display* dpy, double scale )
            {
            int level = mipLevel( scale );
            for( int l = 1;
                 l <= level;
                 ++l )
                {
                if( picture[ l ] != None )
                    continue;
                int lw = QMAX( 1, ( w[ l - 1 ] + 1 ) / 2 );
                int lh = QMAX( 1, ( h[ l - 1 ] + 1 ) / 2 );
                pixmap[ l ] = XCreatePixmap( dpy, DefaultRootWindow( dpy ), lw, lh, depth );
                picture[ l ] = XRenderCreatePicture( dpy, pixmap[ l ], format, 0, NULL );
                w[ l ] = lw;
                h[ l ] = lh;
                Picture src = picture[ l - 1 ];
                // exactly 2x2 boxes: bilinear sampling between four pixels
                XRenderPictureAttributes pa;
                pa.repeat = RepeatPad;
                XRenderChangePicture( dpy, src, CPRepeat, &pa );
                setTransform( dpy, src, 2, 2, 0, 0 );
                XRenderSetPictureFilter( dpy, src, FilterBilinear, NULL, 0 );
                XRenderComposite( dpy, PictOpSrc, src, None, picture[ l ], 0, 0, 0, 0, 0, 0, lw, lh );
                pa.repeat = RepeatNone;
                XRenderChangePicture( dpy, src, CPRepeat, &pa );
                }
            return level;
            }
        XRenderPictFormat* format;
        bool alpha;
        int depth;
        Visual* visual;
        bool owned;
        Pixmap pixmap[ MipLevels ];
        Picture picture[ MipLevels ];
        int w[ MipLevels ], h[ MipLevels ];
    };

class EffectAnim
    {
    public:
        enum Kind { Iconify, Morph, Open, Close };
        EffectAnim()
            : kind( Iconify ), client( NULL ), frame( None ), shaped( false ), keep_snap( false ),
              style( 1 ), p( 0 ), p_from( 0 ), p_to( 0 ), seg_start( 0 ), seg_duration( 0 ), linear( false ),
              defer( false ), wait( false ), damage( None ), wait_start( 0 ), damage_first( 0 ),
              damage_last( 0 ), ready( true ), fade( 0 ), fade_start( 0 ), on_top( false ), use_below( false ), below( None ),
              done( false ), fresh( true ), started( 0 )
            {}
        Kind kind;
        Client* client; // NULL when gone
        Window frame;
        QString caption;
        QRect geom; // the real geometry (followed while the client lives)
        QRect old_geom; // Morph: before
        QRect icon; // Iconify: the taskbar button
        EffectPicture snap; // the captured contents
        bool shaped;
        QValueVector< XRectangle > shape; // of the captured contents
        bool keep_snap; // given to the retained ones at the end (minimized)
        int style; // Iconify: Scale or Genie
        // the progress: Iconify 0 the window, 1 the icon; Morph 0 old, 1 new;
        // Open/Close 0 not shown, 1 shown
        double p;
        double p_from, p_to;
        double seg_start, seg_duration;
        bool linear; // no easing (the genie has its own)
        bool defer; // the motion starts when the window is ready
        // the real window after it was shown or resized: ready when repainted
        bool wait;
        XID damage;
        double wait_start, damage_first, damage_last;
        bool ready;
        double fade, fade_start; // Morph: the new contents fading in
        bool on_top;
        // drawn right above this scene window (None: the bottom one), where
        // its frame was when it started (closed, minimized below others)
        bool use_below;
        Window below;
        bool done;
        bool fresh; // its first frame not drawn yet: the time starts there
        double started;
    };

/*
 The windows below our window, bottom first.
*/
class EffectScene
    {
    public:
        struct Win
            {
            Win() : id( None ), client( NULL ), viewable( false ), redirected( false ), shaped( false ),
                damage( None ), seen( false ) {}
            Window id;
            QRect geom; // with the border
            Client* client;
            bool viewable;
            bool redirected;
            bool shaped;
            QValueVector< XRectangle > shape;
            EffectPicture pic;
            XID damage;
            bool seen;
            };
        EffectScene() : dirty( true ), ok( false ), root_picture( None ), limit( None )
            {
            wins.setAutoDelete( true );
            }
        Win* find( Window w )
            {
            for( Win* win = wins.first();
                 win != NULL;
                 win = wins.next())
                if( win->id == w )
                    return win;
            return NULL;
            }
        QPtrList< Win > wins;
        bool dirty;
        bool ok; // nothing below that can't be drawn
        Picture root_picture; // the root background (tiled), if any
        Window limit; // our window goes right above this one
    };

Effects::Effects( Workspace* w )
    : ws( w ),
      dpy( qt_xdisplay()),
      root( qt_xrootwin()),
      minimize_style( Scale ),
      maximize_anim( true ),
      open_close_anim( true ),
      base_time( 250 ),
      retained_bytes( 0 ),
      out( None ),
      scene( NULL ),
      presenter( NULL ),
      frame_wanted( false ),
      finishing( false ),
      last_frame( 0 ),
      frame_count( 0 ),
      dump_index( 0 ),
      stat_start( 0 ),
      stat_setup( 0 ),
      stat_render( 0 ),
      stat_max_render( 0 ),
      stat_skipped( 0 ),
      stat_gaps( 0 ),
      stat_strips( 0 )
    {
    anims.setAutoDelete( false );
    debug = getenv( "KWIN_EFFECTS_DEBUG" ) != NULL;
    dump_dir = QFile::decodeName( getenv( "KWIN_EFFECTS_DUMP" ));
    checkExtensions( dpy );
    presenter = new Presenter( dpy );
    connect( presenter, SIGNAL( completed( bool, int )), SLOT( presentCompleted( bool, int )));
    connect( presenter, SIGNAL( idle()), SLOT( presentIdle()));
    connect( &frame_timer, SIGNAL( timeout()), SLOT( frameTimeout()));
    connect( &poll_timer, SIGNAL( timeout()), SLOT( pollTimeout()));
    reconfigure();
    }

Effects::~Effects()
    {
    abort();
    trapErrors( dpy );
    for( QMap< Client*, Retained >::ConstIterator it = retained.begin();
         it != retained.end();
         ++it )
        XFreePixmap( dpy, (*it).pixmap );
    retained.clear();
    untrapErrors( dpy );
    delete presenter;
    }

void Effects::reconfigure()
    {
    minimize_style = (Style) options->effectsMinimizeAnimation;
    maximize_anim = options->effectsMaximizeAnimation;
    open_close_anim = options->effectsOpenCloseAnimation;
    // speed 0 (slow) .. 10 (fast), 5 normal: 500 .. 125 ms
    base_time = 250.0 * pow( 2.0, ( 5 - options->effectsAnimationSpeed ) / 5.0 );
    if( !options->effectsEnabled )
        abort();
    }

bool Effects::isActive() const
    {
    return out != None;
    }

double Effects::duration( double factor ) const
    {
    return base_time * factor;
    }

bool Effects::enabled() const
    {
    return options->effectsEnabled && have_render && have_composite && !options->useTranslucency
        && ws->windowOverview() != NULL && !ws->windowOverview()->isActive();
    }

// c can be animated now (its frame is redirected, it is on this desktop)
bool Effects::canAnimate( Client* c, bool need_shown ) const
    {
    if( !enabled() || c == NULL || c->frameId() == None )
        return false;
    if( !c->isOnCurrentDesktop() || c->isFullScreen() || ( need_shown && !c->isShown( true )))
        return false;
    return ws->windowOverview()->isRedirected( c->frameId());
    }

EffectAnim* Effects::findAnim( Client* c ) const
    {
    for( QPtrListIterator< EffectAnim > it( anims );
         it.current() != NULL;
         ++it )
        if( it.current()->client == c )
            return it.current();
    return NULL;
    }

EffectAnim* Effects::newAnim( int kind, Client* c )
    {
    EffectAnim* anim = new EffectAnim;
    anim->kind = (EffectAnim::Kind) kind;
    anim->client = c;
    anim->frame = c->frameId();
    anim->caption = c->caption();
    anim->geom = c->geometry();
    anim->started = now();
    return anim;
    }

void Effects::freeAnim( EffectAnim* anim )
    {
    trapErrors( dpy );
    if( anim->damage != None )
        XDamageDestroy( dpy, anim->damage );
    anim->damage = None;
    const int w = anim->snap.w[ 0 ];
    const int h = anim->snap.h[ 0 ];
    Pixmap kept = anim->snap.release( dpy, anim->keep_snap && anim->client != NULL );
    if( kept != None )
        {
        if( ws->hasClient( anim->client ) && anim->client->isMinimized())
            retain( anim->client, kept, QSize( w, h ), anim->snap.depth,
                anim->snap.visual );
        else
            XFreePixmap( dpy, kept );
        }
    untrapErrors( dpy );
    anims.removeRef( anim );
    delete anim;
    }

// the frame's pixmap, named (it stays when the frame is unmapped or resized)
bool Effects::namePixmap( Window frame, Pixmap* pixmap, QRect* geom, int* depth, Visual** visual )
    {
    *pixmap = None;
    XWindowAttributes attr;
    trapErrors( dpy );
    if( XGetWindowAttributes( dpy, frame, &attr ) && attr.map_state == IsViewable )
        {
        *pixmap = XCompositeNameWindowPixmap( dpy, frame );
        *geom = QRect( attr.x, attr.y, attr.width + 2 * attr.border_width, attr.height + 2 * attr.border_width );
        *depth = attr.depth;
        *visual = attr.visual;
        }
    if( untrapErrors( dpy ) > 0 )
        *pixmap = None; // (a named pixmap after an error would be freed by nobody: no ID was used)
    return *pixmap != None;
    }

void Effects::retain( Client* c, Pixmap pixmap, const QSize& size, int depth, Visual* visual )
    {
    dropRetained( c );
    if( pixmap == None )
        return;
    Retained r;
    r.pixmap = pixmap;
    r.size = size;
    r.depth = depth;
    r.visual = visual != NULL ? visual : DefaultVisual( dpy, DefaultScreen( dpy ));
    r.stamp = now();
    retained[ c ] = r;
    retained_bytes += (unsigned long) size.width() * size.height() * 4;
    // the oldest go when there are too many
    while( retained_bytes > RetainLimit && retained.count() > 1 )
        {
        QMap< Client*, Retained >::Iterator oldest = retained.end();
        for( QMap< Client*, Retained >::Iterator it = retained.begin();
             it != retained.end();
             ++it )
            if( it.key() != c && ( oldest == retained.end() || (*it).stamp < (*oldest).stamp ))
                oldest = it;
        if( oldest == retained.end())
            break;
        dropRetained( oldest.key());
        }
    }

void Effects::dropRetained( Client* c )
    {
    QMap< Client*, Retained >::Iterator it = retained.find( c );
    if( it == retained.end())
        return;
    XFreePixmap( dpy, (*it).pixmap );
    retained_bytes -= QMIN( retained_bytes, (unsigned long) (*it).size.width() * (*it).size.height() * 4 );
    retained.remove( it );
    }

// where a window minimizes to: its taskbar button, else the panel, else
// the bottom of its screen
QRect Effects::iconRect( Client* c ) const
    {
    QRect icon = c->iconGeometry();
    if( icon.isValid() && icon.width() > 1 && icon.height() > 1 )
        return icon;
    QDesktopWidget* desktop = QApplication::desktop();
    const int screen = desktop->screenNumber( c->geometry().center());
    const QRect sg = desktop->screenGeometry( screen < 0 ? 0 : screen );
    const ClientList& clients = ws->stackingOrder();
    for( ClientList::ConstIterator it = clients.begin();
         it != clients.end();
         ++it )
        if( (*it)->isDock() && (*it)->isShown( true ) && (*it)->isOnCurrentDesktop()
            && (*it)->geometry().intersects( sg ))
            {
            QRect g = (*it)->geometry().intersect( sg );
            const int s = QMIN( 48, QMIN( g.width(), g.height()));
            return QRect( g.center().x() - s / 2, g.center().y() - s / 2, s, s );
            }
    return QRect( sg.center().x() - 24, sg.bottom() - 48, 48, 48 );
    }

//****************************************
// the hooks
//****************************************

void Effects::minimize( Client* c, bool animate )
    {
    EffectAnim* anim = findAnim( c );
    if( anim != NULL && anim->kind == EffectAnim::Iconify && anim->p_to == 1 )
        return; // minimizing already (with its main window)
    if( !canAnimate( c ))
        {
        if( anim != NULL )
            freeAnim( anim );
        return;
        }
    if( !animate || minimize_style == None_ )
        { // only the contents for the restore
        if( anim != NULL )
            freeAnim( anim );
        Pixmap p;
        QRect geom;
        int depth;
        Visual* visual;
        if( namePixmap( c->frameId(), &p, &geom, &depth, &visual ))
            retain( c, p, geom.size(), depth, visual );
        return;
        }
    const QRect icon = iconRect( c );
    startIconify( c, true, icon );
    // the transients minimized with it fly along
    QValueList< Client* > list;
    for( ClientList::ConstIterator it = c->transients().begin();
         it != c->transients().end();
         ++it )
        list.append( *it );
    while( !list.isEmpty())
        {
        Client* t = list.first();
        list.remove( list.begin());
        if( t == c || t->isMinimized() || t->isTopMenu() || !canAnimate( t ) || findAnim( t ) != NULL )
            continue;
        startIconify( t, true, icon );
        for( ClientList::ConstIterator it = t->transients().begin();
             it != t->transients().end();
             ++it )
            list.append( *it );
        }
    }

void Effects::unminimize( Client* c, bool animate )
    {
    EffectAnim* anim = findAnim( c );
    if( anim != NULL && anim->kind == EffectAnim::Iconify && anim->p_to == 0 )
        return; // restoring already (with its main window)
    if( !animate || minimize_style == None_ || !canAnimate( c, false ))
        {
        if( anim != NULL )
            freeAnim( anim );
        dropRetained( c );
        return;
        }
    const QRect icon = iconRect( c );
    startIconify( c, false, icon );
    QValueList< Client* > list;
    for( ClientList::ConstIterator it = c->transients().begin();
         it != c->transients().end();
         ++it )
        list.append( *it );
    while( !list.isEmpty())
        {
        Client* t = list.first();
        list.remove( list.begin());
        if( t == c || !t->isMinimized() || t->isTopMenu() || !canAnimate( t, false ))
            continue;
        if( retained.contains( t ) || findAnim( t ) != NULL )
            startIconify( t, false, icon );
        for( ClientList::ConstIterator it = t->transients().begin();
             it != t->transients().end();
             ++it )
            list.append( *it );
        }
    }

// where anim's frame is in the scene: the window below it; covered: a
// window that is not a panel above it
static Window stackedAbove( EffectScene* scene, EffectAnim* anim, bool* covered )
    {
    Window prev = None;
    bool found = false;
    *covered = false;
    for( EffectScene::Win* win = scene->wins.first();
         win != NULL;
         win = scene->wins.next())
        {
        if( found )
            {
            if(( win->client == NULL || !win->client->isDock()) && win->geom.intersects( anim->geom ))
                *covered = true;
            }
        else if( win->id == anim->frame )
            found = true;
        else
            prev = win->id;
        }
    return prev;
    }

void Effects::startIconify( Client* c, bool minimizing, const QRect& icon )
    {
    const double full = duration( minimize_style == Genie ? 1.6 : 1.0 );
    EffectAnim* anim = findAnim( c );
    if( anim != NULL && anim->kind == EffectAnim::Iconify && !anim->snap.isNull()
        && extendArea( anim->geom.unite( icon ).unite( c->geometry())))
        { // the other way from where it is now
        anim->p_from = anim->p;
        anim->p_to = minimizing ? 1 : 0;
        anim->seg_start = now();
        anim->seg_duration = full * QMAX( 0.3, fabs( anim->p_to - anim->p_from ));
        anim->icon = icon;
        anim->keep_snap = minimizing;
        anim->defer = false;
        anim->wait = !minimizing;
        anim->ready = minimizing;
        anim->wait_start = now();
        anim->damage_first = anim->damage_last = 0;
        anim->fresh = true;
        if( !minimizing )
            { // (it gets mapped and raised)
            anim->use_below = false;
            anim->on_top = true;
            }
        if( !minimizing && anim->damage == None ) // (to see the repaint after the frame is mapped)
            anim->damage = XDamageCreate( dpy, c->frameId(), XDamageReportNonEmpty );
        if( debug )
            kdWarning( 1212 ) << "effects: " << ( minimizing ? "minimize " : "restore " ) << anim->caption
                << " reversed at " << anim->p_from << endl;
        renderFrame( true );
        requestFrame();
        return;
        }
    if( anim != NULL )
        freeAnim( anim );
    anim = newAnim( EffectAnim::Iconify, c );
    anim->icon = icon;
    anim->style = minimize_style;
    anim->linear = minimize_style == Genie;
    anim->on_top = true;
    anim->p = anim->p_from = minimizing ? 0 : 1;
    anim->p_to = minimizing ? 1 : 0;
    anim->seg_duration = full;
    anim->seg_start = now();
    if( minimizing )
        {
        Pixmap p;
        QRect geom;
        int depth;
        Visual* visual;
        if( !namePixmap( c->frameId(), &p, &geom, &depth, &visual ))
            {
            delete anim;
            return;
            }
        anim->snap.set( dpy, p, geom.size(), depth, visual, true );
        anim->keep_snap = true;
        anim->geom = geom; // (where the frame really is)
        }
    else
        {
        QMap< Client*, Retained >::Iterator it = retained.find( c );
        if( it != retained.end())
            {
            anim->snap.set( dpy, (*it).pixmap, (*it).size, (*it).depth, (*it).visual, true );
            retained_bytes -= QMIN( retained_bytes, (unsigned long) (*it).size.width() * (*it).size.height() * 4 );
            retained.remove( it );
            }
        else
            anim->defer = true; // the window's own contents, when it has painted them
        anim->wait = true;
        anim->ready = false;
        anim->wait_start = now();
        anim->damage = XDamageCreate( dpy, c->frameId(), XDamageReportNonEmpty );
        }
    if( minimizing && mayBeShaped( c ))
        anim->shaped = readShape( dpy, c->frameId(), anim->geom.size(), anim->shape );
    if( !startSession( anim->geom.unite( icon ).unite( c->geometry())))
        {
        if( minimizing ) // not animated, but kept for the restore
            retain( c, anim->snap.release( dpy, true ), anim->geom.size(), anim->snap.depth, anim->snap.visual );
        anims.append( anim );
        freeAnim( anim );
        return;
        }
    if( minimizing )
        { // below other windows: it stays below them (on top only over the panels)
        bool covered;
        Window below = stackedAbove( scene, anim, &covered );
        if( covered )
            {
            anim->use_below = true;
            anim->below = below;
            anim->on_top = false;
            }
        }
    anims.append( anim );
    if( debug )
        kdWarning( 1212 ) << "effects: " << ( minimizing ? "minimize " : "restore " ) << anim->caption
            << ( anim->defer ? " (no contents kept)" : "" ) << " " << anim->geom.x() << "," << anim->geom.y() << " "
            << anim->geom.width() << "x" << anim->geom.height() << " kwin " << c->geometry().x() << ","
            << c->geometry().y() << endl;
    renderFrame( true );
    requestFrame();
    }

/*
 Maximize and restore: the window is configured once, to its new size; its
 old contents, scaled, move to the new geometry and the new contents fade
 in when the application has repainted.  The area covers the old and the
 possible new geometries before the frame is resized.
*/
void Effects::maximizeBegin( Client* c )
    {
    EffectAnim* anim = findAnim( c );
    if( !maximize_anim || !canAnimate( c ) || c->isShade())
        {
        if( anim != NULL )
            freeAnim( anim );
        return;
        }
    QRect old = c->geometry();
    QRect bounds = old.unite( ws->clientArea( MaximizeArea, c ));
    if( c->geometryRestore().isValid())
        bounds = bounds.unite( c->geometryRestore());
    if( anim != NULL && anim->kind == EffectAnim::Morph && !anim->snap.isNull()
        && extendArea( bounds ))
        { // again while morphing: from where it is seen now, the first contents
        RectF cur = mix( RectF( anim->old_geom ), RectF( anim->geom ), anim->p );
        anim->old_geom = cur.bounds();
        anim->p = anim->p_from = 0;
        anim->seg_start = now();
        anim->fade = 0;
        anim->fade_start = 0;
        return;
        }
    if( anim != NULL )
        freeAnim( anim );
    anim = newAnim( EffectAnim::Morph, c );
    Pixmap p;
    QRect geom;
    int depth;
    Visual* visual;
    if( !namePixmap( c->frameId(), &p, &geom, &depth, &visual ))
        {
        delete anim;
        return;
        }
    anim->snap.set( dpy, p, geom.size(), depth, visual, true );
    if( mayBeShaped( c ))
        anim->shaped = readShape( dpy, c->frameId(), geom.size(), anim->shape );
    anim->old_geom = anim->geom = geom;
    anim->p = anim->p_from = 0;
    anim->p_to = 1;
    anim->seg_start = now();
    anim->seg_duration = duration( 1.0 );
    anim->wait = true;
    anim->ready = false;
    anim->wait_start = now();
    anim->damage = XDamageCreate( dpy, c->frameId(), XDamageReportNonEmpty );
    anims.append( anim );
    if( !startSession( bounds ))
        {
        freeAnim( anim );
        return;
        }
    renderFrame( true );
    }

void Effects::maximizeEnd( Client* c )
    {
    EffectAnim* anim = findAnim( c );
    if( anim == NULL || anim->kind != EffectAnim::Morph )
        return;
    if( c->geometry() == anim->old_geom && anim->fade_start == 0 && anim->p == 0 )
        { // nothing changed (or not yet: the size of a shaded window)
        freeAnim( anim );
        if( anims.isEmpty())
            renderFrame( true ); // the plain scene, then the session ends
        return;
        }
    anim->geom = c->geometry();
    anim->seg_start = now();
    anim->wait_start = now();
    anim->damage_first = anim->damage_last = 0;
    anim->ready = false;
    if( scene != NULL )
        scene->dirty = true; // a new pixmap
    if( !extendArea( anim->geom ))
        {
        freeAnim( anim );
        renderFrame( true );
        return;
        }
    if( debug )
        kdWarning( 1212 ) << "effects: morph " << anim->caption << " " << anim->old_geom.width() << "x"
            << anim->old_geom.height() << " -> " << anim->geom.width() << "x" << anim->geom.height() << endl;
    requestFrame();
    }

// a new window: shown when it has painted, growing a little and fading in
void Effects::windowOpening( Client* c )
    {
    if( !open_close_anim || !( c->isNormalWindow() || c->isDialog()) || c->isSpecialWindow()
        || c->isMinimized() || !canAnimate( c, false ) || findAnim( c ) != NULL )
        return;
    EffectAnim* anim = newAnim( EffectAnim::Open, c );
    anim->p = anim->p_from = 0;
    anim->p_to = 1;
    anim->seg_duration = duration( 0.8 );
    anim->defer = true;
    anim->wait = true;
    anim->ready = false;
    anim->wait_start = now();
    anim->damage = XDamageCreate( dpy, c->frameId(), XDamageReportNonEmpty );
    anims.append( anim );
    if( !startSession( anim->geom ))
        {
        freeAnim( anim );
        return;
        }
    if( debug )
        kdWarning( 1212 ) << "effects: open " << anim->caption << endl;
    renderFrame( true );
    poll_timer.start( 10, true );
    }

// a closed window: its last contents shrink a little and fade out where it was
void Effects::windowClosing( Client* c )
    {
    EffectAnim* anim = findAnim( c );
    if( anim != NULL )
        freeAnim( anim );
    dropRetained( c );
    if( !open_close_anim || !( c->isNormalWindow() || c->isDialog()) || c->isSpecialWindow()
        || !canAnimate( c ))
        return;
    anim = newAnim( EffectAnim::Close, c );
    Pixmap p;
    QRect geom;
    int depth;
    Visual* visual;
    if( !namePixmap( c->frameId(), &p, &geom, &depth, &visual ))
        {
        delete anim;
        return;
        }
    anim->snap.set( dpy, p, geom.size(), depth, visual, true );
    if( mayBeShaped( c ))
        anim->shaped = readShape( dpy, c->frameId(), geom.size(), anim->shape );
    anim->geom = geom;
    anim->p = anim->p_from = 1;
    anim->p_to = 0;
    anim->seg_start = now();
    anim->seg_duration = duration( 0.6 );
    anims.append( anim );
    if( !startSession( anim->geom ))
        {
        freeAnim( anim );
        return;
        }
    // where it is in the stacking order
    bool covered;
    anim->below = stackedAbove( scene, anim, &covered );
    anim->use_below = true;
    anim->client = NULL; // nothing more from the client
    if( debug )
        kdWarning( 1212 ) << "effects: close " << anim->caption << endl;
    renderFrame( true );
    requestFrame();
    }

void Effects::clientRemoved( Client* c )
    {
    dropRetained( c );
    for( QPtrListIterator< EffectAnim > it( anims );
         it.current() != NULL;
         )
        {
        EffectAnim* anim = it.current();
        ++it;
        if( anim->client == c )
            {
            anim->client = NULL;
            anim->keep_snap = false;
            if( anim->kind != EffectAnim::Close )
                freeAnim( anim ); // (the frame is going away)
            }
        }
    if( scene != NULL )
        for( EffectScene::Win* win = scene->wins.first();
             win != NULL;
             win = scene->wins.next())
            if( win->client == c )
                win->client = NULL;
    if( isActive() && anims.isEmpty())
        renderFrame( true );
    }

void Effects::abort()
    {
    if( debug && isActive())
        kdWarning( 1212 ) << "effects: aborted" << endl;
    while( !anims.isEmpty())
        freeAnim( anims.first());
    endSession();
    }

//****************************************
// the session
//****************************************

bool Effects::startSession( const QRect& needed )
    {
    if( out != None )
        return extendArea( needed );
    const double t = now();
    QRect screen( 0, 0, DisplayWidth( dpy, DefaultScreen( dpy )), DisplayHeight( dpy, DefaultScreen( dpy )));
    area = needed.intersect( screen );
    if( area.isEmpty())
        return false;
    scene = new EffectScene;
    if( !buildScene() || !scene->ok )
        {
        if( debug )
            kdWarning( 1212 ) << "effects: not animated (windows that can't be drawn there, or locked)" << endl;
        endSession();
        return false;
        }
    XSetWindowAttributes attr;
    attr.override_redirect = True;
    attr.background_pixmap = None;
    attr.border_pixel = 0;
    out = XCreateWindow( dpy, root, 0, 0, screen.width(), screen.height(), 0, CopyFromParent, InputOutput,
        CopyFromParent, CWOverrideRedirect | CWBackPixmap | CWBorderPixel, &attr );
    // no input, only the animated area; set before it is mapped
    XShapeCombineRectangles( dpy, out, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted );
    XRectangle rect = { (short) area.x(), (short) area.y(), (unsigned short) area.width(),
        (unsigned short) area.height() };
    XShapeCombineRectangles( dpy, out, ShapeBounding, 0, 0, &rect, 1, ShapeSet, Unsorted );
    placeWindow();
    XMapWindow( dpy, out );
    presenter->start( out, area.x(), area.y(), area.width(), area.height(), have_present );
    finishing = false;
    frame_wanted = false;
    frame_count = 0;
    stat_start = t;
    stat_render = stat_max_render = 0;
    stat_skipped = stat_gaps = 0;
    stat_strips = 0;
    stat_setup = now() - t;
    return true;
    }

// right above the managed windows (kept there: a new frame is created on
// top until KWin restacks it)
void Effects::placeWindow()
    {
    if( out == None || scene == NULL || scene->limit == None )
        return;
    XWindowChanges wc;
    wc.sibling = scene->limit;
    wc.stack_mode = Above;
    XConfigureWindow( dpy, out, CWSibling | CWStackMode, &wc );
    }

bool Effects::extendArea( const QRect& needed )
    {
    if( out == None )
        return false;
    QRect screen( 0, 0, DisplayWidth( dpy, DefaultScreen( dpy )), DisplayHeight( dpy, DefaultScreen( dpy )));
    QRect larger = area.unite( needed ).intersect( screen );
    if( larger == area )
        return true;
    QRect old = area;
    area = larger;
    scene->dirty = true;
    if( !buildScene() || !scene->ok )
        {
        area = old;
        scene->dirty = true;
        return false;
        }
    XRectangle rect = { (short) area.x(), (short) area.y(), (unsigned short) area.width(),
        (unsigned short) area.height() };
    XShapeCombineRectangles( dpy, out, ShapeBounding, 0, 0, &rect, 1, ShapeSet, Unsorted );
    presenter->setGeometry( area.x(), area.y(), area.width(), area.height());
    return true;
    }

void Effects::endSession()
    {
    frame_timer.stop();
    poll_timer.stop();
    frame_wanted = false;
    finishing = false;
    trapErrors( dpy );
    presenter->stop();
    if( out != None )
        XDestroyWindow( dpy, out );
    out = None;
    if( scene != NULL )
        {
        for( EffectScene::Win* win = scene->wins.first();
             win != NULL;
             win = scene->wins.next())
            {
            win->pic.release( dpy );
            if( win->damage != None )
                XDamageDestroy( dpy, win->damage );
            }
        if( scene->root_picture != None )
            XRenderFreePicture( dpy, scene->root_picture );
        delete scene;
        scene = NULL;
        }
    untrapErrors( dpy );
    if( frame_count > 0 )
        report();
    frame_count = 0;
    area = QRect();
    names = QString::null;
    }

void Effects::report()
    {
    if( !debug )
        return;
    double t = last_frame - stat_start;
    kdWarning( 1212 ) << "effects: " << names << frame_count << " frames in " << t << " ms, "
        << ( frame_count > 1 && t > 0 ? ( frame_count - 1 ) * 1000.0 / t : 0 ) << " fps, setup " << stat_setup
        << " ms, render avg " << ( frame_count ? stat_render / frame_count : 0 ) << " max " << stat_max_render
        << " ms, skipped " << stat_skipped << " missed refreshes " << stat_gaps << ", area " << area.width()
        << "x" << area.height() << ( stat_strips > 0 ? QString( ", genie strips %1" ).arg( stat_strips )
        : QString::null ) << endl;
    }

/*
 The windows below our window: everything up to the topmost frame (or the
 support window, which KWin keeps above all frames).  A window there that
 is not redirected (a full screen one, an override-redirect one below the
 frames) can't be drawn: no animation over it.  A window above covering
 the whole screen (the screen locker): no animation.
*/
bool Effects::buildScene()
    {
    if( scene == NULL )
        return false;
    scene->dirty = false;
    scene->ok = true;
    trapErrors( dpy );
    Window root_return, parent;
    Window* children = NULL;
    unsigned int count = 0;
    if( !XQueryTree( dpy, root, &root_return, &parent, &children, &count ))
        {
        untrapErrors( dpy );
        scene->ok = false;
        return false;
        }
    const Window support = ws->supportWindowId();
    int limit = -1;
    for( unsigned int i = 0;
         i < count;
         ++i )
        if( children[ i ] == support || ws->findClient( FrameIdMatchPredicate( children[ i ] )) != NULL )
            limit = i;
    for( EffectAnim* anim = anims.first();
         anim != NULL;
         anim = anims.next())
        for( unsigned int i = 0;
             i < count;
             ++i )
            if( children[ i ] == anim->frame && (int) i > limit )
                limit = i; // a new frame, not restacked yet
    const Window old_limit = scene->limit;
    scene->limit = limit >= 0 ? children[ limit ] : None;
    for( EffectScene::Win* win = scene->wins.first();
         win != NULL;
         win = scene->wins.next())
        win->seen = false;
    QPtrList< EffectScene::Win > list;
    const QRect screen( 0, 0, DisplayWidth( dpy, DefaultScreen( dpy )), DisplayHeight( dpy, DefaultScreen( dpy )));
    bool out_below = false;
    for( unsigned int i = 0;
         i < count;
         ++i )
        {
        Window w = children[ i ];
        if( w == out )
            {
            if( (int) i < limit )
                out_below = true;
            continue;
            }
        XWindowAttributes attr;
        if( !XGetWindowAttributes( dpy, w, &attr ) || attr.map_state != IsViewable
            || attr.c_class != InputOutput || attr.width <= 0 || attr.height <= 0 )
            continue;
        QRect geom( attr.x, attr.y, attr.width + 2 * attr.border_width, attr.height + 2 * attr.border_width );
        if( (int) i > limit )
            { // above us
            if( geom.contains( screen ) && attr.override_redirect )
                scene->ok = false; // the screen locker
            continue;
            }
        Client* c = ws->findClient( FrameIdMatchPredicate( w ));
        bool redirected = c != NULL && ws->windowOverview()->isRedirected( w );
        if( c == NULL )
            { // the window of a new client being managed?
            for( EffectAnim* anim = anims.first();
                 anim != NULL;
                 anim = anims.next())
                if( anim->frame == w && anim->client != NULL )
                    {
                    c = anim->client;
                    redirected = ws->windowOverview()->isRedirected( w );
                    }
            }
        if( !redirected )
            {
            if( geom.intersects( area ))
                {
                if( debug )
                    kdWarning( 1212 ) << "effects: can't draw 0x" << QString::number( w, 16 ) << " "
                        << ( c != NULL ? c->caption() : QString( "(unmanaged)" )) << endl;
                scene->ok = false;
                }
            continue;
            }
        EffectScene::Win* win = scene->find( w );
        if( win != NULL && ( win->geom.size() != geom.size()))
            { // a new pixmap after a resize
            win->pic.release( dpy );
            win->shaped = false;
            win->shape.clear();
            }
        if( win == NULL )
            {
            win = new EffectScene::Win;
            win->id = w;
            }
        else
            scene->wins.take( scene->wins.findRef( win ));
        win->geom = geom;
        win->client = c;
        win->viewable = true;
        win->redirected = true;
        win->seen = true;
        if( win->pic.isNull() && geom.intersects( area ))
            {
            Pixmap p = XCompositeNameWindowPixmap( dpy, w );
            win->pic.set( dpy, p, geom.size(), attr.depth, attr.visual, true );
            if( win->pic.picture[ 0 ] != None )
                {
                setTransform( dpy, win->pic.picture[ 0 ], 1, 1, 0, 0 );
                XRenderSetPictureFilter( dpy, win->pic.picture[ 0 ], FilterNearest, NULL, 0 );
                }
            if( mayBeShaped( c ))
                win->shaped = readShape( dpy, w, geom.size(), win->shape );
            }
        if( win->damage == None && geom.intersects( area ))
            win->damage = XDamageCreate( dpy, w, XDamageReportNonEmpty );
        list.append( win );
        }
    if( children != NULL )
        XFree( children );
    // the windows that are gone (destroyed ones: their damage too)
    for( EffectScene::Win* win = scene->wins.first();
         win != NULL;
         win = scene->wins.next())
        {
        win->pic.release( dpy );
        if( win->damage != None )
            XDamageDestroy( dpy, win->damage );
        win->damage = None;
        }
    scene->wins.clear();
    scene->wins.setAutoDelete( false );
    scene->wins = list;
    scene->wins.setAutoDelete( true );
    // the root background
    if( scene->root_picture == None )
        {
        Atom type;
        int format;
        unsigned long n, after;
        unsigned char* data = NULL;
        static Atom rootpmap = XInternAtom( dpy, "_XROOTPMAP_ID", False );
        if( XGetWindowProperty( dpy, root, rootpmap, 0, 1, False, XA_PIXMAP, &type, &format, &n, &after,
            &data ) == Success && data != NULL && type == XA_PIXMAP && format == 32 && n == 1 )
            {
            Pixmap p = *(Pixmap*) data;
            XRenderPictureAttributes pa;
            pa.repeat = RepeatNormal;
            scene->root_picture = XRenderCreatePicture( dpy, p,
                XRenderFindVisualFormat( dpy, DefaultVisual( dpy, DefaultScreen( dpy ))), CPRepeat, &pa );
            }
        if( data != NULL )
            XFree( data );
        }
    if( untrapErrors( dpy ) > 0 )
        {
        if( scene->root_picture != None )
            { // (a stale root pixmap: none)
            trapErrors( dpy );
            XRenderFreePicture( dpy, scene->root_picture );
            untrapErrors( dpy );
            scene->root_picture = None;
            }
        if( debug )
            kdWarning( 1212 ) << "effects: X errors while reading the windows" << endl;
        }
    if( out != None && ( out_below || old_limit != scene->limit ))
        placeWindow();
    return true;
    }

//****************************************
// frames
//****************************************

void Effects::requestFrame()
    {
    if( out == None )
        return;
    frame_wanted = true;
    if( frame_timer.isActive())
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

void Effects::frameTimeout()
    {
    if( out == None || !frame_wanted )
        return;
    frame_wanted = false;
    renderFrame();
    if( frame_wanted )
        requestFrame();
    }

// waiting for a window to repaint, nothing moves
void Effects::pollTimeout()
    {
    if( out != None && !frame_timer.isActive())
        {
        frame_wanted = true;
        frameTimeout();
        }
    }

void Effects::presentCompleted( bool skipped, int missed )
    {
    if( out == None )
        return;
    if( skipped )
        ++stat_skipped;
    else if( !finishing )
        stat_gaps += missed;
    if( finishing && anims.isEmpty())
        {
        if( presenter->queued() == 0 )
            endSession();
        }
    else if( frame_wanted )
        requestFrame();
    }

void Effects::presentIdle()
    {
    if( out != None && frame_wanted )
        requestFrame();
    }

// the state at time t; true while it moves
bool Effects::updateAnim( EffectAnim* anim, double t )
    {
    // the real geometry: of the mapped frame as the server has it, else KWin's
    if( anim->kind != EffectAnim::Close && ( anim->kind != EffectAnim::Iconify || anim->p_to == 0 ))
        {
        EffectScene::Win* win = scene->find( anim->frame );
        if( win != NULL )
            anim->geom = win->geom;
        else if( anim->client != NULL && ws->hasClient( anim->client ))
            anim->geom = anim->client->geometry();
        }
    if( anim->wait && !anim->ready
        && (( anim->damage_first > 0 && t - anim->damage_last >= QuietTime ) || t - anim->wait_start >= MaxWait ))
        {
        anim->ready = true;
        if( anim->defer )
            anim->seg_start = t;
        if( debug )
            kdWarning( 1212 ) << "effects: " << anim->caption << " ready after " << t - anim->wait_start << " ms"
                << ( anim->damage_first > 0 ? "" : " (no repaint seen)" ) << endl;
        }
    if( anim->defer && !anim->ready )
        return false;
    const double lin = anim->seg_duration > 0 ? clamp01(( t - anim->seg_start ) / anim->seg_duration ) : 1.0;
    // (a minimized window gets small quickly with ease out cubic and
    // lingers at the button; ease out quad, like GNOME's)
    anim->p = mix( anim->p_from, anim->p_to, anim->linear ? lin
        : anim->kind == EffectAnim::Iconify ? easeOutQuad( lin ) : easeOutCubic( lin ));
    bool moving = lin < 1;
    switch( anim->kind )
        {
        case EffectAnim::Iconify:
            anim->done = lin >= 1 && ( anim->p_to == 1 || anim->ready );
            break;
        case EffectAnim::Morph:
            if( anim->ready && anim->fade_start == 0 )
                anim->fade_start = t;
            anim->fade = anim->fade_start > 0 ? clamp01(( t - anim->fade_start ) / CrossfadeTime ) : 0;
            if( anim->fade_start > 0 && anim->fade < 1 )
                moving = true;
            anim->done = lin >= 1 && anim->fade >= 1;
            break;
        case EffectAnim::Open:
        case EffectAnim::Close:
            anim->done = lin >= 1;
            break;
        }
    return moving;
    }

// draws pic (w x h) into rect, offset by -ox, -oy
static void drawPicture( Display* dpy, Picture dst, EffectPicture& pic, const RectF& rect, double alpha,
    int ox, int oy, bool shaped, const QValueVector< XRectangle >& shape )
    {
    if( pic.isNull() || alpha <= 1.0 / 512 || rect.w < 0.5 || rect.h < 0.5 )
        return;
    const int level = pic.level( dpy, QMIN( rect.w / pic.w[ 0 ], rect.h / pic.h[ 0 ] ));
    Picture src = pic.picture[ level ];
    const double sx = rect.w / pic.w[ level ];
    const double sy = rect.h / pic.h[ level ];
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
    if( shaped && !shape.isEmpty())
        {
        QValueVector< XRectangle > clip = scaledShape( shape, QSize( pic.w[ 0 ], pic.h[ 0 ] ), rect, ox, oy );
        XRenderSetPictureClipRectangles( dpy, dst, 0, 0, &clip[ 0 ], clip.size());
        }
    Picture mask = None;
    if( alpha < 1.0 )
        mask = solidPicture( dpy, Qt::black, alpha );
    // at scale 1 like the server's automatic compositing (Src, also for
    // ARGB windows), else blended (the edges are anti-aliased)
    const int op = mask != None || !identity ? PictOpOver : PictOpSrc;
    XRenderComposite( dpy, op, src, mask, dst, 0, 0, 0, 0, dx - ox, dy - oy, dw, dh );
    if( mask != None )
        XRenderFreePicture( dpy, mask );
    if( shaped && !shape.isEmpty())
        {
        XRenderPictureAttributes pa;
        pa.clip_mask = None;
        XRenderChangePicture( dpy, dst, CPClipMask, &pa );
        }
    }

void Effects::drawAnim( EffectAnim* anim, Picture dst )
    {
    const int ox = area.x();
    const int oy = area.y();
    EffectScene::Win* live = scene->find( anim->frame );
    static const QValueVector< XRectangle > noshape;
    switch( anim->kind )
        {
        case EffectAnim::Iconify:
            {
            EffectPicture* pic = &anim->snap;
            const QValueVector< XRectangle >* shape = &anim->shape;
            bool shaped = anim->shaped;
            if( pic->isNull())
                {
                if( live == NULL || live->pic.isNull())
                    return;
                pic = &live->pic;
                shape = &live->shape;
                shaped = live->shaped;
                }
            if( anim->p <= 0 && anim->p_to == 0 )
                { // restored, waiting for the repaint
                drawPicture( dpy, dst, *pic, RectF( anim->geom ), 1.0, ox, oy, shaped, *shape );
                return;
                }
            if( anim->style == Genie )
                {
                drawGenie( anim, pic->picture[ 0 ], pic->w[ 0 ], pic->h[ 0 ], dst, anim->p, 1.0 );
                return;
                }
            RectF target = fitInto( anim->geom.width(), anim->geom.height(), anim->icon );
            RectF rect = mix( RectF( anim->geom ), target, anim->p );
            const double alpha = 1 - anim->p * anim->p * anim->p * anim->p;
            drawPicture( dpy, dst, *pic, rect, alpha, ox, oy, shaped, *shape );
            break;
            }
        case EffectAnim::Morph:
            {
            RectF rect = mix( RectF( anim->old_geom ), RectF( anim->geom ), anim->p );
            if( anim->fade < 1 )
                drawPicture( dpy, dst, anim->snap, rect, 1.0, ox, oy, anim->shaped, anim->shape );
            if( anim->fade > 0 && live != NULL )
                drawPicture( dpy, dst, live->pic, rect, anim->fade, ox, oy, live->shaped, live->shape );
            break;
            }
        case EffectAnim::Open:
        case EffectAnim::Close:
            {
            EffectPicture* pic = &anim->snap;
            const QValueVector< XRectangle >* shape = &anim->shape;
            bool shaped = anim->shaped;
            if( anim->kind == EffectAnim::Open )
                {
                if( live == NULL || !anim->ready )
                    return;
                pic = &live->pic;
                shape = &live->shape;
                shaped = live->shaped;
                }
            const double s = anim->kind == EffectAnim::Open ? 0.9 + 0.1 * anim->p : 0.85 + 0.15 * anim->p;
            RectF g( anim->geom );
            RectF rect( g.cx() - g.w * s / 2, g.cy() - g.h * s / 2, g.w * s, g.h * s );
            if( anim->p >= 1 )
                rect = g;
            drawPicture( dpy, dst, *pic, rect, anim->p, ox, oy, shaped, *shape );
            break;
            }
        }
    }

/*
 The genie: the window is pulled into its taskbar button through a funnel
 that narrows from the window's width to the button's.  First its near
 edge is pulled to the button while the edges curve in (the window
 stretches), then its far edge follows (the window is squeezed into the
 button).  Drawn as strips across the direction of travel, each scaled on
 its own across (to the funnel's width in its middle) and all alike along;
 thinner strips where the edges curve more, each a whole number of pixels
 high (no clipping needed between them).

 Computed along the main axis m (the direction of travel, negated for up
 and left so that it increases towards the button) and the cross axis.
*/
void Effects::drawGenie( EffectAnim* anim, Picture src, int sw, int sh, Picture dst, double p, double alpha )
    {
    const QRect& wg = anim->geom;
    const QRect& ic = anim->icon;
    // the direction
    const QPoint c = ic.center();
    bool horizontal = false;
    int sign = 1;
    if( c.y() > wg.bottom())
        sign = 1;
    else if( c.y() < wg.top())
        sign = -1;
    else if( c.x() > wg.right())
        horizontal = true;
    else if( c.x() < wg.left())
        {
        horizontal = true;
        sign = -1;
        }
    else
        sign = c.y() >= wg.center().y() ? 1 : -1;
    // along m: the window from m0 (its far edge) for len, the button's near
    // edge at i0; across: the window from wc0 to wc1, the button from ic0 to ic1
    const double len = horizontal ? sw : sh;
    double m0, i0, wc0, wc1, ic0, ic1;
    if( !horizontal )
        {
        m0 = sign > 0 ? wg.y() : -( wg.y() + sh );
        i0 = sign > 0 ? ic.y() : -( ic.y() + ic.height());
        wc0 = wg.x();
        wc1 = wg.x() + sw;
        ic0 = ic.x();
        ic1 = ic.x() + ic.width();
        }
    else
        {
        m0 = sign > 0 ? wg.x() : -( wg.x() + sw );
        i0 = sign > 0 ? ic.x() : -( ic.x() + ic.width());
        wc0 = wg.y();
        wc1 = wg.y() + sh;
        ic0 = ic.y();
        ic1 = ic.y() + ic.height();
        }
    i0 = QMAX( i0, m0 + len ); // (a button beside the window: below its near edge)
    // the near edge goes to the button and the edges curve in, then the far edge follows
    const double s1 = easeInOutCubic( clamp01( p / 0.45 ));
    const double s2 = easeInOutCubic( clamp01(( p - 0.3 ) / 0.7 ));
    const double top = m0 + s2 * ( i0 - m0 );
    const double bottom = QMAX( top, mix( m0 + len, i0, s1 ));
    if( bottom - top < 0.5 )
        return;
    const double sy = ( bottom - top ) / len; // along, the same for all strips
    const int ox = area.x();
    const int oy = area.y();
    // the funnel's edges at m
#define GENIE_F( m ) ( s1 * smoothstep((( m ) - m0 ) / ( i0 - m0 )))
#define GENIE_L( m ) mix( wc0, ic0, GENIE_F( m ))
#define GENIE_R( m ) mix( wc1, ic1, GENIE_F( m ))
    Picture mask = alpha < 1.0 ? solidPicture( dpy, Qt::black, alpha ) : None;
    XRenderSetPictureFilter( dpy, src, FilterBilinear, NULL, 0 );
    int strips = 0;
    const int begin = (int) floor( top );
    const int end = (int) ceil( bottom );
    // the strips: as high as the edges allow
    QValueVector< int > heights;
    for( double step = GenieStep;
         ;
         step *= 2 )
        {
        heights.clear();
        for( int m = begin;
             m < end;
             )
            {
            int h = end - m;
            const double l0 = GENIE_L( m );
            const double r0 = GENIE_R( m );
            while( h > GenieMinStrip
                && ( fabs( GENIE_L( m + h ) - l0 ) > step || fabs( GENIE_R( m + h ) - r0 ) > step ))
                h = QMAX( GenieMinStrip, h / 2 );
            h = QMIN( h, end - m );
            heights.append( h );
            m += h;
            }
        if( (int) heights.size() <= GenieMaxStrips || step > 64 )
            break;
        }
    int m = begin;
    for( unsigned int i = 0;
         i < heights.size();
         ++i )
        {
        const int h = heights[ i ];
        const double cl = GENIE_L( m + h / 2.0 );
        const double cr = GENIE_R( m + h / 2.0 );
        const double sx = ( cr - cl ) / ( horizontal ? sh : sw ); // across
        if( sx > 0.001 )
            {
            const int c0 = (int) floor( cl );
            const int c1 = (int) ceil( cr );
            // the source of the box's pixel at b along: u = ( m + b - top ) / sy from the far
            // edge (sign > 0), len - u (sign < 0); across: ( c0 + b - cl ) / sx
            const double along_scale = 1 / sy;
            const double along_off = sign > 0 ? ( m - top ) / sy : len - ( m - top ) / sy - h / sy;
            const double across_off = ( c0 - cl ) / sx;
            int dx, dy, dw, dh;
            if( !horizontal )
                {
                setTransform( dpy, src, 1 / sx, along_scale, across_off, along_off );
                dx = c0;
                dw = c1 - c0;
                dy = sign > 0 ? m : -( m + h );
                dh = h;
                }
            else
                {
                setTransform( dpy, src, along_scale, 1 / sx, along_off, across_off );
                dy = c0;
                dh = c1 - c0;
                dx = sign > 0 ? m : -( m + h );
                dw = h;
                }
            XRenderComposite( dpy, PictOpOver, src, mask, dst, 0, 0, 0, 0, dx - ox, dy - oy, dw, dh );
            ++strips;
            }
        m += h;
        }
#undef GENIE_F
#undef GENIE_L
#undef GENIE_R
    if( mask != None )
        XRenderFreePicture( dpy, mask );
    if( strips > stat_strips )
        stat_strips = strips;
    }

void Effects::renderFrame( bool immediate )
    {
    if( out == None || scene == NULL )
        return;
    if( scene->dirty )
        {
        buildScene();
        if( !scene->ok )
            { // something that can't be drawn appeared (the screen locker)
            abort();
            return;
            }
        }
    Picture dst = presenter->beginFrame();
    if( dst == None )
        { // all still used by the server, wait for an idle event
        frame_wanted = true;
        return;
        }
    const double t_start = now();
    const double t0 = immediate ? t_start : presenter->frameTime( t_start );
    bool moving = false;
    bool waiting = false;
    for( QPtrListIterator< EffectAnim > it( anims );
         it.current() != NULL;
         )
        {
        EffectAnim* anim = it.current();
        ++it;
        if( anim->fresh )
            { // (the setup took a few ms: the first frame is where it starts)
            if( !anim->defer )
                anim->seg_start = t0;
            anim->fresh = false;
            }
        if( updateAnim( anim, t0 ))
            moving = true;
        if( anim->done )
            {
            if( debug )
                kdWarning( 1212 ) << "effects: " << anim->caption << " done after " << t0 - anim->started
                    << " ms" << endl;
            if( names.length() < 200 )
                names += QString( "%1 '%2', " ).arg( anim->kind == EffectAnim::Iconify
                    ? ( anim->p_to > 0 ? "minimize" : "restore" ) : anim->kind == EffectAnim::Morph ? "morph"
                    : anim->kind == EffectAnim::Open ? "open" : "close" ).arg( anim->caption.left( 24 ));
            freeAnim( anim );
            }
        else if( !moving )
            waiting = true;
        }
    const int ox = area.x();
    const int oy = area.y();
    // the background
    if( scene->root_picture != None )
        XRenderComposite( dpy, PictOpSrc, scene->root_picture, None, dst, ox, oy, 0, 0, 0, 0,
            area.width(), area.height());
    else
        {
        XRenderColor black = { 0, 0, 0, 0xffff };
        XRenderFillRectangle( dpy, PictOpSrc, dst, &black, 0, 0, area.width(), area.height());
        }
    static const QValueVector< XRectangle > noshape;
    // closing windows above the bottom one
    for( EffectAnim* anim = anims.first();
         anim != NULL;
         anim = anims.next())
        if( anim->use_below && anim->below == None )
            drawAnim( anim, dst );
    for( EffectScene::Win* win = scene->wins.first();
         win != NULL;
         win = scene->wins.next())
        {
        EffectAnim* in_place = NULL;
        bool animated = false;
        for( EffectAnim* anim = anims.first();
             anim != NULL;
             anim = anims.next())
            if( anim->frame == win->id )
                {
                animated = true;
                if( !anim->on_top && !anim->use_below )
                    in_place = anim;
                }
        if( in_place != NULL )
            drawAnim( in_place, dst );
        else if( !animated && win->geom.intersects( area ) && !win->pic.isNull())
            {
            Picture src = win->pic.picture[ 0 ];
            if( win->shaped )
                {
                QValueVector< XRectangle > clip = scaledShape( win->shape, win->geom.size(), RectF( win->geom ),
                    ox, oy );
                XRenderSetPictureClipRectangles( dpy, dst, 0, 0, &clip[ 0 ], clip.size());
                }
            XRenderComposite( dpy, PictOpSrc, src, None, dst, 0, 0, 0, 0, win->geom.x() - ox, win->geom.y() - oy,
                win->geom.width(), win->geom.height());
            if( win->shaped )
                {
                XRenderPictureAttributes pa;
                pa.clip_mask = None;
                XRenderChangePicture( dpy, dst, CPClipMask, &pa );
                }
            }
        for( EffectAnim* anim = anims.first();
             anim != NULL;
             anim = anims.next())
            if( anim->use_below && anim->below == win->id )
                drawAnim( anim, dst );
        }
    // on top: minimized and restored windows, closed ones whose neighbour went
    for( EffectAnim* anim = anims.first();
         anim != NULL;
         anim = anims.next())
        {
        if( anim->use_below )
            {
            if( anim->below != None && scene->find( anim->below ) == NULL )
                drawAnim( anim, dst );
            }
        else if( anim->on_top )
            drawAnim( anim, dst );
        }
    if( !dump_dir.isEmpty())
        dumpFrame();
    presenter->endFrame( immediate );
    ++frame_count;
    if( debug )
        {
        XSync( dpy, False );
        double t = now() - t_start;
        stat_render += t;
        stat_max_render = QMAX( stat_max_render, t );
        }
    last_frame = t_start;
    if( anims.isEmpty())
        { // this was the plain scene: the window goes when it has been shown
        finishing = true;
        if( !presenter->usesPresent() || presenter->queued() == 0 || immediate )
            {
            XSync( dpy, False );
            endSession();
            }
        return;
        }
    finishing = false;
    if( moving )
        frame_wanted = true;
    else if( waiting && !poll_timer.isActive())
        poll_timer.start( 10, true );
    }

void Effects::dumpFrame()
    {
    XImage* img = XGetImage( dpy, presenter->framePixmap(), 0, 0, area.width(), area.height(), AllPlanes, ZPixmap );
    if( img == NULL )
        return;
    QString name;
    name.sprintf( "/effect-%04d.ppm", dump_index++ );
    name.prepend( dump_dir );
    FILE* f = fopen( QFile::encodeName( name ), "wb" );
    if( f != NULL )
        {
        fprintf( f, "P6\n%d %d\n255\n", area.width(), area.height());
        QByteArray line( area.width() * 3 );
        for( int y = 0;
             y < area.height();
             ++y )
            {
            for( int x = 0;
                 x < area.width();
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
        fprintf( log, "%s %.1f %d %d\n", QFile::encodeName( name ).data(), now() - stat_start, area.x(), area.y());
        fclose( log );
        }
    }

bool Effects::x11Event( XEvent* e )
    {
    if( out == None )
        return false;
    if( e->type == damage_event + XDamageNotify )
        {
        XDamageNotifyEvent* de = (XDamageNotifyEvent*) e;
        for( EffectAnim* anim = anims.first();
             anim != NULL;
             anim = anims.next())
            if( anim->damage == de->damage )
                {
                XDamageSubtract( dpy, de->damage, None, None );
                const double t = now();
                if( t >= anim->wait_start )
                    {
                    if( anim->damage_first == 0 )
                        anim->damage_first = t;
                    anim->damage_last = t;
                    }
                requestFrame();
                return true;
                }
        for( EffectScene::Win* win = scene->wins.first();
             win != NULL;
             win = scene->wins.next())
            if( win->damage == de->damage )
                {
                XDamageSubtract( dpy, de->damage, None, None );
                requestFrame();
                return true;
                }
        return false;
        }
    // the top-level windows change: read them again in the next frame
    switch( e->type )
        {
        case MapNotify:
        case UnmapNotify:
        case DestroyNotify:
        case ReparentNotify:
        case CirculateNotify:
            if( e->xany.window == root && e->xmap.window != out )
                {
                scene->dirty = true;
                requestFrame();
                }
            break;
        case ConfigureNotify:
            if( e->xconfigure.event == root && e->xconfigure.window != out )
                {
                scene->dirty = true;
                requestFrame();
                }
            break;
        default:
            break;
        }
    return false;
    }

/*
 Maximize and restore are animated between the geometry before and after
 Client::changeMaximize(): the snapshot is taken when it starts, the
 target is known when its GeometryUpdatesPostponer has applied the new
 geometry (destroyed before this object).
*/
EffectsMaximize::EffectsMaximize( Client* c, bool on )
    : client( on && c->workspace()->windowEffects() != NULL ? c : NULL )
    {
    if( client != NULL )
        client->workspace()->windowEffects()->maximizeBegin( client );
    }

EffectsMaximize::~EffectsMaximize()
    {
    if( client != NULL && client->workspace()->windowEffects() != NULL )
        client->workspace()->windowEffects()->maximizeEnd( client );
    }

#else // EFFECTS_COMPOSITE

Effects::Effects( Workspace* w ) : ws( w ) {}
Effects::~Effects() {}
void Effects::reconfigure() {}
bool Effects::x11Event( XEvent* ) { return false; }
bool Effects::isActive() const { return false; }
void Effects::abort() {}
void Effects::minimize( Client*, bool ) {}
void Effects::unminimize( Client*, bool ) {}
void Effects::maximizeBegin( Client* ) {}
void Effects::maximizeEnd( Client* ) {}
void Effects::windowOpening( Client* ) {}
void Effects::windowClosing( Client* ) {}
void Effects::clientRemoved( Client* ) {}
void Effects::frameTimeout() {}
void Effects::pollTimeout() {}
void Effects::presentCompleted( bool, int ) {}
void Effects::presentIdle() {}
EffectsMaximize::EffectsMaximize( Client*, bool ) : client( NULL ) {}
EffectsMaximize::~EffectsMaximize() {}

#endif

} // namespace

#include "effects.moc"
