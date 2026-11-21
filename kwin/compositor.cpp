/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2026 The KDE3 revival project

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

/*
 The compositing manager (see compositor.h).

 Windows: the scene is every child of the root window, in the X stacking
 order, kept from the root's structure events (Create, Destroy, Map, Unmap,
 Configure with the sibling above, Reparent, Circulate), so that
 override-redirect windows (menus, tooltips, the screen locker) are in it
 where they really are.  All of them are redirected (Composite, Manual,
 RedirectSubwindows on the root); the frames of managed windows are also
 kept redirected automatically by the overview (KeepRedirected), so that
 switching compositing off and on doesn't make them repaint.

 Damage: DeltaRectangles events give the damaged rectangles without round
 trips; each damaged window's damage is subtracted once per frame.

 Painting a frame into a present buffer (render.h does the drawing): the
 region to paint is the damage of the frames the buffer missed (buffer
 age, three buffers).  Opaque windows are drawn top-down, each clipped to
 what the opaque windows above it leave (blending off), the background
 into the rest; then bottom-up the shadows (outside the window's shape) and
 the translucent windows (ARGB visuals, _NET_WM_WINDOW_OPACITY).

 Frames go to the Composite overlay window with Present at the next
 refresh, at most one queued: damage while nothing is queued is drawn at
 once, damage while a frame is queued when that frame completed (right
 after the refresh).  A full-screen present buffer on the full-screen
 overlay can be page-flipped (all CRTCs, each at its own vblank).

 Unredirection: a full screen window alone on top of its monitor is drawn
 directly (no copy, its own page flips or copies), after a delay, and the
 overlay is cut out of its area; the screen locker (kdesktop_lock) at once
 and for as long as it is mapped.
*/

#include <config.h>

#include "compositor.h"

#include <qapplication.h>
#include <qdesktopwidget.h>
#include <qtl.h>
#include <kdebug.h>
#include <klocale.h>
#include <kmanagerselection.h>

