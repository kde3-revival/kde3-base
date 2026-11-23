/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2026 The KDE3 revival project

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

#include <config.h>

#include "compositing.h"

#include <kdebug.h>

#ifdef HAVE_XRENDER
#if defined( HAVE_XCOMPOSITE ) && defined( HAVE_XDAMAGE ) && defined( HAVE_XFIXES )
#define COMPOSITING_COMPOSITE
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#endif
#if defined( COMPOSITING_COMPOSITE ) && defined( KWIN_PANORAMIX_CHECK )
#define COMPOSITING_PANORAMIX_CHECK
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrandr.h>
#endif
#if defined( COMPOSITING_COMPOSITE ) && defined( HAVE_XPRESENT )
#define COMPOSITING_PRESENT
#include <X11/extensions/Xpresent.h>
#endif
#endif
#include <fixx11h.h>

#include <math.h>
#include <stdlib.h>
#include <time.h>

namespace KWinInternal
{

namespace Compositing
{

bool have_render = false;
bool have_composite = false;
bool composite_panoramix = false;
int damage_event = 0;
int damage_error = -1;
bool have_present = false;
int present_opcode = 0;

static bool ext_checked = false;

bool checkExtensions( Display* dpy )
    {
    if( ext_checked )
        return have_render;
    ext_checked = true;
#ifdef HAVE_XRENDER
    int ev, err;
    int major = 0, minor = 0;
    // solid fills need Render 0.10, transformations and filters 0.6
    if( XRenderQueryExtension( dpy, &ev, &err ) && XRenderQueryVersion( dpy, &major, &minor )
        && ( major > 0 || minor >= 10 ))
        have_render = true;
#ifdef COMPOSITING_COMPOSITE
    major = minor = 0;
    // the overlay window needs Composite 0.3
    if( have_render && XCompositeQueryExtension( dpy, &ev, &err )
        && XCompositeQueryVersion( dpy, &major, &minor ) && ( major > 0 || minor >= 3 ))
        {
        int fixes_event, fixes_error;
        int fixes_major = 0, fixes_minor = 0;
        if( XDamageQueryExtension( dpy, &damage_event, &damage_error )
            && XFixesQueryExtension( dpy, &fixes_event, &fixes_error )
            && XFixesQueryVersion( dpy, &fixes_major, &fixes_minor ) && fixes_major >= 2 )
            have_composite = true;
        }
#endif
#ifdef COMPOSITING_PANORAMIX_CHECK
    // Several X screens joined by Xinerama (PanoramiX, not RandR outputs):
    // Composite redirection breaks there (the other screens' areas stay
    // stale).  RandR then reports no CRTCs covering the whole root.
    Window root = DefaultRootWindow( dpy );
    if( have_composite && XineramaIsActive( dpy ))
        {
        int screens = 0;
        XineramaScreenInfo* info = XineramaQueryScreens( dpy, &screens );
        if( info != NULL )
            XFree( info );
        int rr_event, rr_error;
        int rr_major = 0, rr_minor = 0;
        if( screens > 1 && XRRQueryExtension( dpy, &rr_event, &rr_error )
            && XRRQueryVersion( dpy, &rr_major, &rr_minor ) && ( rr_major > 1 || rr_minor >= 2 ))
            {
            XRRScreenResources* res = XRRGetScreenResources( dpy, root );
            QRect covered;
            for( int i = 0;
                 res != NULL && i < res->ncrtc;
                 ++i )
                {
                XRRCrtcInfo* crtc = XRRGetCrtcInfo( dpy, res, res->crtcs[ i ] );
                if( crtc != NULL && crtc->mode != None )
                    covered |= QRect( crtc->x, crtc->y, crtc->width, crtc->height );
                if( crtc != NULL )
                    XRRFreeCrtcInfo( crtc );
                }
            if( res != NULL )
                XRRFreeScreenResources( res );
            if( covered != QRect( 0, 0, DisplayWidth( dpy, DefaultScreen( dpy )),
                DisplayHeight( dpy, DefaultScreen( dpy ))))
                {
                have_composite = false;
                composite_panoramix = true;
                kdWarning( 1212 ) << "Window overview without window contents: Composite doesn't work"
                    " with several X screens joined by Xinerama" << endl;
                }
            }
        }
#endif
#ifdef COMPOSITING_PRESENT
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
#else
    Q_UNUSED( dpy );
#endif
    return have_render;
    }

double now()
    {
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
    }

QRect RectF::bounds() const
    {
    const int x0 = (int) floor( x );
    const int y0 = (int) floor( y );
    return QRect( x0, y0, (int) ceil( x + w ) - x0, (int) ceil( y + h ) - y0 );
    }

static int trapped_errors = 0;
static int (*old_error_handler)( Display*, XErrorEvent* ) = NULL;

static int trapHandler( Display*, XErrorEvent* )
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

int trappedErrors()
    {
    return trapped_errors;
    }

bool dispatchPresentEvent( XEvent* e )
    {
#ifdef HAVE_XRENDER
    return Presenter::dispatch( e );
#else
    Q_UNUSED( e );
    return false;
#endif
    }

#ifdef HAVE_XRENDER

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

void setTransform( Display* dpy, Picture pic, double sx, double sy, double tx, double ty )
    {
    XTransform xf = {{
        { XDoubleToFixed( sx ), XDoubleToFixed( 0 ), XDoubleToFixed( tx ) },
        { XDoubleToFixed( 0 ), XDoubleToFixed( sy ), XDoubleToFixed( ty ) },
        { XDoubleToFixed( 0 ), XDoubleToFixed( 0 ), XDoubleToFixed( 1 ) }
        }};
    XRenderSetPictureTransform( dpy, pic, &xf );
    }

//****************************************
// Presenter
//****************************************

QPtrList< Presenter >* Presenter::presenters = NULL;

Presenter::Presenter( Display* d )
    : dpy( d ), window( None ), off_x( 0 ), off_y( 0 ), width( 0 ), height( 0 ), buf_w( 0 ), buf_h( 0 ),
      format( NULL ), depth( 0 ), gc( NULL ), current( 0 ), present( false ), external( false ),
      last_mode( ModeUnknown ), buffer_count( 0 ),
      present_eid( None ),
      serial( 0 ), queue( 0 ), next_msc( 0 ), last_msc( 0 ), last_ust( 0 ), period( 1000.0 / 60 ),
      last_complete( 0 ), stat_msc( 0 )
    {
    for( int i = 0;
         i < Buffers;
         ++i )
        {
        buffers[ i ] = None;
        pictures[ i ] = None;
        busy[ i ] = false;
        }
    if( presenters == NULL )
        presenters = new QPtrList< Presenter >;
    presenters->append( this );
    connect( &watchdog_timer, SIGNAL( timeout()), SLOT( watchdog()));
    }

Presenter::~Presenter()
    {
    stop();
    presenters->removeRef( this );
    if( presenters->isEmpty())
        {
        delete presenters;
        presenters = NULL;
        }
    }

void Presenter::start( Window win, int x, int y, int w, int h, bool use_present )
    {
    stop();
    window = win;
    off_x = x;
    off_y = y;
    width = w;
    height = h;
    format = XRenderFindVisualFormat( dpy, DefaultVisual( dpy, DefaultScreen( dpy )));
    depth = DefaultDepth( dpy, DefaultScreen( dpy ));
#ifdef COMPOSITING_PRESENT
    present = use_present && have_present;
#else
    Q_UNUSED( use_present );
    present = false;
#endif
    queue = 0;
    next_msc = 0;
    last_msc = 0;
    last_ust = 0; // (maybe another monitor now)
    period = 1000.0 / 60;
    last_complete = 0;
    stat_msc = 0;
    createBuffers();
    gc = XCreateGC( dpy, window, 0, NULL );
#ifdef COMPOSITING_PRESENT
    if( present )
        present_eid = XPresentSelectInput( dpy, window, PresentCompleteNotifyMask | PresentIdleNotifyMask );
#endif
    }

void Presenter::startExternal( Window win, const Pixmap* pixmaps, int count, bool use_present )
    {
    stop();
    window = win;
    off_x = off_y = 0;
    XWindowAttributes attr;
    XGetWindowAttributes( dpy, win, &attr );
    width = buf_w = attr.width;
    height = buf_h = attr.height;
#ifdef COMPOSITING_PRESENT
    present = use_present && have_present;
#else
    Q_UNUSED( use_present );
    present = false;
#endif
    external = true;
    buffer_count = QMIN( count, (int) Buffers );
    for( int i = 0;
         i < Buffers;
         ++i )
        {
        buffers[ i ] = i < buffer_count ? pixmaps[ i ] : None;
        pictures[ i ] = None;
        busy[ i ] = false;
        }
    current = 0;
    queue = 0;
    next_msc = 0;
    last_msc = 0;
    last_ust = 0;
    period = 1000.0 / 60;
    last_complete = 0;
    stat_msc = 0;
    gc = XCreateGC( dpy, window, 0, NULL );
#ifdef COMPOSITING_PRESENT
    if( present )
        present_eid = XPresentSelectInput( dpy, window, PresentCompleteNotifyMask | PresentIdleNotifyMask );
#endif
    }

int Presenter::freeBuffer() const
    {
    for( int i = 0;
         i < buffer_count;
         ++i )
        if( !busy[ i ] )
            return i;
    return -1;
    }

void Presenter::presentBuffer( int buf, XID update, int x, int y )
    {
    current = buf;
#ifdef COMPOSITING_PRESENT
    if( present )
        {
        busy[ buf ] = true;
        if( !mscKnown( now()))
            next_msc = 0; // the next refresh
        ++queue;
        XPresentPixmap( dpy, window, buffers[ buf ], ++serial, None, update, -x, -y,
            None, None, None, PresentOptionNone, next_msc, 0, 0, NULL, 0 );
        if( next_msc != 0 )
            ++next_msc;
        watchdog_timer.start( 200, true );
        XFlush( dpy );
        return;
        }
#endif
#ifdef COMPOSITING_COMPOSITE
    if( update != None )
        XFixesSetGCClipRegion( dpy, gc, -x, -y, update );
#else
    update = None; // (only the compositor gives regions: not built)
#endif
    XCopyArea( dpy, buffers[ buf ], window, gc, x, y, width, height, 0, 0 );
    if( update != None )
        XSetClipMask( dpy, gc, None );
    XFlush( dpy );
    }

void Presenter::createBuffers()
    {
    buf_w = QMAX( 1, width );
    buf_h = QMAX( 1, height );
    Window root = DefaultRootWindow( dpy );
    const int count = present ? (int) Buffers : 1;
    buffer_count = count;
    for( int i = 0;
         i < count;
         ++i )
        {
        buffers[ i ] = XCreatePixmap( dpy, root, buf_w, buf_h, depth );
        pictures[ i ] = XRenderCreatePicture( dpy, buffers[ i ], format, 0, NULL );
        busy[ i ] = false;
        }
    current = 0;
    }

void Presenter::freeBuffers()
    {
    if( external )
        {
        for( int i = 0;
             i < Buffers;
             ++i )
            {
            buffers[ i ] = None;
            busy[ i ] = false;
            }
        external = false;
        buffer_count = 0;
        return;
        }
    for( int i = 0;
         i < Buffers;
         ++i )
        {
        if( pictures[ i ] != None )
            XRenderFreePicture( dpy, pictures[ i ] );
        if( buffers[ i ] != None )
            XFreePixmap( dpy, buffers[ i ] );
        pictures[ i ] = None;
        buffers[ i ] = None;
        busy[ i ] = false;
        }
    }

void Presenter::stop()
    {
    watchdog_timer.stop();
    if( window == None )
        return;
#ifdef COMPOSITING_PRESENT
    if( present_eid != None )
        XPresentFreeInput( dpy, window, present_eid );
#endif
    present_eid = None;
    freeBuffers();
    if( gc != NULL )
        XFreeGC( dpy, gc );
    gc = NULL;
    window = None;
    queue = 0;
    }

void Presenter::setGeometry( int x, int y, int w, int h )
    {
    off_x = x;
    off_y = y;
    width = w;
    height = h;
    if( window != None && ( w > buf_w || h > buf_h ))
        { // the busy ones stay alive in the server as long as it needs them
        freeBuffers();
        createBuffers();
        }
    }

Picture Presenter::beginFrame()
    {
    for( int i = 0;
         i < ( present ? (int) Buffers : 1 );
         ++i )
        if( !busy[ i ] )
            {
            current = i;
            return pictures[ i ];
            }
    return None;
    }

double Presenter::frameTime( double t ) const
    {
    return t + ( present ? queue * period : 0 );
    }

bool Presenter::canQueue() const
    {
    if( !present )
        return true;
    return queue < ( mscKnown( now()) ? 2 : 1 );
    }

void Presenter::endFrame( bool immediate )
    {
    const int buf = current;
#ifdef COMPOSITING_PRESENT
    if( present && !immediate )
        {
        busy[ buf ] = true;
        if( !mscKnown( now()))
            next_msc = 0; // as soon as possible
        ++queue;
        XPresentPixmap( dpy, window, buffers[ buf ], ++serial, None, None, off_x, off_y,
            None, None, None, PresentOptionNone, next_msc, 0, 0, NULL, 0 );
        if( next_msc != 0 )
            ++next_msc;
        watchdog_timer.start( 200, true );
        XFlush( dpy );
        return;
        }
#else
    Q_UNUSED( immediate );
#endif
    XCopyArea( dpy, buffers[ buf ], window, gc, 0, 0, width, height, off_x, off_y );
    XFlush( dpy );
    }

// no completion events from Present: don't hang
void Presenter::watchdog()
    {
    if( window == None )
        return;
    queue = 0;
    next_msc = 0;
    if( external )
        { // (the server may still show or read them: not drawn into again)
        emit stalled();
        return;
        }
    for( int i = 0;
         i < Buffers;
         ++i )
        busy[ i ] = false;
    emit completed( false, 0 );
    }

bool Presenter::dispatch( XEvent* e )
    {
#ifdef COMPOSITING_PRESENT
    if( !have_present || e->type != GenericEvent || e->xgeneric.extension != present_opcode )
        return false;
    XGenericEventCookie* cookie = &e->xcookie;
    if( !XGetEventData( e->xany.display, cookie ))
        return true;
    // only the events of a live selection (an idle event may name a pixmap
    // XID that was freed and reused meanwhile); a presenter may be stopped
    // or deleted by the signals
    XID eid = None;
    if( cookie->evtype == PresentCompleteNotify )
        eid = ((XPresentCompleteNotifyEvent*) cookie->data )->eid;
    else if( cookie->evtype == PresentIdleNotify )
        eid = ((XPresentIdleNotifyEvent*) cookie->data )->eid;
    Presenter* target = NULL;
    if( presenters != NULL && eid != None )
        for( QPtrListIterator< Presenter > it( *presenters );
             it.current() != NULL;
             ++it )
            if( it.current()->present_eid == eid )
                target = it.current();
    if( target != NULL )
        target->handle( e );
    XFreeEventData( e->xany.display, cookie );
    return true;
#else
    Q_UNUSED( e );
    return false;
#endif
    }

void Presenter::handle( XEvent* e )
    {
#ifdef COMPOSITING_PRESENT
    XGenericEventCookie* cookie = &e->xcookie;
    if( cookie->evtype == PresentCompleteNotify )
        {
        XPresentCompleteNotifyEvent* ce = (XPresentCompleteNotifyEvent*) cookie->data;
        if( ce->kind != PresentCompleteKindPixmap )
            return;
        queue = QMAX( 0, queue - 1 );
        bool skipped = ce->mode == PresentCompleteModeSkip;
        last_mode = ce->mode == PresentCompleteModeFlip ? ModeFlip : skipped ? ModeSkip : ModeCopy;
        int missed = 0;
        if( !skipped )
            {
            if( stat_msc != 0 && ce->msc > stat_msc + 1 )
                missed = ce->msc - stat_msc - 1;
            stat_msc = ce->msc;
            }
        last_complete = now();
        if( last_ust != 0 && ce->msc > last_msc && ce->ust > last_ust )
            {
            double p = ( ce->ust - last_ust ) / 1000.0 / ( ce->msc - last_msc );
            if( p > 4 && p < 50 )
                period = p;
            }
        last_msc = ce->msc;
        last_ust = ce->ust;
        if( next_msc <= ce->msc )
            next_msc = ce->msc + 1;
        if( queue == 0 )
            watchdog_timer.stop();
        else
            watchdog_timer.start( 200, true );
        emit completed( skipped, missed );
        }
    else if( cookie->evtype == PresentIdleNotify )
        {
        XPresentIdleNotifyEvent* ie = (XPresentIdleNotifyEvent*) cookie->data;
        for( int i = 0;
             i < Buffers;
             ++i )
            if( buffers[ i ] == ie->pixmap )
                busy[ i ] = false;
        emit idle();
        }
#else
    Q_UNUSED( e );
#endif
    }

#endif // HAVE_XRENDER

} // namespace Compositing

} // namespace KWinInternal

#include "compositing.moc"
