/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2026 The KDE3 revival project

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

#include <config.h>

#include "render_xrender.h"

#include <klocale.h>

#include <X11/Xutil.h>
#include <fixx11h.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace KWinInternal
{

namespace Render
{

#ifdef HAVE_XRENDER

static bool hostLittleEndian()
    {
    const int one = 1;
    return *(const char*) &one == 1;
    }

class XRTexture : public Texture
    {
    public:
        XRTexture( Display* d, int w, int h, bool alpha, bool alpha_only, Pixmap pix, bool own, Picture pic );
        virtual ~XRTexture();
        // the picture's attributes, set only when they change
        void setFilter( Filter f );
        void setRepeat( Repeat r );
        // dst pixel i, j of a composite samples the picture at
        // ( i + 0.5 ) * sx + tx, ( j + 0.5 ) * sy + ty
        void setTransform( double sx, double sy, double tx, double ty );
        void setIdentity();
        Display* dpy;
        Pixmap pixmap;
        bool own_pixmap;
        Picture picture;
        int filter;
        int repeat;
        bool identity;
        double sx, sy, tx, ty;
    };

XRTexture::XRTexture( Display* d, int w, int h, bool alpha, bool alpha_only, Pixmap pix, bool own, Picture pic )
    : Texture( w, h, alpha, alpha_only ), dpy( d ), pixmap( pix ), own_pixmap( own ), picture( pic ),
      filter( NearestFilter ), repeat( NoRepeat ), identity( true ), sx( 1 ), sy( 1 ), tx( 0 ), ty( 0 )
    {
    }

XRTexture::~XRTexture()
    {
    if( picture != None )
        XRenderFreePicture( dpy, picture );
    if( own_pixmap && pixmap != None )
        XFreePixmap( dpy, pixmap );
    }

void XRTexture::setFilter( Filter f )
    {
    if( filter == f )
        return;
    filter = f;
    XRenderSetPictureFilter( dpy, picture, f == BilinearFilter ? FilterBilinear : FilterNearest, NULL, 0 );
    }

void XRTexture::setRepeat( Repeat r )
    {
    if( repeat == r )
        return;
    repeat = r;
    XRenderPictureAttributes pa;
    pa.repeat = r == PadRepeat ? RepeatPad : r == TileRepeat ? RepeatNormal : RepeatNone;
    XRenderChangePicture( dpy, picture, CPRepeat, &pa );
    }

void XRTexture::setTransform( double sx_, double sy_, double tx_, double ty_ )
    {
    if( !identity && sx == sx_ && sy == sy_ && tx == tx_ && ty == ty_ )
        return;
    identity = false;
    sx = sx_;
    sy = sy_;
    tx = tx_;
    ty = ty_;
    Compositing::setTransform( dpy, picture, sx, sy, tx, ty );
    }

void XRTexture::setIdentity()
    {
    if( identity )
        return;
    identity = true;
    Compositing::setTransform( dpy, picture, 1, 1, 0, 0 );
    }

XRenderRenderer::XRenderRenderer( Display* d )
    : Renderer( d ), target( NULL ), clipped( false ), clip_dirty( true ), scratch_pixmap( None ),
      scratch_picture( None ), scratch_w( 0 ), scratch_h( 0 ), render_major( 0 ), render_minor( 0 )
    {
    for( int i = 0;
         i < 256;
         ++i )
        alphas[ i ] = None;
    }

Renderer* XRenderRenderer::create( Display* dpy, QString* error )
    {
    int ev, err;
    int major = 0, minor = 0;
    // RepeatPad and solid fills need Render 0.10, transformations and filters 0.6
    if( !XRenderQueryExtension( dpy, &ev, &err ) || !XRenderQueryVersion( dpy, &major, &minor )
        || ( major == 0 && minor < 10 ))
        {
        if( error != NULL )
            *error = i18n( "The X server lacks RENDER 0.10." );
        return NULL;
        }
    XRenderRenderer* r = new XRenderRenderer( dpy );
    r->render_major = major;
    r->render_minor = minor;
    return r;
    }

XRenderRenderer::~XRenderRenderer()
    {
    for( QMap< unsigned int, XID >::ConstIterator it = solids.begin();
         it != solids.end();
         ++it )
        XRenderFreePicture( dpy, *it );
    for( int i = 0;
         i < 256;
         ++i )
        if( alphas[ i ] != None )
            XRenderFreePicture( dpy, alphas[ i ] );
    if( scratch_picture != None )
        XRenderFreePicture( dpy, scratch_picture );
    if( scratch_pixmap != None )
        XFreePixmap( dpy, scratch_pixmap );
    }

QString XRenderRenderer::description() const
    {
    return QString( "XRender %1.%2" ).arg( render_major ).arg( render_minor );
    }

Texture* XRenderRenderer::textureFromPixmap( Pixmap pixmap, int w, int h, int depth, Visual* visual )
    {
    XRenderPictFormat* format = NULL;
    if( visual != NULL )
        format = XRenderFindVisualFormat( dpy, visual );
    if( format == NULL )
        format = XRenderFindStandardFormat( dpy, depth == 32 ? PictStandardARGB32
            : depth == 8 ? PictStandardA8 : PictStandardRGB24 );
    if( format == NULL )
        return NULL;
    const bool alpha = format->type == PictTypeDirect && format->direct.alphaMask != 0;
    Picture pic = XRenderCreatePicture( dpy, pixmap, format, 0, NULL );
    return new XRTexture( dpy, w, h, alpha, depth == 8, pixmap, false, pic );
    }

Texture* XRenderRenderer::textureFromImage( const QImage& image )
    {
    QImage img = image.convertDepth( 32 );
    const int w = img.width();
    const int h = img.height();
    if( w <= 0 || h <= 0 )
        return NULL;
    const bool alpha = img.hasAlphaBuffer();
    // premultiplied ARGB32 in host order (XPutImage swaps if needed)
    unsigned int* data = (unsigned int*) malloc( w * h * 4 );
    if( data == NULL )
        return NULL;
    for( int y = 0;
         y < h;
         ++y )
        {
        const QRgb* src = (const QRgb*) img.scanLine( y );
        unsigned int* dst = data + y * w;
        for( int x = 0;
             x < w;
             ++x )
            {
            const QRgb p = src[ x ];
            const unsigned int a = alpha ? qAlpha( p ) : 255;
            dst[ x ] = ( a << 24 ) | (( qRed( p ) * a / 255 ) << 16 ) | (( qGreen( p ) * a / 255 ) << 8 )
                | ( qBlue( p ) * a / 255 );
            }
        }
    Pixmap pix = XCreatePixmap( dpy, DefaultRootWindow( dpy ), w, h, 32 );
    XImage* xi = XCreateImage( dpy, DefaultVisual( dpy, DefaultScreen( dpy )), 32, ZPixmap, 0, (char*) data,
        w, h, 32, w * 4 );
    if( xi == NULL )
        {
        free( data );
        XFreePixmap( dpy, pix );
        return NULL;
        }
    xi->byte_order = hostLittleEndian() ? LSBFirst : MSBFirst;
    GC gc = XCreateGC( dpy, pix, 0, NULL );
    XPutImage( dpy, pix, gc, xi, 0, 0, 0, 0, w, h );
    XFreeGC( dpy, gc );
    XDestroyImage( xi ); // frees data
    Picture pic = XRenderCreatePicture( dpy, pix, XRenderFindStandardFormat( dpy, PictStandardARGB32 ), 0, NULL );
    return new XRTexture( dpy, w, h, alpha, false, pix, true, pic );
    }

Texture* XRenderRenderer::textureFromAlpha( const unsigned char* data, int w, int h, int stride )
    {
    if( w <= 0 || h <= 0 )
        return NULL;
    const int line = ( w + 3 ) & ~3;
    char* copy = (char*) malloc( line * h );
    if( copy == NULL )
        return NULL;
    for( int y = 0;
         y < h;
         ++y )
        memcpy( copy + y * line, data + y * stride, w );
    Pixmap pix = XCreatePixmap( dpy, DefaultRootWindow( dpy ), w, h, 8 );
    XImage* xi = XCreateImage( dpy, DefaultVisual( dpy, DefaultScreen( dpy )), 8, ZPixmap, 0, copy,
        w, h, 32, line );
    if( xi == NULL )
        {
        free( copy );
        XFreePixmap( dpy, pix );
        return NULL;
        }
    GC gc = XCreateGC( dpy, pix, 0, NULL );
    XPutImage( dpy, pix, gc, xi, 0, 0, 0, 0, w, h );
    XFreeGC( dpy, gc );
    XDestroyImage( xi );
    Picture pic = XRenderCreatePicture( dpy, pix, XRenderFindStandardFormat( dpy, PictStandardA8 ), 0, NULL );
    return new XRTexture( dpy, w, h, true, true, pix, true, pic );
    }

Texture* XRenderRenderer::createOffscreen( int w, int h, bool alpha )
    {
    if( w <= 0 || h <= 0 )
        return NULL;
    Pixmap pix = XCreatePixmap( dpy, DefaultRootWindow( dpy ), w, h, alpha ? 32 : 24 );
    Picture pic = XRenderCreatePicture( dpy, pix,
        XRenderFindStandardFormat( dpy, alpha ? PictStandardARGB32 : PictStandardRGB24 ), 0, NULL );
    XRenderColor clear = { 0, 0, 0, 0 };
    XRenderFillRectangle( dpy, PictOpSrc, pic, &clear, 0, 0, w, h );
    return new XRTexture( dpy, w, h, alpha, false, pix, true, pic );
    }

Texture* XRenderRenderer::createPresentBuffer( Window window, int w, int h, Pixmap* pixmap )
    {
    const int depth = DefaultDepth( dpy, DefaultScreen( dpy ));
    XRenderPictFormat* format = XRenderFindVisualFormat( dpy, DefaultVisual( dpy, DefaultScreen( dpy )));
    if( format == NULL )
        return NULL;
    Pixmap pix = XCreatePixmap( dpy, window, w, h, depth );
    Picture pic = XRenderCreatePicture( dpy, pix, format, 0, NULL );
    *pixmap = pix;
    return new XRTexture( dpy, w, h, false, false, pix, true, pic );
    }

void XRenderRenderer::acquireClientBuffers()
    {
    // the X server renders in request order
    }

SyncPoint XRenderRenderer::finish()
    {
    XFlush( dpy );
    return SyncPoint();
    }

bool XRenderRenderer::setTarget( Texture* t )
    {
    target = static_cast< XRTexture* >( t );
    clipped = false;
    clip_dirty = true;
    clip = QRegion();
    return target != NULL;
    }

void XRenderRenderer::setClip( const QRegion& c )
    {
    if( clipped && c == clip )
        return;
    clip = c;
    clipped = true;
    clip_dirty = true;
    }

void XRenderRenderer::clearClip()
    {
    if( !clipped )
        return;
    clipped = false;
    clip_dirty = true;
    clip = QRegion();
    }

void XRenderRenderer::applyClip()
    {
    if( !clip_dirty || target == NULL )
        return;
    clip_dirty = false;
    if( !clipped )
        {
        XRenderPictureAttributes pa;
        pa.clip_mask = None;
        XRenderChangePicture( dpy, target->picture, CPClipMask, &pa );
        return;
        }
    QMemArray< QRect > rects = clip.rects();
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
    XRenderSetPictureClipRectangles( dpy, target->picture, 0, 0, xr, rects.size());
    delete[] xr;
    }

XID XRenderRenderer::solidPicture( const QColor& c, double alpha )
    {
    XRenderColor col = Compositing::renderColor( c, alpha );
    const unsigned int key = (( col.alpha >> 8 ) << 24 ) | (( col.red >> 8 ) << 16 ) | (( col.green >> 8 ) << 8 )
        | ( col.blue >> 8 );
    QMap< unsigned int, XID >::ConstIterator it = solids.find( key );
    if( it != solids.end())
        return *it;
    if( solids.count() >= 64 )
        { // (colours of animations: don't keep them all)
        for( QMap< unsigned int, XID >::ConstIterator it2 = solids.begin();
             it2 != solids.end();
             ++it2 )
            XRenderFreePicture( dpy, *it2 );
        solids.clear();
        }
    Picture pic = XRenderCreateSolidFill( dpy, &col );
    solids[ key ] = pic;
    return pic;
    }

XID XRenderRenderer::alphaPicture( double alpha )
    {
    const int a = (int) floor( Compositing::clamp01( alpha ) * 255 + 0.5 );
    if( alphas[ a ] == None )
        {
        XRenderColor col = { 0, 0, 0, (unsigned short)( a * 0x101 ) };
        alphas[ a ] = XRenderCreateSolidFill( dpy, &col );
        }
    return alphas[ a ];
    }

XID XRenderRenderer::scratchMask( int w, int h )
    {
    if( scratch_picture == None || w > scratch_w || h > scratch_h )
        {
        if( scratch_picture != None )
            XRenderFreePicture( dpy, scratch_picture );
        if( scratch_pixmap != None )
            XFreePixmap( dpy, scratch_pixmap );
        scratch_w = QMAX( w, scratch_w );
        scratch_h = QMAX( h, scratch_h );
        scratch_pixmap = XCreatePixmap( dpy, DefaultRootWindow( dpy ), scratch_w, scratch_h, 8 );
        scratch_picture = XRenderCreatePicture( dpy, scratch_pixmap,
            XRenderFindStandardFormat( dpy, PictStandardA8 ), 0, NULL );
        }
    return scratch_picture;
    }

// the picture of tex sampled for the destination box at x0, y0 drawing
// src onto dst; integer translations need no transformation (src_x, src_y)
static void placeSource( XRTexture* tex, const RectF& src, const RectF& dst, int x0, int y0,
    Filter filter, Repeat repeat, int* sx_return, int* sy_return )
    {
    const double sx = src.w / dst.w;
    const double sy = src.h / dst.h;
    const double tx = src.x + ( x0 - dst.x ) * sx;
    const double ty = src.y + ( y0 - dst.y ) * sy;
    tex->setRepeat( repeat );
    if( sx == 1 && sy == 1 && tx == floor( tx ) && ty == floor( ty ))
        {
        tex->setIdentity();
        tex->setFilter( NearestFilter ); // (exact anyway)
        *sx_return = (int) tx;
        *sy_return = (int) ty;
        return;
        }
    tex->setFilter( filter );
    tex->setTransform( sx, sy, tx, ty );
    *sx_return = 0;
    *sy_return = 0;
    }

void XRenderRenderer::draw( const Draw& d )
    {
    if( target == NULL || d.dst.w <= 0 || d.dst.h <= 0 )
        return;
    if( d.texture != NULL && ( d.src.w <= 0 || d.src.h <= 0 ))
        return;
    QRect box = d.dst.bounds() & QRect( 0, 0, target->width(), target->height());
    if( clipped )
        box &= clip.boundingRect();
    if( box.isEmpty())
        return;
    applyClip();
    const int x0 = box.x();
    const int y0 = box.y();
    int op = d.blend == BlendSource ? PictOpSrc : d.blend == BlendAdd ? PictOpAdd : PictOpOver;
    Picture src;
    int src_x = 0, src_y = 0;
    double opacity = d.opacity;
    if( d.texture != NULL )
        {
        XRTexture* tex = static_cast< XRTexture* >( d.texture );
        src = tex->picture;
        placeSource( tex, d.src, d.dst, x0, y0, d.filter, d.repeat, &src_x, &src_y );
        }
    else
        {
        src = solidPicture( d.color, d.color_alpha * opacity );
        opacity = 1;
        }
    Picture mask = None;
    int mask_x = 0, mask_y = 0;
    if( d.mask != NULL )
        {
        XRTexture* m = static_cast< XRTexture* >( d.mask );
        placeSource( m, d.mask_src, d.dst, x0, y0, d.mask_filter, d.mask_repeat, &mask_x, &mask_y );
        mask = m->picture;
        if( opacity < 1 )
            { // the mask times the opacity, in a scratch picture
            Picture scratch = scratchMask( box.width(), box.height());
            XRenderComposite( dpy, PictOpSrc, alphaPicture( opacity ), mask, scratch, 0, 0, mask_x, mask_y,
                0, 0, box.width(), box.height());
            mask = scratch;
            mask_x = 0;
            mask_y = 0;
            }
        }
    else if( opacity < 1 )
        mask = alphaPicture( opacity );
    if( opacity <= 0 && op != PictOpSrc )
        return;
    XRenderComposite( dpy, op, src, mask, target->picture, src_x, src_y, mask_x, mask_y,
        x0, y0, box.width(), box.height());
    }

#else // HAVE_XRENDER

Renderer* XRenderRenderer::create( Display*, QString* error )
    {
    if( error != NULL )
        *error = i18n( "KWin was built without XRender support." );
    return NULL;
    }

XRenderRenderer::~XRenderRenderer() {}
QString XRenderRenderer::description() const { return QString::null; }
Texture* XRenderRenderer::textureFromPixmap( Pixmap, int, int, int, Visual* ) { return NULL; }
Texture* XRenderRenderer::textureFromImage( const QImage& ) { return NULL; }
Texture* XRenderRenderer::textureFromAlpha( const unsigned char*, int, int, int ) { return NULL; }
Texture* XRenderRenderer::createOffscreen( int, int, bool ) { return NULL; }
Texture* XRenderRenderer::createPresentBuffer( Window, int, int, Pixmap* ) { return NULL; }
void XRenderRenderer::acquireClientBuffers() {}
SyncPoint XRenderRenderer::finish() { return SyncPoint(); }
bool XRenderRenderer::setTarget( Texture* ) { return false; }
void XRenderRenderer::setClip( const QRegion& ) {}
void XRenderRenderer::clearClip() {}
void XRenderRenderer::draw( const Draw& ) {}

#endif // HAVE_XRENDER

} // namespace Render

} // namespace KWinInternal
