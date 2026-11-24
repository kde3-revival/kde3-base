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
#include "compositor.h"
#include "render.h"

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

// downscaled copies of the windows (see EffectPicture::copy()): halved
// across and along separately, down to 1/64 (a window minimized into a
// taskbar button is drawn at about 1/30); none for windows larger than
// 4096x2160.  In GPU memory: the copies along the diagonal (a scale alike
// in both directions) take a third of the window more; all pairs of
// levels, which the genie and the morphs may make over an animation, at
// most about three times the window (they go with the animation)
const int CopyLevels = 7;
const long CopyLimit = 4096L * 2160;
// the first repaint of a window shown or resized: damage, then quiet for
// this long; or at most this long
const double QuietTime = 40.0;
const double MaxWait = 300.0;
// a window growing to its maximized size waits at most this long before it
// moves (its new contents are shown from the start)
const double MaxGrowWait = 150.0;
// the new contents of a maximized window fade in; of a growing one quickly
// (its old contents would be shown enlarged)
const double CrossfadeTime = 150.0;
const double GrowCrossfadeTime = 60.0;
// the pixmaps kept for restoring minimized windows (bytes)
const unsigned long RetainLimit = 256UL << 20;
// the genie: a strip's edges move at most this much (pixels) along it (each
// strip is drawn with the edges of its middle: a step of up to half of
// this at its ends, under the anti-aliasing)
const double GenieStep = 0.3;
// at most this high, and about this many strips (2 requests each, one draw
// call with glamor: about 1 us each on a desktop GPU): larger edge steps
// for windows taller than that
const int GenieMaxStrip = 32;
const int GenieMaxStrips = 2000;