#include "workspace.h"
#include "client.h"
#include "options.h"
#include "utils.h"
#include "compositing.h"
#include "render.h"
#include "shadow.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#if defined( HAVE_XRENDER ) && defined( HAVE_XCOMPOSITE ) && defined( HAVE_XDAMAGE ) && defined( HAVE_XFIXES )
#define COMPOSITOR_BUILD
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#endif
#include <fixx11h.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace KWinInternal
{

using namespace Compositing;

// shadow classes
enum ShadowClass { NoShadow, ShadowActive, ShadowInactive, ShadowMenu, ShadowTooltip };

// unmanaged window types (from _NET_WM_WINDOW_TYPE)
enum UnmanagedType { TypeNone, TypeMenu, TypeTooltip, TypeNotification, TypeDnd, TypeDesktop, TypeDock,
    TypeSplash, TypeUtility, TypeOther };

// a full screen window is drawn directly after this time on top (ms),
// longer when it was drawn directly shortly before (flicker)
static const double UnredirectDelay = 250;
static const double UnredirectAgainDelay = 1000;

/*
 A child of the root window.
*/
class SceneWindow
    {
    public:
        SceneWindow( Window w );
        Window id;
        QRect geom; // outer: with the border
        int border;
        bool mapped;
        bool queried; // the attributes below are known
        bool input_only;
        bool override_redirect;
        Visual* visual;
        int depth;
        bool argb;
        XID damage;
        bool damaged; // damage events since the last subtraction
        Pixmap pixmap;
        Render::Texture* texture;
        bool bind_failed;
        QRegion shape; // window relative (outer); null: the rectangle
        double opacity;
        int type; // UnmanagedType
        bool locker;
        bool unredirected;
        double candidate_since;
        double redirected_at;
        int shadow;
        // the shadow's blur radius, offset and opacity
        int shadow_radius, shadow_dx, shadow_dy;
        double shadow_alpha;
        // set while painting: what the opaque windows above leave
        QRegion clip;
        bool isOpaque() const { return !argb && opacity >= 1.0; }
        QRegion shapeOnScreen() const
            {
            if( shape.isNull())
                return QRegion( geom );
            QRegion r = shape;
            r.translate( geom.x(), geom.y());
            return r;
            }
        QRect shadowRect() const
            {
            return shadow != NoShadow ? ShadowCache::outerRect( geom, shadow_radius, shadow_dx, shadow_dy ) : QRect();
            }
        bool isPaintable( const QRect& screen ) const
            {
            return mapped && queried && !input_only && !unredirected && geom.intersects( screen )
                && geom.width() > 0 && geom.height() > 0;
            }
    };

SceneWindow::SceneWindow( Window w )
    : id( w ), border( 0 ), mapped( false ), queried( false ), input_only( false ), override_redirect( false ),
      visual( NULL ), depth( 0 ), argb( false ), damage( None ), damaged( false ), pixmap( None ), texture( NULL ),
      bind_failed( false ), opacity( 1.0 ), type( TypeNone ), locker( false ), unredirected( false ),
      candidate_since( 0 ), redirected_at( 0 ), shadow( NoShadow ), shadow_radius( 0 ), shadow_dx( 0 ),
      shadow_dy( 0 ), shadow_alpha( 0 )
    {
    }

/*
 Where frames are presented: the overlay window (one output), or a child
 window of the overlay per monitor, presented at that monitor's refresh.
*/
class CompositorOutput
    {
    public:
        CompositorOutput() : window( None ), own_window( false ), presenter( NULL ), deferred( false ), flips( 0 ),
            copies( 0 ), skips( 0 ) {}
        void count()
            {
            switch( presenter->lastMode())
                {
                case Presenter::ModeFlip: ++flips; break;
                case Presenter::ModeCopy: ++copies; break;
                case Presenter::ModeSkip: ++skips; break;
                default: break;
                }
            }
        QRect geom; // screen coordinates
        Window window;
        bool own_window; // a child of the overlay
        Presenter* presenter;
        QRegion pending; // changed since the last presentation here (screen coordinates)
        bool deferred; // a frame waits for the presentation queued here
        int flips, copies, skips; // (statistics)
    };

static int trapped = 0;
static int ( *saved_handler )( Display*, XErrorEvent* ) = NULL;

static int countErrors( Display*, XErrorEvent* )
    {
    ++trapped;
    return 0;
    }

// error trapping around a synchronous request (no XSync of its own)
static void trap()
    {
    trapped = 0;
    saved_handler = XSetErrorHandler( countErrors );
    }

static int untrap()
    {
    XSetErrorHandler( saved_handler );
    return trapped;
    }

Compositor::Compositor( Workspace* w )
    : ws( w ), dpy( qt_xdisplay()), root( qt_xrootwin()), screen_w( 0 ), screen_h( 0 ), active( false ),
      suspended( false ), failed_once( false ), selection( NULL ), renderer( NULL ), shadows( NULL ),
      presenter( NULL ), per_monitor( false ), flips_failed( false ), copies_in_row( 0 ), latest_buffer( -1 ),
      hold_until( 0 ), force_mode( 0 ), overlay( None ), frame_number( 0 ), frame_wanted( false ), root_pixmap( None ),
      root_texture( NULL ), shape_event( 0 ), asap( false ), render_budget( 3.0 ), min_budget( 3.0 ), on_time( 0 ),
      target_vblank( 0 ), late_at_max( 0 ), late_failed( false ), submit_time( 0 ), shown_index( 0 ),
      immediate_server( false ), stat_frames( 0 ), stat_render( 0 ), stat_max_render( 0 ), stat_skipped( 0 ),
      stat_missed( 0 ), stat_late( 0 ), stat_start( 0 )
    {
    stacking.setAutoDelete( false );
    for( int i = 0;
         i < BufferCount;
         ++i )
        {
        buffers[ i ] = NULL;
        buffer_pixmaps[ i ] = None;
        buffer_frame[ i ] = -1;
        }
    frame_damage.resize( BufferCount + 1 );
    debug = getenv( "KWIN_COMPOSITOR_DEBUG" ) != NULL;
    // KWIN_COMPOSITOR_ASAP=1: frames drawn at once (for comparing);
    // KWIN_COMPOSITOR_BUDGET=ms: the time kept for drawing before a refresh
    asap = getenv( "KWIN_COMPOSITOR_ASAP" ) != NULL;
    if( getenv( "KWIN_COMPOSITOR_BUDGET" ) != NULL )
        min_budget = render_budget = QMAX( 0.5, QMIN( 10.0, atof( getenv( "KWIN_COMPOSITOR_BUDGET" ))));
    connect( &repaint_timer, SIGNAL( timeout()), SLOT( performRepaint()));
    connect( &unredirect_timer, SIGNAL( timeout()), SLOT( checkUnredirect()));
    connect( &retry_timer, SIGNAL( timeout()), SLOT( retry()));
    const char* names[] =
        {
        "_NET_WM_WINDOW_OPACITY", "_NET_WM_WINDOW_TYPE", "_XROOTPMAP_ID", "ESETROOT_PMAP_ID",
        "_NET_WM_WINDOW_TYPE_POPUP_MENU", "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", "_NET_WM_WINDOW_TYPE_MENU",
        "_NET_WM_WINDOW_TYPE_COMBO", "_NET_WM_WINDOW_TYPE_TOOLTIP", "_NET_WM_WINDOW_TYPE_NOTIFICATION",
        "_NET_WM_WINDOW_TYPE_DND", "_NET_WM_WINDOW_TYPE_DESKTOP", "_NET_WM_WINDOW_TYPE_DOCK",
        "_NET_WM_WINDOW_TYPE_SPLASH", "_NET_WM_WINDOW_TYPE_UTILITY", "_KDE_NET_WM_WINDOW_TYPE_OVERRIDE"
        };
    Atom atoms[ 16 ];
    XInternAtoms( dpy, const_cast< char** >( names ), 16, False, atoms );
    atom_opacity = atoms[ 0 ];
    atom_type = atoms[ 1 ];
    atom_rootpmap = atoms[ 2 ];
    atom_esetroot = atoms[ 3 ];
    for( int i = 0;
         i < 12;
         ++i )
        atom_types[ i ] = atoms[ 4 + i ];
    }

Compositor::~Compositor()
    {
    stop( false );
    }

bool Compositor::foreignCompositor( Display* dpy )
    {
    char name[ 32 ];
    snprintf( name, sizeof( name ), "_NET_WM_CM_S%d", DefaultScreen( dpy ));
    Atom atom = XInternAtom( dpy, name, False );
    Window owner = XGetSelectionOwner( dpy, atom );
    if( owner == None )
        return false;
    Workspace* ws = Workspace::self();
    Compositor* c = ws != NULL ? ws->compositor() : NULL;
    return c == NULL || c->selection == NULL || c->selection->ownerWindow() != owner;
    }

bool Compositor::gpuAvailable( Display* dpy )
    {
    int opcode, ev, err;
    return XQueryExtension( dpy, "DRI3", &opcode, &ev, &err ) && XQueryExtension( dpy, "Present", &opcode, &ev, &err );
    }

QString Compositor::status() const
    {
    if( active )
        return i18n( "Compositing is active (%1)." ).arg( renderer != NULL ? renderer->description() : QString());
    if( suspended )
        return i18n( "Compositing is suspended (Ctrl+Alt+Shift+F12 resumes it)." );
    if( !reason.isEmpty())
        return i18n( "Compositing is off: %1" ).arg( reason );
    return i18n( "Compositing is off." );
    }

void Compositor::reconfigure()
    {
    const bool want = options->compositingEnabled && !suspended;
    if( active && ( !want || ( renderer->backend() != (Render::Renderer::Backend) options->compositingBackend
            && options->compositingBackend != Render::Renderer::Auto )))
        stop( false );
    if( !active && want )
        {
        failed_once = false;
        start();
        return;
        }
    if( !want && !suspended )
        reason = i18n( "disabled in the settings" );
    if( active )
        { // shadows, unredirection
        shadows->clear();
        for( QPtrListIterator< SceneWindow > it( stacking );
             it.current() != NULL;
             ++it )
            updateShadow( it.current());
        if( !options->compositingUnredirectFullscreen )
            for( QPtrListIterator< SceneWindow > it( stacking );
                 it.current() != NULL;
                 ++it )
                if( it.current()->unredirected && !it.current()->locker )
                    setUnredirected( it.current(), false );
        addRepaintFull();
        unredirect_timer.start( 0, true );
        }
    }

void Compositor::toggle()
    {
    if( active )
        {
        suspended = true;
        stop( false );
        }
    else
        {
        suspended = false;
        if( !options->compositingEnabled )
            {
            reason = i18n( "disabled in the settings" );
            return;
            }
        failed_once = false;
        start();
        }
    }

void Compositor::screenChanged()
    {
    if( !active )
        return;
    // everything anew: buffers of the screen's size, an output per monitor
    QDesktopWidget* desktop = QApplication::desktop();
    bool same = DisplayWidth( dpy, DefaultScreen( dpy )) == screen_w
        && DisplayHeight( dpy, DefaultScreen( dpy )) == screen_h;
    if( same && per_monitor )
        { // (the same monitors, in any order)
        same = (int) outputs.count() == desktop->numScreens();
        for( int i = 0;
             i < desktop->numScreens() && same;
             ++i )
            {
            bool found = false;
            for( QPtrListIterator< CompositorOutput > it( outputs );
                 it.current() != NULL && !found;
                 ++it )
                found = it.current()->geom == ( desktop->screenGeometry( i ) & QRect( 0, 0, screen_w, screen_h ));
            same = found;
            }
        }
    if( same )
        return;
    if( debug )
        kdWarning( 1212 ) << "compositor: the screen changed, starting anew" << endl;
    stop( false );
    start();
    }

void Compositor::retry()
    {
    if( !active && !suspended && options->compositingEnabled )
        start();
    }

bool Compositor::claimSelection()
    {
    char name[ 32 ];
    snprintf( name, sizeof( name ), "_NET_WM_CM_S%d", DefaultScreen( dpy ));
    selection = new KSelectionOwner( name );
    if( !selection->claim( false, false ))
        {
        delete selection;
        selection = NULL;
        return false;
        }
    connect( selection, SIGNAL( lostOwnership()), SLOT( selectionLost()));
    return true;
    }

void Compositor::selectionLost()
    {
    // another compositing manager replaced us
    kdWarning( 1212 ) << "compositor: another compositing manager took over" << endl;
    if( selection != NULL )
        {
        selection->disconnect( this );
        selection->deleteLater();
        selection = NULL;
        }
    stop( false, i18n( "another compositing manager runs" ));
    }

bool Compositor::start()
    {
#ifdef COMPOSITOR_BUILD
    if( active )
        return true;
    reason = QString::null;
    checkExtensions( dpy );
    if( !have_render || !have_composite )
        {
        reason = composite_panoramix ? i18n( "several X screens joined by Xinerama" )
            : i18n( "the X server lacks Composite 0.3, Damage, XFixes 2 or RENDER" );
        kdWarning( 1212 ) << "compositor: " << reason << endl;
        return false;
        }
    if( !Shape::available())
        {
        reason = i18n( "the X server lacks the Shape extension" );
        return false;
        }
    if( foreignCompositor( dpy ) || !claimSelection())
        {
        reason = i18n( "another compositing manager runs" );
        kdWarning( 1212 ) << "compositor: " << reason << endl;
        return false;
        }
    shape_event = Shape::shapeEvent();
    screen_w = DisplayWidth( dpy, DefaultScreen( dpy ));
    screen_h = DisplayHeight( dpy, DefaultScreen( dpy ));
    QString error;
    renderer = Render::Renderer::create( dpy, (Render::Renderer::Backend) options->compositingBackend, &error );
    if( renderer == NULL )
        {
        stop( true, error );
        return false;
        }
    shadows = new ShadowCache( renderer );
    // everything redirected, the windows taken over in one go
    XGrabServer( dpy );
    trapErrors( dpy );
    XCompositeRedirectSubwindows( dpy, root, CompositeRedirectManual );
    if( untrapErrors( dpy ) != 0 )
        {
        XUngrabServer( dpy );
        stop( true, i18n( "the windows are redirected by another program" ));
        return false;
        }
    Window root_return, parent_return;
    Window* children = NULL;
    unsigned int count = 0;
    XQueryTree( dpy, root, &root_return, &parent_return, &children, &count );
    trap();
    for( unsigned int i = 0;
         i < count;
         ++i )
        addWindow( children[ i ], true );
    untrap();
    if( children != NULL )
        XFree( children );
    XUngrabServer( dpy );
    overlay = XCompositeGetOverlayWindow( dpy, root );
    // input goes through it
    XserverRegion empty = XFixesCreateRegion( dpy, NULL, 0 );
    XFixesSetWindowShapeRegion( dpy, overlay, ShapeInput, 0, 0, empty );
    XFixesDestroyRegion( dpy, empty );
    // (not a scene window)
    if( SceneWindow* w = findWindow( overlay ))
        removeWindow( w );
    if( !createBuffers())
        {
        stop( true, i18n( "the frame buffers could not be made" ));
        return false;
        }
    flips_failed = false;
    copies_in_row = 0;
    latest_buffer = -1;
    hold_until = 0;
    const char* mode = getenv( "KWIN_COMPOSITOR_PRESENT" ); // overlay|monitors (tests)
    force_mode = mode == NULL ? 0 : strcmp( mode, "monitors" ) == 0 ? 2 : strcmp( mode, "overlay" ) == 0 ? 1 : 0;
    setupOutputs( force_mode == 2 );
    updateRootPixmap();
    for( QPtrListIterator< SceneWindow > it( stacking );
         it.current() != NULL;
         ++it )
        if( it.current()->mapped )
            windowMapped( it.current());
    active = true;
    frame_number = 0;
    stat_frames = 0;
    stat_render = stat_max_render = 0;
    stat_skipped = stat_missed = stat_late = 0;
    stat_start = now();
    render_budget = min_budget;
    on_time = 0;
    late_at_max = 0;
    late_failed = false;
    target_vblank = 0;
    submit_time = 0;
    shown_index = 0;
    immediate_server = false;
    kdDebug( 1212 ) << "compositor: started, " << renderer->description() << endl;
    if( debug )
        kdWarning( 1212 ) << "compositor: started, " << renderer->description() << ", present "
            << have_present << endl;
    emit activeChanged( true );
    addRepaintFull();
    checkUnredirect();
    performRepaint(); // the first frame now
    return true;
#else
    reason = i18n( "KWin was built without Composite, Damage and XFixes" );
    return false;
#endif
    }

void Compositor::stop( bool failed, const QString& why )
    {
#ifdef COMPOSITOR_BUILD
    const bool was_active = active;
    active = false;
    repaint_timer.stop();
    unredirect_timer.stop();
    frame_wanted = false;
    if( debug && was_active && stat_frames > 0 )
        kdWarning( 1212 ) << "compositor: stopped after " << stat_frames << " frames, render avg "
            << stat_render / stat_frames << " ms max " << stat_max_render << " ms, skipped " << stat_skipped
            << " missed refreshes " << stat_missed << outputStats() << endl;
    trap();
    freeOutputs();
    // the overlay goes first: the screen keeps the last frame (the root's
    // background isn't painted while its children are redirected) until
    // the windows are back on it
    if( overlay != None )
        {
        XCompositeReleaseOverlayWindow( dpy, root );
        overlay = None;
        }
    for( QPtrListIterator< SceneWindow > it( stacking );
         it.current() != NULL;
         ++it )
        {
        SceneWindow* w = it.current();
        releaseContents( w );
        if( w->damage != None )
            XDamageDestroy( dpy, w->damage );
        w->damage = None;
        }
    if( renderer != NULL )
        XCompositeUnredirectSubwindows( dpy, root, CompositeRedirectManual );
    XSync( dpy, False );
    untrap();
    stacking.setAutoDelete( true );
    stacking.clear();
    stacking.setAutoDelete( false );
    windows.clear();
    overlay_hole = QRegion();
    pending = QRegion();
    delete root_texture;
    root_texture = NULL;
    root_pixmap = None;
    freeBuffers();
    delete shadows;
    shadows = NULL;
    delete renderer;
    renderer = NULL;
    delete selection;
    selection = NULL;
    if( !why.isEmpty())
        reason = why;
    else if( suspended )
        reason = i18n( "suspended" );
    if( failed )
        {
        kdWarning( 1212 ) << "compositor: compositing failed: " << reason << endl;
        if( !failed_once )
            { // e.g. a GPU reset: once more after a while
            failed_once = true;
            retry_timer.start( 2000, true );
            }
        }
    if( was_active )
        emit activeChanged( false );
#else
    Q_UNUSED( failed );
    Q_UNUSED( why );
#endif
    }

bool Compositor::createBuffers()
    {
    for( int i = 0;
         i < BufferCount;
         ++i )
        {
        buffers[ i ] = renderer->createPresentBuffer( overlay, screen_w, screen_h, &buffer_pixmaps[ i ] );
        buffer_frame[ i ] = -1;
        if( buffers[ i ] == NULL )
            return false;
        }
    return true;
    }

void Compositor::freeBuffers()
    {
    for( int i = 0;
         i < BufferCount;
         ++i )
        {
        delete buffers[ i ]; // (frees the pixmap)
        buffers[ i ] = NULL;
        buffer_pixmaps[ i ] = None;
        buffer_frame[ i ] = -1;
        }
    }

//****************************************
// the windows
//****************************************

SceneWindow* Compositor::findWindow( Window w ) const
    {
    QMap< Window, SceneWindow* >::ConstIterator it = windows.find( w );
    return it != windows.end() ? *it : NULL;
    }

// a new child of the root, on top; attributes queried at once (start) or
// when it gets mapped
SceneWindow* Compositor::addWindow( Window id, bool query )
    {
#ifdef COMPOSITOR_BUILD
    if( id == overlay && overlay != None )
        return NULL;
    SceneWindow* w = findWindow( id );
    if( w == NULL )
        {
        w = new SceneWindow( id );
        windows[ id ] = w;
        stacking.append( w );
        }
    if( query )
        {
        XWindowAttributes attr;
        if( !XGetWindowAttributes( dpy, id, &attr ))
            {
            removeWindow( w );
            return NULL;
            }
        w->geom = QRect( attr.x, attr.y, attr.width + 2 * attr.border_width, attr.height + 2 * attr.border_width );
        w->border = attr.border_width;
        w->mapped = attr.map_state != IsUnmapped;
        w->input_only = attr.c_class == InputOnly;
        w->override_redirect = attr.override_redirect;
        w->visual = attr.visual;
        w->depth = attr.depth;
        w->argb = false;
        if( !w->input_only )
            {
            XRenderPictFormat* format = XRenderFindVisualFormat( dpy, attr.visual );
            w->argb = format != NULL && format->type == PictTypeDirect && format->direct.alphaMask != 0;
            }
        w->queried = true;
        }
    return w;
#else
    Q_UNUSED( id );
    Q_UNUSED( query );
    return NULL;
#endif
    }

void Compositor::removeWindow( SceneWindow* w )
    {
    if( w->mapped && w->queried && active )
        damageWindow( w );
    releaseContents( w );
    if( w->unredirected )
        {
        overlay_hole = QRegion();
        w->unredirected = false;
        for( QPtrListIterator< SceneWindow > it( stacking );
             it.current() != NULL;
             ++it )
            if( it.current()->unredirected )
                overlay_hole |= it.current()->shapeOnScreen();
        updateOverlayShape();
        }
    windows.remove( w->id );
    stacking.removeRef( w );
    delete w;
    unredirect_timer.start( 0, true );
    }

// w goes right above the window above (None: at the bottom)
void Compositor::restack( SceneWindow* w, Window above )
    {
    stacking.removeRef( w );
    if( above == None )
        stacking.prepend( w );
    else
        {
        int index = 0;
        for( QPtrListIterator< SceneWindow > it( stacking );
             it.current() != NULL;
             ++it, ++index )
            if( it.current()->id == above )
                break;
        stacking.insert( QMIN( index + 1, (int) stacking.count()), w );
        }
    }

void Compositor::windowMapped( SceneWindow* w )
    {
#ifdef COMPOSITOR_BUILD
    if( !w->queried )
        {
        trap();
        addWindow( w->id, true );
        untrap();
        w = findWindow( w->id );
        if( w == NULL || !w->queried )
            return;
        }
    w->mapped = true;
    if( w->input_only )
        return;
    trap();
    if( w->damage == None )
        w->damage = XDamageCreate( dpy, w->id, XDamageReportDeltaRectangles );
    XShapeSelectInput( dpy, w->id, ShapeNotifyMask );
    // (KWin's own event mask on its frames stays)
    XWindowAttributes attr;
    if( XGetWindowAttributes( dpy, w->id, &attr ))
        XSelectInput( dpy, w->id, attr.your_event_mask | PropertyChangeMask );
    untrap();
    fetchShape( w );
    fetchOpacity( w );
    fetchType( w );
    updateShadow( w );
    w->bind_failed = false;
    damageWindow( w );
    if( w->locker )
        checkUnredirect(); // at once, before anything is drawn
    else
        unredirect_timer.start( 0, true );
#else
    Q_UNUSED( w );
#endif
    }

void Compositor::windowUnmapped( SceneWindow* w )
    {
    if( w->queried && !w->input_only )
        damageWindow( w );
    w->mapped = false;
    releaseContents( w );
    if( w->unredirected )
        { // (it is no longer in the way of the overlay)
        w->unredirected = false;
        overlay_hole = QRegion();
        for( QPtrListIterator< SceneWindow > it( stacking );
             it.current() != NULL;
             ++it )
            if( it.current()->unredirected )
                overlay_hole |= it.current()->shapeOnScreen();
        // redirected again by the server's subwindow redirection? No: our
        // own unredirection of it stays; redirect it again for its next map
#ifdef COMPOSITOR_BUILD
        trap();
        XCompositeRedirectWindow( dpy, w->id, CompositeRedirectManual );
        XSync( dpy, False );
        untrap();
#endif
        updateOverlayShape();
        }
    w->candidate_since = 0;
    unredirect_timer.start( 0, true );
    }

void Compositor::fetchShape( SceneWindow* w )
    {
    w->shape = QRegion();
    int count = 0, ordering;
    trap();
    XRectangle* rects = XShapeGetRectangles( dpy, w->id, ShapeBounding, &count, &ordering );
    untrap();
    if( rects == NULL )
        return;
    QRegion r;
    for( int i = 0;
         i < count;
         ++i )
        r |= QRect( rects[ i ].x + w->border, rects[ i ].y + w->border, rects[ i ].width, rects[ i ].height );
    XFree( rects );
    // a plain rectangle is no shape
    if( r != QRegion( 0, 0, w->geom.width(), w->geom.height()))
        w->shape = r & QRegion( 0, 0, w->geom.width(), w->geom.height());
    }

void Compositor::fetchOpacity( SceneWindow* w )
    {
    w->opacity = 1.0;
    Atom type;
    int format;
    unsigned long n, left;
    unsigned char* data = NULL;
    trap();
    int ret = XGetWindowProperty( dpy, w->id, atom_opacity, 0, 1, False, XA_CARDINAL, &type, &format, &n, &left,
        &data );
    untrap();
    // (format 32 comes as longs, sign-extended)
    if( ret == Success && data != NULL && format == 32 && n == 1 )
        w->opacity = ( *(unsigned long*) data & 0xffffffffUL ) / (double) 0xffffffffUL;
    if( data != NULL )
        XFree( data );
    if( debug && w->opacity < 1 )
        kdWarning( 1212 ) << "compositor: window 0x" << QString::number( w->id, 16 ) << " opacity " << w->opacity
            << endl;
    }

void Compositor::fetchType( SceneWindow* w )
    {
    w->type = TypeNone;
    w->locker = false;
    if( !w->override_redirect )
        return; // managed (or never mapped by a WM): KWin's Client tells
    Atom type;
    int format;
    unsigned long n, left;
    unsigned char* data = NULL;
    trap();
    int ret = XGetWindowProperty( dpy, w->id, atom_type, 0, 8, False, XA_ATOM, &type, &format, &n, &left, &data );
    if( ret == Success && data != NULL && format == 32 )
        {
        static const int map[ 12 ] = { TypeMenu, TypeMenu, TypeMenu, TypeMenu, TypeTooltip, TypeNotification,
            TypeDnd, TypeDesktop, TypeDock, TypeSplash, TypeUtility, TypeOther };
        Atom* atoms = (Atom*) data;
        for( unsigned long i = 0;
             i < n && w->type == TypeNone;
             ++i )
            for( int j = 0;
                 j < 12;
                 ++j )
                if( atoms[ i ] == atom_types[ j ] )
                    {
                    w->type = map[ j ];
                    break;
                    }
        }
    if( data != NULL )
        XFree( data );
    XClassHint hint;
    if( XGetClassHint( dpy, w->id, &hint ))
        {
        w->locker = hint.res_name != NULL && strcmp( hint.res_name, "kdesktop_lock" ) == 0;
        if( hint.res_name != NULL )
            XFree( hint.res_name );
        if( hint.res_class != NULL )
            XFree( hint.res_class );
        }
    untrap();
    }

// the shadow class of w and its size
void Compositor::updateShadow( SceneWindow* w )
    {
    const int old = w->shadow;
    const QRect old_rect = w->shadowRect();
    w->shadow = NoShadow;
    if( options->compositingShadows && w->queried && !w->input_only )
        {
        Client* c = ws->findClient( FrameIdMatchPredicate( w->id ));
        if( c != NULL )
            {
            if( !( c->isDesktop() || c->isDock() || c->isSplash() || c->isTopMenu() || c->isFullScreen()
                || c->maximizeMode() == Client::MaximizeFull ))
                w->shadow = c->isActive() ? ShadowActive : ShadowInactive;
            }
        else if( w->override_redirect && !w->locker )
            {
            // (nothing covering a whole monitor: lock screens, splash screens)
            bool covers = false;
            QDesktopWidget* desktop = QApplication::desktop();
            for( int i = 0;
                 i < desktop->numScreens();
                 ++i )
                if( w->geom.contains( desktop->screenGeometry( i )))
                    covers = true;
            if( !covers )
                switch( w->type )
                    {
                    case TypeTooltip:
                    case TypeNotification:
                    case TypeUtility: // Qt 3's tooltips (WStyle_Tool, bypassing the WM)
                        w->shadow = ShadowTooltip;
                        break;
                    case TypeDnd:
                    case TypeDesktop:
                    case TypeDock:
                    case TypeSplash:
                        break;
                    default: // menus, untyped popups
                        w->shadow = ShadowMenu;
                        break;
                    }
            }
        }
    const double size = options->compositingShadowSize;
    const double alpha = options->compositingShadowOpacity / 100.0;
    switch( w->shadow )
        {
        case ShadowActive:
            w->shadow_radius = (int) size;
            w->shadow_dx = options->compositingShadowOffsetX;
            w->shadow_dy = options->compositingShadowOffsetY;
            w->shadow_alpha = alpha;
            break;
        case ShadowInactive:
            w->shadow_radius = (int) floor( size * 0.65 + 0.5 );
            w->shadow_dx = (int) floor( options->compositingShadowOffsetX * 0.65 + 0.5 );
            w->shadow_dy = (int) floor( options->compositingShadowOffsetY * 0.65 + 0.5 );
            w->shadow_alpha = alpha * 0.7;
            break;
        case ShadowMenu:
            w->shadow_radius = (int) floor( size * 0.5 + 0.5 );
            w->shadow_dx = 0;
            w->shadow_dy = (int) floor( options->compositingShadowOffsetY * 0.5 + 0.5 );
            w->shadow_alpha = alpha * 0.8;
            break;
        case ShadowTooltip:
            w->shadow_radius = (int) floor( size * 0.3 + 0.5 );
            w->shadow_dx = 0;
            w->shadow_dy = (int) floor( options->compositingShadowOffsetY * 0.3 + 0.5 );
            w->shadow_alpha = alpha * 0.55;
            break;
        default:
            w->shadow_radius = 0;
            break;
        }
    if( w->shadow != NoShadow && w->shadow_radius < 1 )
        w->shadow = NoShadow;
    if( active && w->mapped && ( old != w->shadow || old_rect != w->shadowRect()))
        {
        addRepaint( QRegion( old_rect ));
        addRepaint( QRegion( w->shadowRect()));
        }
    }

void Compositor::releaseContents( SceneWindow* w )
    {
    delete w->texture;
    w->texture = NULL;
    if( w->pixmap != None )
        XFreePixmap( dpy, w->pixmap );
    w->pixmap = None;
    }

// the window's pixmap as a texture (once per map and size)
bool Compositor::bindContents( SceneWindow* w )
    {
#ifdef COMPOSITOR_BUILD
    if( w->texture != NULL )
        return true;
    if( w->bind_failed )
        return false;
    trapErrors( dpy );
    w->pixmap = XCompositeNameWindowPixmap( dpy, w->id );
    int errors = untrapErrors( dpy );
    if( errors == 0 )
        {
        trap();
        w->texture = renderer->textureFromPixmap( w->pixmap, w->geom.width(), w->geom.height(), w->depth, w->visual );
        XSync( dpy, False );
        errors = untrap();
        }
    if( errors != 0 || w->texture == NULL )
        {
        releaseContents( w );
        w->bind_failed = true;
        if( debug )
            kdWarning( 1212 ) << "compositor: no contents for window 0x" << QString::number( w->id, 16 ) << endl;
        return false;
        }
    return true;
#else
    Q_UNUSED( w );
    return false;
#endif
    }

QRegion Compositor::extents( const SceneWindow* w ) const
    {
    QRegion r( w->geom );
    if( w->shadow != NoShadow )
        r |= QRegion( w->shadowRect());
    return r;
    }

void Compositor::damageWindow( SceneWindow* w )
    {
    addRepaint( extents( w ));
    }

void Compositor::addRepaint( const QRegion& r )
    {
    if( !active || r.isEmpty())
        return;
    pending |= r & QRegion( 0, 0, screen_w, screen_h );
    scheduleRepaint();
    }

void Compositor::addRepaintFull()
    {
    addRepaint( QRegion( 0, 0, screen_w, screen_h ));
    }

bool Compositor::isUnredirected( Window w ) const
    {
    SceneWindow* sw = findWindow( w );
    return sw != NULL && sw->unredirected;
    }

CompositorUpdate::~CompositorUpdate()
    {
    Workspace* ws = Workspace::self();
    if( ws != NULL && ws->compositor() != NULL )
        ws->compositor()->clientChanged( client );
    }

void Compositor::clientChanged( Client* c )
    {
    if( !active || c == NULL )
        return;
    SceneWindow* w = findWindow( c->frameId());
    if( w == NULL )
        return;
    updateShadow( w );
    unredirect_timer.start( 0, true );
    }

//****************************************
// events
//****************************************

bool Compositor::x11Event( XEvent* e )
    {
#ifdef COMPOSITOR_BUILD
    if( !active )
        return false;
    if( e->type == damage_event + XDamageNotify )
        return handleDamage( e );
    switch( e->type )
        {
        case CreateNotify:
            if( e->xcreatewindow.parent == root )
                {
                SceneWindow* w = addWindow( e->xcreatewindow.window, false );
                if( w != NULL )
                    {
                    const int bw = e->xcreatewindow.border_width;
                    w->geom = QRect( e->xcreatewindow.x, e->xcreatewindow.y, e->xcreatewindow.width + 2 * bw,
                        e->xcreatewindow.height + 2 * bw );
                    w->border = bw;
                    w->override_redirect = e->xcreatewindow.override_redirect;
                    }
                }
            break;
        case DestroyNotify:
            if( e->xdestroywindow.event == root )
                if( SceneWindow* w = findWindow( e->xdestroywindow.window ))
                    {
                    w->damage = None; // (destroyed with the window)
                    removeWindow( w );
                    }
            break;
        case MapNotify:
            if( e->xmap.event == root )
                {
                SceneWindow* w = findWindow( e->xmap.window );
                if( w == NULL )
                    w = addWindow( e->xmap.window, false );
                if( w != NULL )
                    {
                    w->override_redirect = e->xmap.override_redirect;
                    windowMapped( w );
                    }
                }
            break;
        case UnmapNotify:
            if( e->xunmap.event == root )
                if( SceneWindow* w = findWindow( e->xunmap.window ))
                    windowUnmapped( w );
            break;
        case ConfigureNotify:
            if( e->xconfigure.event == root )
                if( SceneWindow* w = findWindow( e->xconfigure.window ))
                    {
                    const int bw = e->xconfigure.border_width;
                    const QRect geom( e->xconfigure.x, e->xconfigure.y, e->xconfigure.width + 2 * bw,
                        e->xconfigure.height + 2 * bw );
                    const bool visible = w->mapped && w->queried && !w->input_only;
                    if( visible )
                        damageWindow( w );
                    if( geom.size() != w->geom.size() || bw != w->border )
                        {
                        releaseContents( w ); // a new pixmap
                        w->bind_failed = false;
                        }
                    w->geom = geom;
                    w->border = bw;
                    w->override_redirect = e->xconfigure.override_redirect;
                    restack( w, e->xconfigure.above );
                    if( visible )
                        {
                        if( !w->shape.isNull())
                            fetchShape( w ); // (resized frames get new shapes, the event may come later)
                        damageWindow( w );
                        }
                    unredirect_timer.start( 0, true );
                    }
            break;
        case ReparentNotify:
            if( e->xreparent.event == root )
                {
                if( e->xreparent.parent == root )
                    {
                    SceneWindow* w = addWindow( e->xreparent.window, true );
                    if( w != NULL && w->mapped )
                        windowMapped( w );
                    }
                else if( SceneWindow* w = findWindow( e->xreparent.window ))
                    removeWindow( w );
                }
            break;
        case CirculateNotify:
            if( e->xcirculate.event == root )
                if( SceneWindow* w = findWindow( e->xcirculate.window ))
                    {
                    stacking.removeRef( w );
                    if( e->xcirculate.place == PlaceOnTop )
                        stacking.append( w );
                    else
                        stacking.prepend( w );
                    if( w->mapped )
                        damageWindow( w );
                    unredirect_timer.start( 0, true );
                    }
            break;
        case PropertyNotify:
            if( e->xproperty.window == root )
                {
                if( e->xproperty.atom == atom_rootpmap || e->xproperty.atom == atom_esetroot )
                    updateRootPixmap();
                }
            else if( e->xproperty.atom == atom_opacity )
                if( SceneWindow* w = findWindow( e->xproperty.window ))
                    {
                    fetchOpacity( w );
                    if( w->mapped )
                        damageWindow( w );
                    unredirect_timer.start( 0, true );
                    }
            break;
        default:
            if( shape_event != 0 && e->type == shape_event )
                {
                XShapeEvent* se = (XShapeEvent*) e;
                if( se->kind == ShapeBounding )
                    if( SceneWindow* w = findWindow( se->window ))
                        if( w->mapped && w->queried )
                            {
                            damageWindow( w );
                            fetchShape( w );
                            updateShadow( w );
                            damageWindow( w );
                            unredirect_timer.start( 0, true );
                            }
                }
            break;
        }
#else
    Q_UNUSED( e );
#endif
    return false;
    }

bool Compositor::handleDamage( XEvent* e )
    {
#ifdef COMPOSITOR_BUILD
    XDamageNotifyEvent* de = (XDamageNotifyEvent*) e;
    SceneWindow* w = findWindow( de->drawable );
    if( w == NULL || w->damage != de->damage )
        return true; // (the overview's and the animations' damage is theirs)
    w->damaged = true;
    if( !w->unredirected )
        {
        // relative to the window's origin, inside the border
        QRect r( de->area.x + w->geom.x() + w->border, de->area.y + w->geom.y() + w->border,
            de->area.width, de->area.height );
        addRepaint( QRegion( r ));
        }
    else
        scheduleRepaint(); // (only for the subtraction)
    return true;
#else
    Q_UNUSED( e );
    return false;
#endif
    }

// the damage seen so far is drawn: the server reports anything new again
void Compositor::flushDamage()
    {
#ifdef COMPOSITOR_BUILD
    for( QPtrListIterator< SceneWindow > it( stacking );
         it.current() != NULL;
         ++it )
        if( it.current()->damaged && it.current()->damage != None )
            {
            XDamageSubtract( dpy, it.current()->damage, None, None );
            it.current()->damaged = false;
            }
#endif
    }

void Compositor::updateRootPixmap()
    {
    delete root_texture;
    root_texture = NULL;
    root_pixmap = None;
    if( renderer == NULL )
        return;
    Atom type;
    int format;
    unsigned long n, left;
    unsigned char* data = NULL;
    if( XGetWindowProperty( dpy, root, atom_rootpmap, 0, 1, False, XA_PIXMAP, &type, &format, &n, &left,
            &data ) == Success && data != NULL && format == 32 && n == 1 )
        root_pixmap = *(Pixmap*) data;
    if( data != NULL )
        XFree( data );
    if( root_pixmap != None )
        {
        Window r;
        int x, y;
        unsigned int w, h, bw, depth;
        trap();
        if( XGetGeometry( dpy, root_pixmap, &r, &x, &y, &w, &h, &bw, &depth ))
            root_texture = renderer->textureFromPixmap( root_pixmap, w, h, depth, NULL );
        XSync( dpy, False );
        if( untrap() != 0 )
            {
            delete root_texture;
            root_texture = NULL;
            }
        }
    addRepaintFull();
    }

//****************************************
// painting
//****************************************

/*
 When a frame is drawn: as late as it safely makes the next refresh of a
 monitor that gets something (render_budget before it), so that all
 damage until then is in it and shown at that refresh, also when more
 comes during the frame (drawing at once would queue the frame and leave
 later damage for the refresh after it).  The refreshes are known from the
 last presentations; without them (idle for a while, no Present) the frame
 is drawn at once.  Frames that miss their refresh make the budget larger;
 servers that show frames at once (Xwayland) or whose refreshes are not
 where their timestamps say (Xvfb) get frames at once.
*/
void Compositor::scheduleRepaint()
    {
    if( !active || repaint_timer.isActive())
        return;
    if( frame_wanted )
        return; // (the completion of the queued frame starts it)
    int delay = 0;
    if( !asap && !immediate_server && !late_failed )
        {
        const double t = now();
        double earliest = -1;
        for( QPtrListIterator< CompositorOutput > it( outputs );
             it.current() != NULL;
             ++it )
            {
            if( !outputWanted( it.current()))
                continue;
            const Presenter* p = it.current()->presenter;
            const double last = p->lastShown();
            const double period = p->refreshPeriod();
            if( !p->usesPresent() || last <= 0 || t < last || t - last >= 2000 || period <= 4 )
                {
                earliest = t;
                break;
                }
            const double next = last + ( floor(( t - last ) / period ) + 1 ) * period;
            if( earliest < 0 || next < earliest )
                earliest = next;
            }
        if( earliest > 0 && earliest - render_budget > t )
            delay = (int) floor( earliest - render_budget - t );
        }
    repaint_timer.start( delay, true );
    }

// the pending damage is (partly) on o's monitor, or o missed frames
bool Compositor::outputWanted( const CompositorOutput* o ) const
    {
    return o->deferred || !( pending & QRegion( o->geom )).isEmpty();
    }

// a frame is queued for o's next refresh
static bool queuedOn( const CompositorOutput* o )
    {
    return o->presenter->usesPresent() && o->presenter->queued() >= 1;
    }

void Compositor::outputCompleted( bool skipped, int missed )
    {
    CompositorOutput* o = NULL;
    for( QPtrListIterator< CompositorOutput > it( outputs );
         it.current() != NULL && o == NULL;
         ++it )
        if( it.current()->presenter == sender())
            o = it.current();
    if( o == NULL )
        return;
    o->count();
    // KWIN_COMPOSITOR_DEBUG=2: every frame (the latency tests)
    static const bool every = getenv( "KWIN_COMPOSITOR_DEBUG" ) != NULL
        && strcmp( getenv( "KWIN_COMPOSITOR_DEBUG" ), "2" ) == 0;
    if( every )
        kdWarning( 1212 ) << "compositor: " << ( o->presenter == presenter ? "" : "other monitor " ) << "shown at "
            << QString::number( o->presenter->lastShown(), 'f', 3 ) << " msc "
            << (unsigned long) o->presenter->lastMsc() << " period "
            << QString::number( o->presenter->refreshPeriod(), 'f', 3 ) << " mode " << o->presenter->lastMode()
            << ( skipped ? " skipped" : "" ) << endl;
    if( o->presenter == presenter && leadCompleted( skipped, missed ))
        return; // (the outputs are new)
    if( o->deferred && !queuedOn( o ) && latest_buffer >= 0 )
        presentTo( o, latest_buffer ); // the newest frame, with all it missed
    if( frame_wanted )
        {
        frame_wanted = false;
        scheduleRepaint();
        }
    }

void Compositor::outputIdle()
    {
    if( frame_wanted )
        {
        frame_wanted = false;
        scheduleRepaint();
        }
    }

// the presentation on the monitor that paces the frames completed: timing
// statistics, the budget, flips or not; true when the outputs were switched
bool Compositor::leadCompleted( bool skipped, int missed )
    {
    if( skipped )
        ++stat_skipped;
    stat_missed += missed;
    if( !skipped && submit_time > 0 )
        { // servers that show frames at once (Xwayland): no waiting for refreshes
        shown_delay[ shown_index++ % 16 ] = presenter->lastShown() - submit_time;
        if( shown_index >= 16 )
            {
            QValueVector< double > v( 16 );
            for( int i = 0;
                 i < 16;
                 ++i )
                v[ i ] = shown_delay[ i ];
            qHeapSort( v );
            const bool immediate = v[ 8 ] < 1.0 && presenter->refreshPeriod() > 8;
            if( immediate != immediate_server && debug )
                kdWarning( 1212 ) << "compositor: the X server shows frames "
                    << ( immediate ? "at once" : "at refreshes" ) << endl;
            immediate_server = immediate;
            }
        }
    submit_time = 0;
    if( target_vblank > 0 && !skipped && !immediate_server && !late_failed )
        { // too late for the refresh it was meant for: more time from now on
        if( presenter->lastShown() > target_vblank + presenter->refreshPeriod() / 2 )
            {
            ++stat_late;
            on_time = 0;
            if( render_budget < 10.0 )
                render_budget += 1.0;
            else if( ++late_at_max >= 10 )
                { // the refreshes are not where they seem to be (Xvfb): at once
                late_failed = true;
                if( debug )
                    kdWarning( 1212 ) << "compositor: frames keep missing their refresh, drawn at once now" << endl;
                }
            }
        else if( ++on_time >= 300 && render_budget > min_budget )
            {
            render_budget = QMAX( min_budget, render_budget - 0.5 );
            on_time = 0;
            late_at_max = 0;
            }
        }
    target_vblank = 0;
    // copies of the overlay on several monitors tear on all but one of them:
    // each monitor at its own refresh then
    if( !per_monitor && !skipped && QApplication::desktop()->numScreens() > 1 && force_mode == 0 )
        {
        if( presenter->lastMode() == Presenter::ModeCopy )
            {
            if( ++copies_in_row >= 3 )
                {
                flips_failed = true;
                if( debug )
                    kdWarning( 1212 ) << "compositor: the overlay is copied, not flipped" << endl;
                setupOutputs( true );
                scheduleRepaint();
                return true;
                }
            }
        else if( presenter->lastMode() == Presenter::ModeFlip )
            copies_in_row = 0;
        }
    return false;
    }

/*
 Presenting: the overlay as one window, which Present page-flips on all
 monitors (each at its own vblank) when it can.  When it copies instead
 (on several monitors), each monitor gets a child window of the overlay
 presented at its own refresh, so that no monitor is copied to in the
 middle of its scanout.  Also so while the overlay has holes (unredirected
 windows): it can't be flipped then.
*/
void Compositor::setupOutputs( bool per_monitor_mode )
    {
#ifdef COMPOSITOR_BUILD
    freeOutputs();
    QDesktopWidget* desktop = QApplication::desktop();
    per_monitor = per_monitor_mode && desktop->numScreens() > 1 && force_mode != 1;
    const int count = per_monitor ? desktop->numScreens() : 1;
    const int primary = desktop->primaryScreen();
    for( int i = 0;
         i < count;
         ++i )
        {
        CompositorOutput* o = new CompositorOutput;
        if( per_monitor )
            {
            o->geom = desktop->screenGeometry( i ) & QRect( 0, 0, screen_w, screen_h );
            XSetWindowAttributes attr;
            attr.background_pixmap = None; // (the screen stays as it is until the first frame)
            o->window = XCreateWindow( dpy, overlay, o->geom.x(), o->geom.y(), o->geom.width(), o->geom.height(),
                0, CopyFromParent, InputOutput, CopyFromParent, CWBackPixmap, &attr );
            o->own_window = true;
            XserverRegion empty = XFixesCreateRegion( dpy, NULL, 0 );
            XFixesSetWindowShapeRegion( dpy, o->window, ShapeInput, 0, 0, empty );
            XFixesDestroyRegion( dpy, empty );
            XMapWindow( dpy, o->window );
            }
        else
            {
            o->geom = QRect( 0, 0, screen_w, screen_h );
            o->window = overlay;
            }
        o->presenter = new Presenter( dpy );
        o->presenter->startExternal( o->window, buffer_pixmaps, BufferCount, have_present );
        o->pending = QRegion( o->geom );
        if( per_monitor && i == primary )
            outputs.prepend( o ); // the primary monitor paces the frames
        else
            outputs.append( o );
        }
    presenter = outputs.first()->presenter;
    for( QPtrListIterator< CompositorOutput > it( outputs );
         it.current() != NULL;
         ++it )
        {
        connect( it.current()->presenter, SIGNAL( completed( bool, int )), SLOT( outputCompleted( bool, int )));
        connect( it.current()->presenter, SIGNAL( idle()), SLOT( outputIdle()));
        }
    copies_in_row = 0;
    frame_wanted = false;
    submit_time = 0;
    target_vblank = 0;
    // (presentations of the old windows may still be queued: their buffers
    // aren't drawn into for a few refreshes)
    hold_until = now() + 60;
    if( active )
        {
        addRepaintFull();
        if( debug )
            kdWarning( 1212 ) << "compositor: presenting " << ( per_monitor ? "per monitor" : "the overlay" ) << endl;
        }
#else
    Q_UNUSED( per_monitor_mode );
#endif
    }

void Compositor::freeOutputs()
    {
    presenter = NULL;
    for( QPtrListIterator< CompositorOutput > it( outputs );
         it.current() != NULL;
         ++it )
        {
        CompositorOutput* o = it.current();
        o->presenter->stop();
        delete o->presenter;
        if( o->own_window )
            XDestroyWindow( dpy, o->window );
        delete o;
        }
    outputs.clear();
    }

// a buffer no presentation uses, the most recent one (least to draw)
int Compositor::freeBuffer() const
    {
    int best = -1;
    for( int i = 0;
         i < BufferCount;
         ++i )
        {
        bool busy = false;
        for( QPtrListIterator< CompositorOutput > it( outputs );
             it.current() != NULL && !busy;
             ++it )
            busy = it.current()->presenter->isBusy( i );
        if( !busy && ( best < 0 || buffer_frame[ i ] > buffer_frame[ best ] ))
            best = i;
        }
    return best;
    }

// what changed on o's monitor since its last presentation, from buffer buf
// (copies: only that is copied); false when nothing changed there
bool Compositor::presentTo( CompositorOutput* o, int buf )
    {
#ifdef COMPOSITOR_BUILD
    o->deferred = false;
    QRegion update = o->pending - overlay_hole;
    o->pending = QRegion();
    if( update.isEmpty())
        return false;
    QMemArray< QRect > rects = update.rects();
    XRectangle* xr = new XRectangle[ QMAX( 1, (int) rects.size()) ];
    for( unsigned int i = 0;
         i < rects.size();
         ++i )
        {
        xr[ i ].x = rects[ i ].x();
        xr[ i ].y = rects[ i ].y();
        xr[ i ].width = rects[ i ].width();
        xr[ i ].height = rects[ i ].height();
        }
    XserverRegion region = XFixesCreateRegion( dpy, xr, rects.size());
    delete[] xr;
    o->presenter->presentBuffer( buf, region, o->geom.x(), o->geom.y());
    XFixesDestroyRegion( dpy, region );
    return true;
#else
    Q_UNUSED( o );
    Q_UNUSED( buf );
    return false;
#endif
    }

void Compositor::performRepaint()
    {
#ifdef COMPOSITOR_BUILD
    if( !active )
        return;
    pending -= overlay_hole; // (drawn by the unredirected windows themselves)
    if( pending.isEmpty())
        {
        flushDamage();
        return;
        }
    if( hold_until > 0 )
        { // just switched the outputs
        const double t = now();
        if( t < hold_until )
            {
            repaint_timer.start( (int) ceil( hold_until - t ), true );
            return;
            }
        hold_until = 0;
        }
    // at most one frame queued in the server per monitor: wait when every
    // monitor that gets something has one
    bool can = false;
    for( QPtrListIterator< CompositorOutput > it( outputs );
         it.current() != NULL && !can;
         ++it )
        can = outputWanted( it.current()) && !queuedOn( it.current());
    if( !can )
        {
        frame_wanted = true;
        return;
        }
    const int buf = freeBuffer();
    if( buf < 0 )
        {
        frame_wanted = true; // (outputIdle())
        return;
        }
    const double start = now();
    flushDamage();
    QRegion damage = pending;
    pending = QRegion();
    ++frame_number;
    const int history = frame_damage.size();
    frame_damage[ frame_number % history ] = damage;
    QRegion repaint;
    if( buffer_frame[ buf ] < 0 || frame_number - buffer_frame[ buf ] >= history )
        repaint = QRegion( 0, 0, screen_w, screen_h );
    else
        for( int f = buffer_frame[ buf ] + 1;
             f <= frame_number;
             ++f )
            repaint |= frame_damage[ f % history ];
    repaint -= overlay_hole;
    renderer->acquireClientBuffers();
    paint( buffers[ buf ], repaint );
    renderer->finish();
    if( renderer->isLost())
        {
        stop( true, i18n( "the GPU was reset" ));
        return;
        }
    buffer_frame[ buf ] = frame_number;
    latest_buffer = buf;
    // the refresh the frame is meant for (for the budget), when it was drawn
    // in time for it: not for damage that came too late for it anyway
    const double submit = now();
    target_vblank = 0;
    submit_time = 0;
    if( presenter->usesPresent() && presenter->lastShown() > 0 )
        {
        const double last = presenter->lastShown();
        const double period = presenter->refreshPeriod();
        if( submit >= last && submit - last < 2000 )
            {
            target_vblank = last + ( floor(( submit - last ) / period ) + 1 ) * period;
            if( start > target_vblank - render_budget + 0.5 )
                target_vblank = 0;
            }
        }
    // what changed goes to each monitor; one that still has a frame queued
    // gets the newest frame when that one was shown
    for( QPtrListIterator< CompositorOutput > it( outputs );
         it.current() != NULL;
         ++it )
        {
        CompositorOutput* o = it.current();
        o->pending |= damage & QRegion( o->geom );
        if( queuedOn( o ))
            o->deferred = !o->pending.isEmpty();
        else if( presentTo( o, buf ))
            {
            if( o->presenter == presenter )
                submit_time = submit; // (for leadCompleted())
            }
        else if( o->presenter == presenter )
            target_vblank = 0;
        }
    debugFrame( start, repaint );
#endif
    }

void Compositor::debugFrame( double start, const QRegion& repaint )
    {
    const double t = now() - start;
    ++stat_frames;
    stat_render += t;
    stat_max_render = QMAX( stat_max_render, t );
    if( !debug )
        return;
    QMemArray< QRect > rects = repaint.rects();
    double px = 0;
    for( unsigned int i = 0;
         i < rects.size();
         ++i )
        px += rects[ i ].width() * (double) rects[ i ].height();
    // KWIN_COMPOSITOR_DEBUG=2: every frame (the latency tests)
    static const bool every = getenv( "KWIN_COMPOSITOR_DEBUG" ) != NULL
        && strcmp( getenv( "KWIN_COMPOSITOR_DEBUG" ), "2" ) == 0;
    if( every || stat_frames % 300 == 0 )
        kdWarning( 1212 ) << "compositor: frame " << frame_number << " at " << QString::number( start, 'f', 3 )
            << " render " << QString::number( t, 'f', 3 ) << " ms, " << (long) px << " px; avg "
            << QString::number( stat_render / stat_frames, 'f', 3 ) << " max "
            << QString::number( stat_max_render, 'f', 3 ) << " skipped " << stat_skipped << " missed "
            << stat_missed << " late " << stat_late << " budget " << render_budget << outputStats() << endl;
    }

// "; monitor 0: flips 10 copies 0 skipped 0; ..."
QString Compositor::outputStats() const
    {
    QString s;
    int i = 0;
    for( QPtrListIterator< CompositorOutput > it( outputs );
         it.current() != NULL;
         ++it, ++i )
        s += QString( "; %1 %2: flips %3 copies %4 skipped %5" ).arg( per_monitor ? "monitor" : "overlay" ).arg( i )
            .arg( it.current()->flips ).arg( it.current()->copies ).arg( it.current()->skips );
    return s;
    }

void Compositor::paintBackground( const QRegion& region )
    {
    if( region.isEmpty())
        return;
    renderer->setClip( region );
    if( root_texture != NULL )
        {
        renderer->drawTexture( root_texture, 0, 0, 1.0, Render::BlendSource );
        QRegion rest = region - QRegion( 0, 0, root_texture->width(), root_texture->height());
        if( !rest.isEmpty())
            {
            renderer->setClip( rest );
            renderer->fill( RectF( 0, 0, screen_w, screen_h ), Qt::black, 1.0, Render::BlendSource );
            }
        }
    else
        renderer->fill( RectF( 0, 0, screen_w, screen_h ), Qt::black, 1.0, Render::BlendSource );
    }

void Compositor::paint( Render::Texture* target, const QRegion& region )
    {
    if( !renderer->setTarget( target ))
        return;
    const QRect screen( 0, 0, screen_w, screen_h );
    // top-down: opaque windows, each where nothing opaque is above it
    QRegion left = region;
    QPtrList< SceneWindow > visible;
    QPtrListIterator< SceneWindow > it( stacking );
    for( it.toLast();
         it.current() != NULL;
         --it )
        {
        SceneWindow* w = it.current();
        if( !w->isPaintable( screen ))
            continue;
        if( !bindContents( w ))
            continue;
        w->clip = left;
        visible.prepend( w );
        if( w->isOpaque())
            {
            QRegion shape = w->shapeOnScreen();
            QRegion draw = left & shape;
            if( !draw.isEmpty())
                {
                renderer->setClip( draw );
                renderer->drawTexture( w->texture, w->geom.x(), w->geom.y(), 1.0, Render::BlendSource );
                }
            left -= shape;
            if( left.isEmpty())
                break;
            }
        }
    paintBackground( left );
    // bottom-up: shadows and translucent windows
    for( QPtrListIterator< SceneWindow > vit( visible );
         vit.current() != NULL;
         ++vit )
        {
        SceneWindow* w = vit.current();
        if( w->shadow != NoShadow )
            {
            QRect outer = w->shadowRect();
            QRegion clip = ( w->clip & QRegion( outer )) - w->shapeOnScreen();
            if( !clip.isEmpty())
                {
                const ShadowTiles* tiles = shadows->tiles( w->shadow_radius, w->shape, w->geom.size());
                if( tiles != NULL )
                    {
                    renderer->setClip( clip );
                    renderer->drawNineSlice( tiles->tile, outer, Qt::black, w->shadow_alpha );
                    }
                }
            }
        if( !w->isOpaque())
            {
            QRegion clip = w->clip & w->shapeOnScreen();
            if( !clip.isEmpty())
                {
                renderer->setClip( clip );
                renderer->drawTexture( w->texture, w->geom.x(), w->geom.y(), w->opacity, Render::BlendOver );
                }
            }
        }
    renderer->clearClip();
    }

//****************************************
// unredirection
//****************************************

// a full screen window that could be drawn directly: managed and full
// screen, or override-redirect covering a whole monitor; opaque
bool Compositor::unredirectCandidate( const SceneWindow* w ) const
    {
    if( !w->mapped || !w->queried || w->input_only || !w->isOpaque())
        return false;
    if( w->locker )
        return true;
    if( !options->compositingUnredirectFullscreen )
        return false;
    Client* c = ws->findClient( FrameIdMatchPredicate( w->id ));
    if( c != NULL )
        {
        if( !c->isFullScreen())
            return false;
        }
    else
        {
        if( !w->override_redirect )
            return false;
        bool covers = false;
        QDesktopWidget* desktop = QApplication::desktop();
        for( int i = 0;
             i < desktop->numScreens();
             ++i )
            if( w->geom.contains( desktop->screenGeometry( i )))
                covers = true;
        if( !covers )
            return false;
        }
    // the whole shape must cover the monitors it is on: no holes, nothing
    // of what is below to compose with
    return w->shape.isNull();
    }

void Compositor::checkUnredirect()
    {
    if( !active )
        return;
    const double t = now();
    double next = -1;
    const QRect screen( 0, 0, screen_w, screen_h );
    // top-down: what is above each window
    QRegion above;
    QPtrListIterator< SceneWindow > it( stacking );
    for( it.toLast();
         it.current() != NULL;
         --it )
        {
        SceneWindow* w = it.current();
        const bool shown = w->mapped && w->queried && !w->input_only && w->geom.intersects( screen );
        if( !shown )
            continue;
        bool candidate = unredirectCandidate( w );
        // (the locker stays unredirected whatever comes above it)
        if( candidate && !w->locker && !( above & w->shapeOnScreen()).isEmpty())
            candidate = false;
        if( !candidate )
            {
            w->candidate_since = 0;
            if( w->unredirected )
                {
                setUnredirected( w, false );
                w->redirected_at = t;
                }
            }
        else if( !w->unredirected )
            {
            if( w->candidate_since == 0 )
                w->candidate_since = t;
            const double delay = w->locker ? 0
                : ( w->redirected_at > 0 && t - w->redirected_at < 3000 ) ? UnredirectAgainDelay : UnredirectDelay;
            if( t - w->candidate_since >= delay )
                setUnredirected( w, true );
            else if( next < 0 || w->candidate_since + delay - t < next )
                next = w->candidate_since + delay - t;
            }
        above |= extents( w );
        }
    if( next >= 0 )
        unredirect_timer.start( (int) ceil( next ) + 1, true );
    }

/*
 Unredirecting: the overlay is cut out of the window's area first (the
 screen keeps our last frame there: the root's background is not painted
 while its children are redirected), then the server copies the window's
 pixmap onto the screen: the same pixels, nothing exposed.  Redirecting
 again: the new pixmap gets a copy of the screen (the window as it is),
 the overlay covers it again and shows the same until the next frame.
*/
void Compositor::setUnredirected( SceneWindow* w, bool unredirect )
    {
#ifdef COMPOSITOR_BUILD
    if( w->unredirected == unredirect )
        return;
    if( debug )
        kdWarning( 1212 ) << "compositor: " << ( unredirect ? "unredirecting" : "redirecting" ) << " 0x"
            << QString::number( w->id, 16 ) << ( w->locker ? " (screen locker)" : "" ) << endl;
    w->unredirected = unredirect;
    overlay_hole = QRegion();
    for( QPtrListIterator< SceneWindow > it( stacking );
         it.current() != NULL;
         ++it )
        if( it.current()->unredirected )
            overlay_hole |= it.current()->shapeOnScreen();
    trap();
    if( unredirect )
        {
        updateOverlayShape();
        XCompositeUnredirectWindow( dpy, w->id, CompositeRedirectManual );
        releaseContents( w );
        }
    else
        {
        XCompositeRedirectWindow( dpy, w->id, CompositeRedirectManual );
        w->bind_failed = false;
        updateOverlayShape();
        damageWindow( w );
        }
    XSync( dpy, False );
    untrap();
#else
    Q_UNUSED( w );
    Q_UNUSED( unredirect );
#endif
    }

void Compositor::updateOverlayShape()
    {
#ifdef COMPOSITOR_BUILD
    if( overlay == None )
        return;
    // a shaped overlay can't be flipped: per monitor; whole again: flips
    // (unless they didn't work)
    if( force_mode == 0 && QApplication::desktop()->numScreens() > 1 && !outputs.isEmpty())
        {
        const bool want = !overlay_hole.isEmpty() || flips_failed;
        if( want != per_monitor )
            setupOutputs( want );
        }
    if( overlay_hole.isEmpty())
        {
        XFixesSetWindowShapeRegion( dpy, overlay, ShapeBounding, 0, 0, None );
        return;
        }
    QMemArray< QRect > rects = ( QRegion( 0, 0, screen_w, screen_h ) - overlay_hole ).rects();
    XRectangle* xr = new XRectangle[ QMAX( 1, (int) rects.size()) ];
    for( unsigned int i = 0;
         i < rects.size();
         ++i )
        {
        xr[ i ].x = rects[ i ].x();
        xr[ i ].y = rects[ i ].y();
        xr[ i ].width = rects[ i ].width();
        xr[ i ].height = rects[ i ].height();
        }
    XserverRegion region = XFixesCreateRegion( dpy, xr, rects.size());
    delete[] xr;
    XFixesSetWindowShapeRegion( dpy, overlay, ShapeBounding, 0, 0, region );
    XFixesDestroyRegion( dpy, region );
#endif
    }

} // namespace

#include "compositor.moc"