// the copy for drawing at this scale (in one direction): downscaled by at
// most a half more, bilinearly
int copyLevel( double scale )
    {
    int level = 0;
    while( level < CopyLevels - 1 && scale * ( 1 << ( level + 1 )) <= 1.0 )
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
    int count = -1, ordering;
    XRectangle* rects = XShapeGetRectangles( dpy, w, ShapeBounding, &count, &ordering );
    bool shaped = count == 0; // (an empty shape: nothing visible)
    shape.clear();
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
 that don't change: the captured ones), as renderer textures: a named
 pixmap of ours, or a window texture of the compositor (external).
*/
class EffectPicture
    {
    public:
        EffectPicture() : tex( NULL ), opaque( NULL ), pixmap( None ), owned( false ), external( false ),
            alpha( false ), keep_alpha( false ), depth( 0 ), visual( NULL ), w( 0 ), h( 0 ) {}
        ~EffectPicture() { dropCopies(); delete opaque; if( !external ) delete tex; }
        bool isNull() const { return tex == NULL; }
        /*
         The window downscaled by 2^lx across and 2^ly down, for drawing it
         at a scale below a half without aliasing (the text shimmering, thin
         lines breaking up): bilinear sampling from a copy downscaled by at
         most a half more.  Separately in each direction: the genie and the
         morphs squeeze a window much more in one direction than in the
         other.  Each copy is made when first needed from the one halved
         less (in both directions, or in one): 2x2, 2x1 or 1x2 boxes,
         sampled bilinearly between their pixels.  The copies of contents
         that change (a window's own pixmap) are dropped when it changes.
         None for very large windows (bilinear only).  *made tells that the
         renderer drew into a copy (its target must be set again).
        */
        Render::Texture* copy( Render::Renderer* r, int lx, int ly, int* cw, int* ch, bool* made )
            {
            if(( lx == 0 && ly == 0 ) || (long) w * h > CopyLimit )
                {
                *cw = w;
                *ch = h;
                return base( r, made );
                }
            const int key = lx * 16 + ly;
            QMap< int, Copy >::ConstIterator it = copies.find( key );
            if( it != copies.end())
                {
                *cw = (*it).w;
                *ch = (*it).h;
                return (*it).tex;
                }
            // from the one halved less: in both directions when possible
            const int px = lx > 0 ? lx - 1 : 0;
            const int py = ly > 0 ? ly - 1 : 0;
            int sw, sh;
            Render::Texture* src = copy( r, px, py, &sw, &sh, made );
            const bool across = px != lx;
            const bool down = py != ly;
            Copy c;
            c.w = across ? QMAX( 1, ( sw + 1 ) / 2 ) : sw;
            c.h = down ? QMAX( 1, ( sh + 1 ) / 2 ) : sh;
            c.tex = r->createOffscreen( c.w, c.h, alpha && keep_alpha );
            if( c.tex == NULL )
                {
                *cw = sw;
                *ch = sh;
                return src;
                }
            r->setTarget( c.tex );
            r->drawTexture( src, RectF( 0, 0, c.w * ( across ? 2 : 1 ), c.h * ( down ? 2 : 1 )),
                RectF( 0, 0, c.w, c.h ), 1.0, Render::BilinearFilter, Render::BlendSource, Render::PadRepeat );
            *made = true;
            copies[ key ] = c;
            *cw = c.w;
            *ch = c.h;
            return c.tex;
            }
        /*
         What the screen shows of the window: itself, or for an ARGB window
         without a compositor its colours without the alpha (the server draws
         them so: Src, the premultiplied colours as they are), in a copy
         without alpha.  Scaled windows are drawn from this: an ARGB window
         looks the same while animated as before and after.  Composited, the
         alpha stays (keep_alpha).
        */
        Render::Texture* base( Render::Renderer* r, bool* made )
            {
            if( !alpha || keep_alpha )
                return tex;
            if( opaque == NULL )
                {
                opaque = r->createOffscreen( w, h, false );
                if( opaque == NULL )
                    return tex;
                r->setTarget( opaque );
                r->drawTexture( tex, 0, 0, 1.0, Render::BlendSource );
                *made = true;
                }
            return opaque;
            }
        bool isBase( Render::Texture* t ) const { return t == tex || ( t == opaque && t != NULL ); }
        void dropCopies()
            {
            delete opaque;
            opaque = NULL;
            for( QMap< int, Copy >::ConstIterator it = copies.begin();
                 it != copies.end();
                 ++it )
                delete (*it).tex;
            copies.clear();
            }
        // pixmap p is owned (a named pixmap) when own
        void set( Render::Renderer* r, Pixmap p, const QSize& size, int d, Visual* vis, bool own, bool keep )
            {
            pixmap = p;
            depth = d;
            visual = vis;
            owned = own;
            external = false;
            keep_alpha = keep;
            w = size.width();
            h = size.height();
            tex = r != NULL ? r->textureFromPixmap( p, w, h, d, vis ) : NULL;
            alpha = tex != NULL && tex->hasAlpha();
            }
        /*
         The contents as they are now, in a texture of their own: a named
         pixmap is the window's, it follows what is drawn into the window
         until the window is resized (KWin repaints the decoration of a
         window being maximized before it resizes it: the morph would show
         the new title bar over the old contents).  The pixmap stays (owned
         as before).  Nothing changes when the copy can't be made.
        */
        void freeze( Render::Renderer* r )
            {
            if( r == NULL || tex == NULL || external )
                return;
            Render::Texture* t = r->createOffscreen( w, h, alpha );
            if( t == NULL )
                return;
            r->acquireClientBuffers(); // (what the X server drew so far)
            r->setTarget( t );
            r->drawTexture( tex, 0, 0, 1.0, Render::BlendSource );
            r->finish(); // (submitted now: before the X server draws into the window again)
            dropCopies();
            delete tex;
            tex = t;
            }
        // the compositor's texture of a window (not owned; its copies are)
        void setExternal( Render::Texture* t, const QSize& size )
            {
            if( t != tex )
                dropCopies();
            tex = t;
            external = true;
            owned = false;
            pixmap = None;
            keep_alpha = true;
            w = size.width();
            h = size.height();
            alpha = t != NULL && t->hasAlpha();
            }
        // releases everything; returns the pixmap when owned and keep is set
        Pixmap release( Display* dpy, bool keep = false )
            {
            Pixmap ret = None;
            dropCopies();
            if( !external )
                delete tex;
            tex = NULL;
            if( pixmap != None && owned )
                {
                if( keep )
                    ret = pixmap;
                else
                    XFreePixmap( dpy, pixmap );
                }
            pixmap = None;
            external = false;
            return ret;
            }
        Render::Texture* tex;
        Render::Texture* opaque; // base() of an ARGB window
        struct Copy
            {
            Render::Texture* tex;
            int w, h;
            };
        QMap< int, Copy > copies; // by lx * 16 + ly
        Pixmap pixmap;
        bool owned;
        bool external;
        bool alpha;
        bool keep_alpha;
        int depth;
        Visual* visual;
        int w, h;
    };

class EffectAnim
    {
    public:
        enum Kind { Iconify, Morph, Open, Close };
        EffectAnim()
            : kind( Iconify ), client( NULL ), frame( None ), shaped( false ), keep_snap( false ),
              style( 1 ), p( 0 ), p_from( 0 ), p_to( 0 ), seg_start( 0 ), seg_duration( 0 ), linear( false ),
              defer( false ), wait( false ), damage( None ), wait_start( 0 ), damage_first( 0 ),
              damage_last( 0 ), ready( true ), fade( 0 ), fade_start( 0 ), grow( false ), moved( false ), opacity( 1.0 ), max_wait( MaxWait ), on_top( false ), use_below( false ), below( None ),
              done( false ), fresh( true ), started( 0 ), live_shape_read( false ), live_shaped( false ),
              fade_only( false )
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
        bool grow; // Morph: to a larger size
        bool moved; // Morph: only moved (to another monitor), the same size
        double opacity; // the window's (_NET_WM_WINDOW_OPACITY, composited), the last one known
        double max_wait; // the longest wait for the repaint
        bool on_top;
        // drawn right above this scene window (None: the bottom one), where
        // its frame was when it started (closed, minimized below others)
        bool use_below;
        Window below;
        bool done;
        bool fresh; // its first frame not drawn yet: the time starts there
        RectF drawn; // where it was drawn last (KWIN_EFFECTS_DUMP)
        double started;
        // composited: the window's contents now (the compositor's), its shape,
        // and the shadow it had when its contents were captured
        EffectPicture live;
        bool live_shape_read;
        bool live_shaped;
        QValueVector< XRectangle > live_shape;
        CompositorShadow shadow;
        bool fade_only; // Open/Close: no growing or shrinking (notifications)
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
        EffectScene() : dirty( true ), ok( false ), root_tex( NULL ), limit( None )
            {
            wins.setAutoDelete( true );
            }
        // (not moving the list's current item: called while iterating it)
        Win* find( Window w ) const
            {
            for( QPtrListIterator< Win > it( wins );
                 it.current() != NULL;
                 ++it )
                if( it.current()->id == w )
                    return it.current();
            return NULL;
            }
        QPtrList< Win > wins;
        // the attributes of the top-level windows, read once per session and
        // forgotten when an event tells that they changed
        QMap< Window, XWindowAttributes > attrs;
        bool dirty;
        bool ok; // nothing below that can't be drawn
        Render::Texture* root_tex; // the root background (tiled), if any
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
      need_immediate( false ),
      blend( NULL ),
      layer( NULL ),
      own_renderer( NULL ),
      rr( NULL ),
      target( NULL ),
      ox( 0 ),
      oy( 0 ),
      comp_session( false ),
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
    delete own_renderer;
    }

void Effects::reconfigure()
    {
    minimize_style = (Style) options->effectsMinimizeAnimation;
    maximize_anim = options->effectsMaximizeAnimation;
    open_close_anim = options->effectsOpenCloseAnimation;
    // speed 0 (slow) .. 10 (fast), 5 normal: 500 .. 125 ms
    base_time = 250.0 * pow( 2.0, ( 5 - options->effectsAnimationSpeed ) / 5.0 );
    if( !options->effectsEnabled || !options->overviewKeepRedirected || minimize_style == None_ )
        { // (no use for the kept contents)
        abort();
        trapErrors( dpy );
        while( !retained.isEmpty())
            dropRetained( retained.begin().key());
        untrapErrors( dpy );
        }
    }

bool Effects::isActive() const
    {
    return out != None || comp_session;
    }

double Effects::duration( double factor ) const
    {
    // (KWIN_EFFECTS_SLOW=n: n times slower, for looking at the frames)
    static const double slow = getenv( "KWIN_EFFECTS_SLOW" ) != NULL
        ? QMAX( 1.0, QMIN( 50.0, atof( getenv( "KWIN_EFFECTS_SLOW" )))) : 1.0;
    return base_time * factor * slow;
    }

bool Effects::enabled() const
    {
    // (not under another compositing manager: our window would be its)
    Compositor* c = compositor();
    if( c == NULL || ( !c->isActive() && Compositor::foreignCompositor( dpy )))
        return false;
    return options->effectsEnabled && have_render && have_composite
        && ws->windowOverview() != NULL && !ws->windowOverview()->isActive();
    }

// c can be animated now (its frame is redirected, it is on this desktop)
bool Effects::canAnimate( Client* c, bool need_shown ) const
    {
    if( !enabled() || c == NULL || c->frameId() == None )
        return false;
    if( !c->isOnCurrentDesktop() || c->isFullScreen() || ( need_shown && !c->isShown( true )))
        return false;
    Compositor* comp = compositor();
    if( comp != NULL && comp->isActive())
        return !comp->isUnredirected( c->frameId());
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
    Compositor* cm = composited_now() ? compositor() : NULL;
    if( cm != NULL && cm->hasWindow( anim->frame ))
        anim->opacity = cm->windowOpacity( anim->frame );
    return anim;
    }

void Effects::freeAnim( EffectAnim* anim )
    {
    trapErrors( dpy );
    if( anim->damage != None )
        XDamageDestroy( dpy, anim->damage );
    anim->damage = None;
    const int w = anim->snap.w;
    const int h = anim->snap.h;
    anim->live.release( dpy );
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

bool Effects::retainedContents( Client* c, Pixmap* pixmap, QSize* size, int* depth, Visual** visual ) const
    {
    QMap< Client*, Retained >::ConstIterator it = retained.find( c );
    if( it == retained.end())
        return false;
    *pixmap = (*it).pixmap;
    *size = (*it).size;
    *depth = (*it).depth;
    *visual = (*it).visual;
    return true;
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
static Window stackedAbove( Workspace* ws, EffectScene* scene, EffectAnim* anim, bool* covered )
    {
    if( scene == NULL ) // (composited: the compositor's scene)
        return ws->compositor()->windowBelow( anim->frame, anim->geom, covered );
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
        showFirstFrame();
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
        anim->snap.set( renderer(), p, geom.size(), depth, visual, true, composited_now());
        anim->keep_snap = true;
        anim->geom = geom; // (where the frame really is)
        captureShadow( anim );
        }
    else
        {
        QMap< Client*, Retained >::Iterator it = retained.find( c );
        if( it != retained.end())
            {
            anim->snap.set( renderer(), (*it).pixmap, (*it).size, (*it).depth, (*it).visual, true, composited_now());
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
        Window below = stackedAbove( ws, scene, anim, &covered );
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
    showFirstFrame();
    }

/*
 Maximize and restore: the window is configured once, to its new size; its
 old contents, scaled, move to the new geometry and the new contents fade
 in when the application has repainted.  The area covers the old and the
 possible new geometries before the frame is resized.
*/
void Effects::maximizeBegin( Client* c )
    {
    QRect bounds = c->geometry().unite( ws->clientArea( MaximizeArea, c ));
    if( c->geometryRestore().isValid())
        bounds = bounds.unite( c->geometryRestore());
    morphBegin( c, bounds );
    }

void Effects::maximizeEnd( Client* c )
    {
    morphEnd( c, false );
    }

/*
 Window to Another Screen: the morph of maximize, from the old geometry to
 the new one on the other monitor (maximized and too large windows change
 their size there).  Only moved, the captured contents are the window's
 own pixmap (named again only when it is resized): the live contents are
 shown throughout, nothing to wait for, no crossfade.
*/
void Effects::screenMoveBegin( Client* c, const QRect& to )
    {
    morphBegin( c, c->geometry().unite( to ));
    }

void Effects::screenMoveEnd( Client* c )
    {
    morphEnd( c, true );
    }

// bounds: the old and the possible new geometries
void Effects::morphBegin( Client* c, const QRect& bounds )
    {
    EffectAnim* anim = findAnim( c );
    if( !maximize_anim || !canAnimate( c ) || c->isShade())
        {
        if( anim != NULL )
            freeAnim( anim );
        return;
        }
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
    anim->snap.set( renderer(), p, geom.size(), depth, visual, true, composited_now());
    anim->snap.freeze( renderer());
    if( mayBeShaped( c ))
        anim->shaped = readShape( dpy, c->frameId(), geom.size(), anim->shape );
    anim->old_geom = anim->geom = geom;
    captureShadow( anim );
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
    showFirstFrame();
    }

// moving: only moved to another monitor (no wait, no crossfade when the
// size stays)
void Effects::morphEnd( Client* c, bool moving )
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
    /*
     Growing, the old contents would be shown enlarged (up to 2.5 times when
     a window is maximized: everything looks huge for a moment): it waits
     for its new contents (at most MaxGrowWait), then moves showing them,
     scaled down to where it is, after a quick crossfade.  Shrinking, the
     old contents are scaled down, the new ones would be enlarged: they fade
     in towards the end.
    */
    anim->grow = (long) anim->geom.width() * anim->geom.height()
        > (long) anim->old_geom.width() * anim->old_geom.height();
    anim->defer = anim->grow;
    anim->max_wait = anim->grow ? MaxGrowWait : MaxWait;
    anim->fade = 0;
    anim->fade_start = 0;
    // (within a pixel: a move again while moving starts from where the
    // window was seen, rounded outwards)
    anim->moved = moving && QABS( anim->geom.width() - anim->old_geom.width()) <= 1
        && QABS( anim->geom.height() - anim->old_geom.height()) <= 1;
    if( anim->moved )
        {
        anim->wait = false;
        anim->ready = true;
        anim->defer = false;
        anim->fade = 1;
        }
    else
        anim->wait = true; // (again after a move)
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
// (a notification only fades in)
void Effects::windowOpening( Client* c )
    {
    const bool fade_only = c->isNotification();
    if( !open_close_anim || ( !fade_only && ( !( c->isNormalWindow() || c->isDialog()) || c->isSpecialWindow()))
        || c->isMinimized() || !canAnimate( c, false ) || findAnim( c ) != NULL )
        return;
    // the new frame is still on top of everything (KWin restacks it later):
    // below the support window, like all frames, before anything is read
    XWindowChanges wc;
    wc.sibling = ws->supportWindowId();
    wc.stack_mode = Below;
    XConfigureWindow( dpy, c->frameId(), CWSibling | CWStackMode, &wc );
    EffectAnim* anim = newAnim( EffectAnim::Open, c );
    anim->fade_only = fade_only;
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
    showFirstFrame();
    poll_timer.start( 10, true );
    }

// a closed window: its last contents shrink a little and fade out where it was
// (a notification only fades out)
void Effects::windowClosing( Client* c )
    {
    EffectAnim* anim = findAnim( c );
    if( anim != NULL )
        freeAnim( anim );
    dropRetained( c );
    const bool fade_only = c->isNotification();
    if( !open_close_anim || ( !fade_only && ( !( c->isNormalWindow() || c->isDialog()) || c->isSpecialWindow()))
        || !canAnimate( c ))
        return;
    anim = newAnim( EffectAnim::Close, c );
    anim->fade_only = fade_only;
    Pixmap p;
    QRect geom;
    int depth;
    Visual* visual;
    if( !namePixmap( c->frameId(), &p, &geom, &depth, &visual ))
        {
        delete anim;
        return;
        }
    anim->snap.set( renderer(), p, geom.size(), depth, visual, true, composited_now());
    if( mayBeShaped( c ))
        anim->shaped = readShape( dpy, c->frameId(), geom.size(), anim->shape );
    anim->geom = geom;
    captureShadow( anim );
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
    anim->below = stackedAbove( ws, scene, anim, &covered );
    anim->use_below = true;
    anim->client = NULL; // nothing more from the client
    if( debug )
        kdWarning( 1212 ) << "effects: close " << anim->caption << endl;
    showFirstFrame();
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
    if( out != None || comp_session )
        return extendArea( needed );
    const double t = now();
    if( composited_now())
        { // in the compositor's frames: no window, no scene of our own
        Compositor* c = compositor();
        if( c->lockerShown())
            return false;
        QRect screen( 0, 0, DisplayWidth( dpy, DefaultScreen( dpy )), DisplayHeight( dpy, DefaultScreen( dpy )));
        area = needed.intersect( screen );
        if( area.isEmpty())
            return false;
        comp_session = true;
        comp_drawn = QRegion();
        frame_count = 0;
        stat_start = t;
        stat_render = stat_max_render = 0;
        stat_skipped = stat_gaps = 0;
        stat_strips = 0;
        stat_setup = now() - t;
        return true;
        }
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
    createWindow();
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

/*
 Our window covers exactly the area (Present syncs to the monitor that
 shows most of the window), takes no input and has no background: mapped,
 it shows what the screen showed there until the first frame is copied
 into it.  A larger area gets a new window (the frames queued for the old
 one go with it, they would be shown at the wrong place).
*/
void Effects::createWindow()
    {
    Window old = out;
    XSetWindowAttributes attr;
    attr.override_redirect = True;
    attr.background_pixmap = None;
    attr.border_pixel = 0;
    out = XCreateWindow( dpy, root, area.x(), area.y(), area.width(), area.height(), 0, CopyFromParent,
        InputOutput, CopyFromParent, CWOverrideRedirect | CWBackPixmap | CWBorderPixel, &attr );
    XShapeCombineRectangles( dpy, out, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted );
    placeWindow();
    XMapWindow( dpy, out );
    presenter->start( out, 0, 0, area.width(), area.height(), have_present );
    if( old != None )
        XDestroyWindow( dpy, old ); // (below the new one, which shows what it showed)
    need_immediate = true;
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
    if( comp_session )
        {
        area = area.unite( needed ).intersect( QRect( 0, 0, DisplayWidth( dpy, DefaultScreen( dpy )),
            DisplayHeight( dpy, DefaultScreen( dpy ))));
        return true;
        }
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
    createWindow();
    return true;
    }

void Effects::endSession()
    {
    frame_timer.stop();
    poll_timer.stop();
    frame_wanted = false;
    finishing = false;
    comp_session = false;
    comp_drawn = QRegion();
    target = NULL;
    trapErrors( dpy );
    presenter->stop();
    freeBlendTarget();
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
        delete scene->root_tex;
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
        QMap< Window, XWindowAttributes >::ConstIterator cached = scene->attrs.find( w );
        if( cached != scene->attrs.end())
            attr = *cached;
        else
            {
            if( !XGetWindowAttributes( dpy, w, &attr ))
                continue;
            scene->attrs[ w ] = attr;
            }
        if( attr.map_state != IsViewable || attr.c_class != InputOutput || attr.width <= 0 || attr.height <= 0 )
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
            win->pic.set( renderer(), p, geom.size(), attr.depth, attr.visual, true, false );
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
    if( scene->root_tex == NULL )
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
            Window r;
            int x, y;
            unsigned int w, h, bw, depth;
            if( XGetGeometry( dpy, p, &r, &x, &y, &w, &h, &bw, &depth ) && renderer() != NULL )
                scene->root_tex = renderer()->textureFromPixmap( p, w, h, depth,
                    DefaultVisual( dpy, DefaultScreen( dpy )));
            }
        if( data != NULL )
            XFree( data );
        }
    if( untrapErrors( dpy ) > 0 )
        {
        if( scene->root_tex != NULL )
            { // (a stale root pixmap: none)
            trapErrors( dpy );
            delete scene->root_tex;
            untrapErrors( dpy );
            scene->root_tex = NULL;
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

// the first frame of a new animation: at once when our window is new (else
// the frames shown or queued still show the window as it was)
void Effects::showFirstFrame()
    {
    if( need_immediate )
        renderFrame( true );
    requestFrame();
    }

void Effects::requestFrame()
    {
    if( comp_session )
        {
        compositor()->requestFrame();
        return;
        }
    if( out == None )
        return;
    if( finishing && anims.isEmpty())
        return; // the plain scene is being shown: no more frames (a video below)
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
    if( comp_session )
        compositor()->requestFrame();
    else if( out != None && !frame_timer.isActive())
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
        EffectScene::Win* win = scene != NULL ? scene->find( anim->frame ) : NULL;
        QRect g;
        if( win != NULL )
            anim->geom = win->geom;
        else if( comp_session && compositor()->windowGeometry( anim->frame, &g ))
            anim->geom = g;
        else if( anim->client != NULL && ws->hasClient( anim->client ))
            anim->geom = anim->client->geometry();
        }
    if( anim->wait && !anim->ready
        && (( anim->damage_first > 0 && t - anim->damage_last >= QuietTime ) || t - anim->wait_start >= anim->max_wait ))
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
            {
            if( anim->moved )
                { // (the live contents, see screenMoveBegin())
                anim->fade = 1;
                anim->done = lin >= 1;
                break;
                }
            if( anim->ready && anim->fade_start == 0 )
                anim->fade_start = t;
            // the crossfade: quick when growing (it starts with the motion); late
            // when shrinking (by the progress, at least CrossfadeTime after the repaint)
            double fade = 0;
            if( anim->fade_start > 0 )
                {
                if( anim->grow )
                    fade = clamp01(( t - anim->fade_start ) / GrowCrossfadeTime );
                else
                    fade = QMIN( clamp01(( t - anim->fade_start ) / CrossfadeTime ),
                        clamp01(( anim->p - 0.6 ) / 0.4 ));
                }
            anim->fade = fade;
            if( anim->fade_start > 0 && anim->fade < 1 )
                moving = true;
            anim->done = lin >= 1 && anim->fade >= 1;
            break;
            }
        case EffectAnim::Open:
        case EffectAnim::Close:
            anim->done = lin >= 1;
            break;
        }
    return moving;
    }

// the level of the copy (see copyLevel()) and how far the scale is from
// its size towards the next smaller one, 0 .. 1 (log2)
static int copyLevelFraction( double scale, double* fraction )
    {
    const int level = copyLevel( scale );
    const double lod = scale > 0 ? log( 1.0 / scale ) / log( 2.0 ) : 0;
    *fraction = level < CopyLevels - 1 ? clamp01( lod - level ) : 0;
    return level;
    }

// the shape rectangles relative to the window (size), scaled into rect
// (screen coordinates)
static QRegion scaledShapeRegion( const QValueVector< XRectangle >& shape, const QSize& size, const RectF& rect )
    {
    QValueVector< XRectangle > clip = scaledShape( shape, size, rect, 0, 0 );
    QRegion r;
    for( unsigned int i = 0;
         i < clip.size();
         ++i )
        if( clip[ i ].width > 0 && clip[ i ].height > 0 )
            r |= QRect( clip[ i ].x, clip[ i ].y, clip[ i ].width, clip[ i ].height );
    return r;
    }

void Effects::beginDraw( Render::Renderer* r, Render::Texture* t, int x, int y, const QRegion& clip )
    {
    rr = r;
    target = t;
    ox = x;
    oy = y;
    clip_base = clip;
    restoreTarget();
    }

// (after drawing into a copy or the scratch picture)
void Effects::restoreTarget()
    {
    rr->setTarget( target );
    }

void Effects::setClip( const QRegion& screen_region )
    {
    QRegion r = screen_region & clip_base;
    r.translate( -ox, -oy );
    rr->setClip( r );
    }

/*
 pic drawn into rect (screen coordinates), with alpha.

 Scaled down, a bilinear filter skips pixels (the text shimmers and
 breaks up as the window moves): drawn from the copies downscaled by at
 most a half more (EffectPicture::copy()), the two levels around the
 scale blended in each direction by how far the scale is between them
 (bilinearly over up to four copies, like trilinear filtering, but per
 direction: a morph scales the two directions differently): no aliasing
 and no jump in sharpness when a level changes.  The blend is composed in
 a scratch picture and drawn with alpha from there: exact also for
 translucent windows.  Scaled up (the old contents of a maximized window),
 bilinear.  Each copy is drawn with its nominal size (the window's size /
 2^level; a copy is rounded up by up to a pixel, which falls outside
 rect), so that all copies land at the same place, to the subpixel.
*/
void Effects::drawPicture( EffectPicture& pic, const RectF& rect, double alpha, bool shaped,
    const QValueVector< XRectangle >& shape )
    {
    if( pic.isNull() || alpha <= 1.0 / 512 || rect.w < 0.5 || rect.h < 0.5 || ( shaped && shape.isEmpty()))
        return;
    double fx, fy;
    const int lx = copyLevelFraction( rect.w / pic.w, &fx );
    const int ly = copyLevelFraction( rect.h / pic.h, &fy );
    // the copies and their weights
    struct { int lx, ly; double w; Render::Texture* tex; RectF src; } parts[ 4 ];
    int count = 0;
    const bool copies = (long) pic.w * pic.h <= CopyLimit;
    for( int i = 0;
         i < 4;
         ++i )
        {
        const int bx = i & 1;
        const int by = i >> 1;
        const double w = ( bx ? fx : 1 - fx ) * ( by ? fy : 1 - fy );
        if( w <= 1.0 / 512 || ( !copies && ( bx || by )))
            continue;
        parts[ count ].lx = lx + bx;
        parts[ count ].ly = ly + by;
        parts[ count ].w = w;
        ++count;
        }
    if( count == 0 )
        {
        parts[ 0 ].lx = lx;
        parts[ 0 ].ly = ly;
        parts[ 0 ].w = 1;
        count = 1;
        }
    double sum = 0;
    for( int i = 0;
         i < count;
         ++i )
        sum += parts[ i ].w;
    // the copies first (making one draws into it)
    bool made = false;
    for( int i = 0;
         i < count;
         ++i )
        {
        int cw, ch;
        parts[ i ].tex = pic.copy( rr, parts[ i ].lx, parts[ i ].ly, &cw, &ch, &made );
        const bool base = pic.isBase( parts[ i ].tex );
        parts[ i ].src = RectF( 0, 0, base ? pic.w : double( pic.w ) / ( 1 << parts[ i ].lx ),
            base ? pic.h : double( pic.h ) / ( 1 << parts[ i ].ly ));
        }
    if( made )
        restoreTarget();
    const int dx = (int) floor( rect.x );
    const int dy = (int) floor( rect.y );
    const int dw = (int) ceil( rect.x + rect.w ) - dx;
    const int dh = (int) ceil( rect.y + rect.h ) - dy;
    const QRegion clip = shaped ? scaledShapeRegion( shape, QSize( pic.w, pic.h ), rect ) : QRegion( dx, dy, dw, dh );
    const RectF dst( rect.x - ox, rect.y - oy, rect.w, rect.h );
    if( count == 1 )
        { // one copy (or the window itself): drawn directly
        const bool identity = pic.isBase( parts[ 0 ].tex ) && fabs( rect.w - pic.w ) < 1e-9
            && fabs( rect.h - pic.h ) < 1e-9 && rect.x == dx && rect.y == dy;
        // at scale 1 like the server's automatic compositing (Src, also for
        // ARGB windows; composited: with their alpha), else blended (the
        // edges are anti-aliased)
        const Render::Blend blend = identity && alpha >= 1 && !( pic.alpha && pic.keep_alpha )
            ? Render::BlendSource : Render::BlendOver;
        setClip( clip );
        rr->drawTexture( parts[ 0 ].tex, parts[ 0 ].src, dst, QMIN( 1.0, alpha ),
            identity ? Render::NearestFilter : Render::BilinearFilter, blend, Render::NoRepeat );
        }
    else if( blendTarget( dw, dh ))
        { // the weighted sum in the scratch picture (premultiplied, also its alpha), then over dst
        rr->setTarget( blend );
        rr->fill( RectF( 0, 0, dw, dh ), Qt::black, 0.0, Render::BlendSource );
        const RectF local( rect.x - dx, rect.y - dy, rect.w, rect.h );
        for( int i = 0;
             i < count;
             ++i )
            rr->drawTexture( parts[ i ].tex, parts[ i ].src, local, parts[ i ].w / sum, Render::BilinearFilter,
                Render::BlendAdd, Render::NoRepeat );
        restoreTarget();
        setClip( clip );
        rr->drawTexture( blend, RectF( 0, 0, dw, dh ), RectF( dx - ox, dy - oy, dw, dh ), QMIN( 1.0, alpha ),
            Render::NearestFilter, Render::BlendOver, Render::NoRepeat );
        }
    }

// the scratch picture for blending copies (ARGB), at least w x h
bool Effects::blendTarget( int w, int h )
    {
    if( blend != NULL && w <= blend->width() && h <= blend->height())
        return true;
    const int bw = QMAX( w, blend != NULL ? blend->width() : 0 );
    const int bh = QMAX( h, blend != NULL ? blend->height() : 0 );
    delete blend; // (only it: the layer may be the target now)
    blend = rr->createOffscreen( bw, bh, true );
    return blend != NULL;
    }

// the scratch picture for the crossfade of a translucent window, at least w x h
bool Effects::layerTarget( int w, int h )
    {
    if( layer != NULL && w <= layer->width() && h <= layer->height())
        return true;
    const int lw = QMAX( w, layer != NULL ? layer->width() : 0 );
    const int lh = QMAX( h, layer != NULL ? layer->height() : 0 );
    delete layer;
    layer = rr->createOffscreen( lw, lh, true );
    return layer != NULL;
    }

void Effects::freeBlendTarget()
    {
    delete blend;
    blend = NULL;
    delete layer;
    layer = NULL;
    }

// the window's contents now (the scene's, the compositor's)
EffectPicture* Effects::livePicture( EffectAnim* anim )
    {
    if( comp_session )
        {
        Compositor* c = compositor();
        QRect geom;
        Render::Texture* t = c != NULL ? c->windowTexture( anim->frame, &geom ) : NULL;
        if( t == NULL )
            {
            anim->live.setExternal( NULL, QSize());
            return NULL;
            }
        anim->live.setExternal( t, geom.size());
        if( !anim->live_shape_read )
            { // (with the compositor's shape: the same as it draws)
            QRegion shape = c->windowShape( anim->frame );
            anim->live_shaped = !shape.isNull();
            anim->live_shape.clear();
            QMemArray< QRect > rects = shape.rects();
            for( unsigned int i = 0;
                 i < rects.size();
                 ++i )
                {
                XRectangle xr;
                xr.x = rects[ i ].x();
                xr.y = rects[ i ].y();
                xr.width = rects[ i ].width();
                xr.height = rects[ i ].height();
                anim->live_shape.append( xr );
                }
            anim->live_shape_read = true;
            }
        return &anim->live;
        }
    EffectScene::Win* live = scene != NULL ? scene->find( anim->frame ) : NULL;
    if( live == NULL || live->pic.isNull())
        return NULL;
    anim->live_shaped = live->shaped;
    anim->live_shape = live->shape;
    return &live->pic;
    }

// the window's shadow (composited only), scaled into rect with alpha;
// live: the window's shadow now, else the one captured with its contents
void Effects::drawShadow( EffectAnim* anim, bool live, const RectF& rect, double alpha )
    {
    if( !comp_session || alpha <= 1.0 / 512 )
        return;
    Compositor* c = compositor();
    if( c == NULL )
        return;
    CompositorShadow s;
    if( live )
        {
        if( !c->windowShadow( anim->frame, &s ))
            return;
        }
    else
        s = anim->shadow;
    if( !s.valid )
        return;
    // (outside the window: below a translucent one it would show; live:
    // outside its shape now, scaled with it)
    QRegion inside;
    if( !s.alpha_mask.isEmpty())
        { // made from the alpha channel: below the transparent parts too
        QValueVector< XRectangle > opaque;
        QMemArray< QRect > rects = s.opaque.rects();
        for( unsigned int i = 0;
             i < rects.size();
             ++i )
            {
            XRectangle xr;
            xr.x = rects[ i ].x();
            xr.y = rects[ i ].y();
            xr.width = rects[ i ].width();
            xr.height = rects[ i ].height();
            opaque.append( xr );
            }
        inside = scaledShapeRegion( opaque, s.size, rect );
        }
    else if( live && !s.shape.isNull())
        {
        QValueVector< XRectangle > shape;
        QMemArray< QRect > rects = s.shape.rects();
        for( unsigned int i = 0;
             i < rects.size();
             ++i )
            {
            XRectangle xr;
            xr.x = rects[ i ].x();
            xr.y = rects[ i ].y();
            xr.width = rects[ i ].width();
            xr.height = rects[ i ].height();
            shape.append( xr );
            }
        inside = scaledShapeRegion( shape, s.size, rect );
        }
    else if( anim->shaped && !live )
        inside = scaledShapeRegion( anim->shape, s.size, rect );
    else
        inside = QRegion( rect.bounds());
    QRegion clip = clip_base - inside;
    clip.translate( -ox, -oy );
    if( clip.isEmpty())
        return;
    c->drawShadow( rr, s, RectF( rect.x - ox, rect.y - oy, rect.w, rect.h ), alpha, clip );
    }

void Effects::drawAnim( EffectAnim* anim )
    {
    // translucent windows stay so while animated (the compositor draws
    // them with their opacity; a closed one keeps the last one known)
    if( comp_session )
        {
        Compositor* cm = compositor();
        if( cm != NULL && cm->hasWindow( anim->frame ))
            anim->opacity = cm->windowOpacity( anim->frame );
        }
    const double op = comp_session ? anim->opacity : 1.0;
    switch( anim->kind )
        {
        case EffectAnim::Iconify:
            {
            EffectPicture* pic = &anim->snap;
            const QValueVector< XRectangle >* shape = &anim->shape;
            bool shaped = anim->shaped;
            bool live = false;
            if( pic->isNull())
                {
                pic = livePicture( anim );
                if( pic == NULL )
                    return;
                shape = &anim->live_shape;
                shaped = anim->live_shaped;
                live = true;
                }
            if( anim->p <= 0 && anim->p_to == 0 )
                { // restored, waiting for the repaint
                drawShadow( anim, live, RectF( anim->geom ), 1.0 );
                drawPicture( *pic, RectF( anim->geom ), op, shaped, *shape );
                return;
                }
            if( anim->style == Genie )
                {
                // (the shadow goes at once: the funnel has none)
                drawShadow( anim, live, RectF( anim->geom ), 1 - clamp01( anim->p / 0.15 ));
                drawGenie( anim, *pic, true, anim->p, op );
                return;
                }
            RectF target_rect = fitInto( anim->geom.width(), anim->geom.height(), anim->icon );
            RectF rect = mix( RectF( anim->geom ), target_rect, anim->p );
            anim->drawn = rect;
            const double alpha = 1 - anim->p * anim->p * anim->p * anim->p;
            drawShadow( anim, live, rect, alpha * ( 1 - anim->p ));
            drawPicture( *pic, rect, alpha * op, shaped, *shape );
            break;
            }
        case EffectAnim::Morph:
            {
            RectF rect = mix( RectF( anim->old_geom ), RectF( anim->geom ), anim->p );
            anim->drawn = rect;
            // the shadow before and after (maximized: none) crossfade
            drawShadow( anim, false, rect, 1 - anim->p );
            drawShadow( anim, true, rect, anim->p );
            EffectPicture* live = anim->fade > 0 ? livePicture( anim ) : NULL;
            const bool snap = anim->fade < 1 || ( anim->moved && live == NULL );
            const int dx = (int) floor( rect.x );
            const int dy = (int) floor( rect.y );
            const int dw = (int) ceil( rect.x + rect.w ) - dx;
            const int dh = (int) ceil( rect.y + rect.h ) - dy;
            if( snap && live != NULL && op < 1 && dw > 0 && dh > 0 && layerTarget( dw, dh ))
                { // both: crossfaded at full opacity, then drawn with the window's
                  // (two translucent layers would add up to more)
                Render::Texture* const frame_target = target;
                const int frame_ox = ox;
                const int frame_oy = oy;
                const QRegion frame_clip = clip_base;
                target = layer;
                ox = dx;
                oy = dy;
                clip_base = QRegion( dx, dy, dw, dh );
                restoreTarget();
                rr->fill( RectF( 0, 0, dw, dh ), Qt::black, 0.0, Render::BlendSource );
                drawPicture( anim->snap, rect, 1.0, anim->shaped, anim->shape );
                drawPicture( *live, rect, anim->fade, anim->live_shaped, anim->live_shape );
                target = frame_target;
                ox = frame_ox;
                oy = frame_oy;
                clip_base = frame_clip;
                restoreTarget();
                setClip( QRegion( dx, dy, dw, dh ));
                rr->drawTexture( layer, RectF( 0, 0, dw, dh ), RectF( dx - ox, dy - oy, dw, dh ), op,
                    Render::NearestFilter, Render::BlendOver, Render::NoRepeat );
                break;
                }
            if( snap )
                drawPicture( anim->snap, rect, op, anim->shaped, anim->shape );
            if( live != NULL )
                drawPicture( *live, rect, anim->fade * op, anim->live_shaped, anim->live_shape );
            break;
            }
        case EffectAnim::Open:
        case EffectAnim::Close:
            {
            EffectPicture* pic = &anim->snap;
            const QValueVector< XRectangle >* shape = &anim->shape;
            bool shaped = anim->shaped;
            const bool live = anim->kind == EffectAnim::Open;
            if( live )
                {
                if( !anim->ready )
                    return;
                pic = livePicture( anim );
                if( pic == NULL )
                    return;
                shape = &anim->live_shape;
                shaped = anim->live_shaped;
                }
            const double s = anim->fade_only ? 1.0
                : anim->kind == EffectAnim::Open ? 0.9 + 0.1 * anim->p : 0.85 + 0.15 * anim->p;
            RectF g( anim->geom );
            RectF rect( g.cx() - g.w * s / 2, g.cy() - g.h * s / 2, g.w * s, g.h * s );
            if( anim->p >= 1 )
                rect = g;
            drawShadow( anim, live, rect, anim->p );
            drawPicture( *pic, rect, anim->p * op, shaped, *shape );
            break;
            }
        }
    }

/*
 The genie: the window is pulled into its taskbar button through a funnel
 that narrows from the window's width to the button's.  First its near
 edge is pulled to the button while the edges curve in (the window
 stretches), then its far edge follows (the window is squeezed into the
 button).

 Drawn as strips across the direction of travel, a whole number of pixel
 rows each (no seams, no overlaps), each scaled across on its own to the
 funnel's width in its middle, all alike along; a strip is one row where
 the edges curve, more where they move less than GenieStep along it.  The
 edges are where the funnel is, to the subpixel, anti-aliased by the
 bilinear filter (outside the window is transparent).  Where the window
 is squeezed to less than half, a copy downscaled across and/or along is
 drawn (made once, see EffectPicture::copy()): no aliasing, no shimmer of
 the text.

 Computed along the main axis m (the direction of travel, negated for up
 and left so that it increases towards the button) and the cross axis.
*/
void Effects::drawGenie( EffectAnim* anim, EffectPicture& pic, bool scaled_copies, double p, double alpha )
    {
    const int sw = pic.w;
    const int sh = pic.h;
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
    const double width = horizontal ? sh : sw;
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
    // the funnel's edges at m
#define GENIE_F( m ) ( s1 * smoothstep((( m ) - m0 ) / ( i0 - m0 )))
#define GENIE_L( m ) mix( wc0, ic0, GENIE_F( m ))
#define GENIE_R( m ) mix( wc1, ic1, GENIE_F( m ))
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
            const double l0 = GENIE_L( m );
            const double r0 = GENIE_R( m );
            int h = 1;
            while( h < GenieMaxStrip && m + h < end
                && fabs( GENIE_L( m + h + 1 ) - l0 ) <= step && fabs( GENIE_R( m + h + 1 ) - r0 ) <= step )
                ++h;
            heights.append( h );
            m += h;
            }
        if( (int) heights.size() <= GenieMaxStrips || step > 16 )
            break;
        }
    // the levels of the downscaled copies along (the same for all strips)
    int level_along = 0;
    if( scaled_copies )
        level_along = copyLevel( sy );
    // the copies first (making one draws into it)
    bool made = false;
    int m = begin;
    for( unsigned int i = 0;
         i < heights.size();
         m += heights[ i ], ++i )
        {
        const double sx = ( GENIE_R( m + heights[ i ] / 2.0 ) - GENIE_L( m + heights[ i ] / 2.0 )) / width;
        if( sx <= 0.0005 )
            continue;
        const int level_across = scaled_copies ? copyLevel( sx ) : 0;
        int gw, gh;
        if( horizontal )
            pic.copy( rr, level_along, level_across, &gw, &gh, &made );
        else
            pic.copy( rr, level_across, level_along, &gw, &gh, &made );
        }
    if( made )
        restoreTarget();
    setClip( QRegion( 0, 0, DisplayWidth( dpy, DefaultScreen( dpy )), DisplayHeight( dpy, DefaultScreen( dpy ))));
    int strips = 0;
    m = begin;
    for( unsigned int i = 0;
         i < heights.size();
         ++i )
        {
        const int h = heights[ i ];
        const double cl = GENIE_L( m + h / 2.0 );
        const double cr = GENIE_R( m + h / 2.0 );
        const double sx = ( cr - cl ) / width; // across
        if( sx > 0.0005 )
            {
            const int level_across = scaled_copies ? copyLevel( sx ) : 0;
            int gw, gh;
            bool dummy = false;
            Render::Texture* src = horizontal ? pic.copy( rr, level_along, level_across, &gw, &gh, &dummy )
                : pic.copy( rr, level_across, level_along, &gw, &gh, &dummy );
            // the copy's pixels per window pixel
            const double fa = horizontal ? double( gh ) / sh : double( gw ) / sw; // across
            const double fl = horizontal ? double( gw ) / sw : double( gh ) / sh; // along
            const int c0 = (int) floor( cl );
            const int c1 = (int) ceil( cr );
            // the window's pixel shown at the box's pixel b, along: u = ( m + b - top ) / sy
            // from the far edge (sign > 0), len - that (sign < 0); across: ( c0 + b - cl ) / sx
            const double along_off = sign > 0 ? ( m - top ) / sy : len - ( m + h - top ) / sy;
            const double across_off = ( c0 - cl ) / sx;
            RectF src_rect, dst_rect;
            if( !horizontal )
                {
                dst_rect = RectF( c0 - ox, ( sign > 0 ? m : -( m + h )) - oy, c1 - c0, h );
                src_rect = RectF( fa * across_off, fl * along_off, dst_rect.w * fa / sx, dst_rect.h * fl / sy );
                }
            else
                {
                dst_rect = RectF(( sign > 0 ? m : -( m + h )) - ox, c0 - oy, h, c1 - c0 );
                src_rect = RectF( fl * along_off, fa * across_off, dst_rect.w * fl / sy, dst_rect.h * fa / sx );
                }
            rr->drawTexture( src, src_rect, dst_rect, alpha, Render::BilinearFilter, Render::BlendOver,
                Render::NoRepeat );
            ++strips;
            }
        m += h;
        }
#undef GENIE_F
#undef GENIE_L
#undef GENIE_R
    if( strips > stat_strips )
        stat_strips = strips;
    }

// the animations at time t0 (finished ones freed); *moving: something
// moves, *waiting: something waits for a window's repaint
void Effects::advance( double t0, bool* moving, bool* waiting )
    {
    *moving = false;
    *waiting = false;
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
            *moving = true;
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
        else if( !*moving )
            *waiting = true;
        }
    }

void Effects::renderFrame( bool immediate )
    {
    if( comp_session )
        { // (the compositor draws: a frame there)
        compositor()->requestFrame();
        return;
        }
    if( out == None || scene == NULL )
        return;
    if( need_immediate )
        immediate = true; // a new window: nothing shown in it yet
    if( scene->dirty )
        {
        buildScene();
        if( !scene->ok )
            { // something that can't be drawn appeared (the screen locker)
            abort();
            return;
            }
        }
    if( presenter->beginFrame() == None )
        { // all still used by the server, wait for an idle event
        frame_wanted = true;
        return;
        }
    Render::Renderer* r = renderer();
    Render::Texture* t = r != NULL ? r->textureFromPixmap( presenter->framePixmap(), area.width(), area.height(),
        DefaultDepth( dpy, DefaultScreen( dpy )), DefaultVisual( dpy, DefaultScreen( dpy ))) : NULL;
    if( t == NULL )
        {
        abort();
        return;
        }
    const double t_start = now();
    const double t0 = immediate ? t_start : presenter->frameTime( t_start );
    bool moving, waiting;
    advance( t0, &moving, &waiting );
    beginDraw( r, t, area.x(), area.y(), QRegion( area ));
    // the topmost window that covers the whole area, opaque, not animated:
    // nothing below it is drawn (maximized windows below a maximized one)
    EffectScene::Win* cover = NULL;
    for( QPtrListIterator< EffectScene::Win > wit( scene->wins );
         wit.current() != NULL;
         ++wit )
        {
        EffectScene::Win* win = wit.current();
        if( win->pic.isNull() || win->pic.alpha || win->shaped || !win->geom.contains( area ))
            continue;
        bool animated = false;
        for( QPtrListIterator< EffectAnim > ait( anims );
             ait.current() != NULL;
             ++ait )
            if( ait.current()->frame == win->id )
                animated = true;
        if( !animated )
            cover = win;
        }
    if( cover == NULL )
        { // the background
        setClip( QRegion( area ));
        if( scene->root_tex != NULL )
            r->drawTexture( scene->root_tex, RectF( ox, oy, area.width(), area.height()),
                RectF( 0, 0, area.width(), area.height()), 1.0, Render::NearestFilter, Render::BlendSource,
                Render::TileRepeat );
        else
            r->fill( RectF( 0, 0, area.width(), area.height()), Qt::black, 1.0, Render::BlendSource );
        // closing windows above the bottom one
        for( EffectAnim* anim = anims.first();
             anim != NULL;
             anim = anims.next())
            if( anim->use_below && anim->below == None )
                drawAnim( anim );
        }
    bool covered = cover != NULL;
    for( QPtrListIterator< EffectScene::Win > wit( scene->wins );
         wit.current() != NULL;
         ++wit )
        {
        EffectScene::Win* win = wit.current();
        if( covered )
            {
            if( win != cover )
                continue; // (and what was drawn above it)
            covered = false;
            }
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
            drawAnim( in_place );
        else if( !animated && win->geom.intersects( area ) && !win->pic.isNull()
            && !( win->shaped && win->shape.isEmpty()))
            {
            setClip( win->shaped ? scaledShapeRegion( win->shape, win->geom.size(), RectF( win->geom ))
                : QRegion( win->geom ));
            r->drawTexture( win->pic.tex, win->geom.x() - ox, win->geom.y() - oy, 1.0, Render::BlendSource );
            }
        for( EffectAnim* anim = anims.first();
             anim != NULL;
             anim = anims.next())
            if( anim->use_below && anim->below == win->id )
                drawAnim( anim );
        }
    // on top: minimized and restored windows, closed ones whose neighbour went
    for( EffectAnim* anim = anims.first();
         anim != NULL;
         anim = anims.next())
        {
        if( anim->use_below )
            {
            if( anim->below != None && scene->find( anim->below ) == NULL )
                drawAnim( anim );
            }
        else if( anim->on_top )
            drawAnim( anim );
        }
    r->finish();
    delete t;
    target = NULL;
    if( !dump_dir.isEmpty())
        dumpFrame();
    presenter->endFrame( immediate );
    need_immediate = false;
    ++frame_count;
    if( debug )
        {
        XSync( dpy, False );
        double tr = now() - t_start;
        stat_render += tr;
        stat_max_render = QMAX( stat_max_render, tr );
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

//****************************************
// inside the compositor's frames
//****************************************

Compositor* Effects::compositor() const
    {
    return ws->compositor();
    }

bool Effects::composited_now() const
    {
    return ws->compositor() != NULL && ws->compositor()->isActive();
    }

// the window's shadow as it is now (composited), drawn with its captured contents
void Effects::captureShadow( EffectAnim* anim )
    {
    anim->shadow = CompositorShadow();
    if( composited_now())
        compositor()->windowShadow( anim->frame, &anim->shadow );
    }

Render::Renderer* Effects::renderer()
    {
    Compositor* c = compositor();
    if( c != NULL && c->isActive())
        return c->renderer();
    if( own_renderer == NULL )
        own_renderer = Render::Renderer::create( dpy, Render::Renderer::XRender, NULL );
    return own_renderer;
    }

// where an animation draws, as far as it can get (with its shadow)
QRect Effects::extents( EffectAnim* anim ) const
    {
    QRect r = anim->geom;
    if( anim->kind == EffectAnim::Iconify )
        r = r.unite( anim->icon );
    else if( anim->kind == EffectAnim::Morph )
        r = r.unite( anim->old_geom );
    const int margin = 2 * options->compositingShadowSize + QMAX( QABS( options->compositingShadowOffsetX ),
        QABS( options->compositingShadowOffsetY )) + 2;
    return QRect( r.x() - margin, r.y() - margin, r.width() + 2 * margin, r.height() + 2 * margin );
    }

QRegion Effects::prePaint( double t )
    {
    if( !comp_session )
        return QRegion();
    QRegion r = comp_drawn;
    bool moving, waiting;
    advance( t, &moving, &waiting );
    comp_drawn = QRegion();
    for( QPtrListIterator< EffectAnim > it( anims );
         it.current() != NULL;
         ++it )
        comp_drawn |= extents( it.current());
    r |= comp_drawn;
    ++frame_count;
    last_frame = t;
    if( anims.isEmpty())
        { // (the plain scene there in this frame)
        endSession();
        return r;
        }
    if( moving )
        compositor()->requestFrame();
    else if( waiting && !poll_timer.isActive())
        poll_timer.start( 10, true );
    return r;
    }

bool Effects::replacesWindow( Window frame ) const
    {
    if( !comp_session )
        return false;
    for( QPtrListIterator< EffectAnim > it( anims );
         it.current() != NULL;
         ++it )
        if( it.current()->frame == frame )
            return true;
    return false;
    }

void Effects::paintWindow( Render::Renderer* r, Render::Texture* t, Window frame, const QRegion& clip )
    {
    if( !comp_session )
        return;
    beginDraw( r, t, 0, 0, clip );
    for( EffectAnim* anim = anims.first();
         anim != NULL;
         anim = anims.next())
        if( anim->frame == frame && !anim->on_top && !anim->use_below )
            drawAnim( anim );
    r->clearClip();
    }

void Effects::paintAbove( Render::Renderer* r, Render::Texture* t, Window below, const QRegion& clip )
    {
    if( !comp_session )
        return;
    beginDraw( r, t, 0, 0, clip );
    for( EffectAnim* anim = anims.first();
         anim != NULL;
         anim = anims.next())
        if( anim->use_below && anim->below == below )
            drawAnim( anim );
    r->clearClip();
    }

void Effects::paintOnTop( Render::Renderer* r, Render::Texture* t, const QRegion& clip )
    {
    if( !comp_session )
        return;
    Compositor* c = compositor();
    beginDraw( r, t, 0, 0, clip );
    for( EffectAnim* anim = anims.first();
         anim != NULL;
         anim = anims.next())
        {
        if( anim->use_below )
            {
            QRect g;
            if( anim->below != None && !c->windowGeometry( anim->below, &g ))
                drawAnim( anim ); // (its neighbour went)
            }
        else if( anim->on_top )
            drawAnim( anim );
        }
    r->clearClip();
    }

void Effects::compositorStopping()
    {
    abort();
    freeBlendTarget();
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
        RectF r = anims.isEmpty() ? RectF() : anims.getFirst()->drawn;
        fprintf( log, "%s %.1f %d %d %.3f %.3f %.3f %.3f\n", QFile::encodeName( name ).data(), now() - stat_start,
            area.x(), area.y(), r.x, r.y, r.w, r.h );
        fclose( log );
        }
    }

bool Effects::x11Event( XEvent* e )
    {
    if( out == None && !comp_session )
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
                // (the captured contents of a morph are the window's own pixmap
                // until it is resized: their copies change with it)
                if( anim->kind == EffectAnim::Morph && anim->geom.size() == anim->old_geom.size())
                    anim->snap.dropCopies();
                anim->live.dropCopies(); // (the window's contents now)
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
        for( EffectScene::Win* win = scene != NULL ? scene->wins.first() : NULL;
             win != NULL;
             win = scene->wins.next())
            if( win->damage == de->damage )
                {
                XDamageSubtract( dpy, de->damage, None, None );
                win->pic.dropCopies(); // (downscaled copies of what it was)
                requestFrame();
                return true;
                }
        return false;
        }
    if( scene == NULL )
        return false; // (composited: the compositor has the scene)
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
                scene->attrs.remove( e->xmap.window );
                if( e->type == MapNotify || e->type == UnmapNotify )
                    { // a new pixmap when it is mapped again
                    EffectScene::Win* win = scene->find( e->xmap.window );
                    if( win != NULL )
                        win->pic.release( dpy );
                    }
                scene->dirty = true;
                requestFrame();
                }
            break;
        case ConfigureNotify:
            if( e->xconfigure.event == root && e->xconfigure.window != out )
                {
                scene->attrs.remove( e->xconfigure.window );
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
void Effects::screenMoveBegin( Client*, const QRect& ) {}
void Effects::screenMoveEnd( Client* ) {}
void Effects::windowOpening( Client* ) {}
void Effects::windowClosing( Client* ) {}
void Effects::clientRemoved( Client* ) {}
bool Effects::retainedContents( Client*, Pixmap*, QSize*, int*, Visual** ) const { return false; }
void Effects::frameTimeout() {}
void Effects::pollTimeout() {}
void Effects::presentCompleted( bool, int ) {}
void Effects::presentIdle() {}
EffectsMaximize::EffectsMaximize( Client*, bool ) : client( NULL ) {}
EffectsMaximize::~EffectsMaximize() {}

#endif

} // namespace

#include "effects.moc"
