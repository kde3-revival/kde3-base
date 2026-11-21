/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2026 The KDE3 revival project

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

#include <config.h>

#include "render.h"
#include "render_xrender.h"
#ifdef KWIN_HAVE_GLES
#include "render_gles.h"
#endif

#include <klocale.h>

#include <math.h>

namespace KWinInternal
{

namespace Render
{

Texture::Texture( int w_, int h_, bool alpha_, bool alpha_only_ )
    : w( w_ ), h( h_ ), alpha( alpha_ || alpha_only_ ), alpha_only( alpha_only_ )
    {
    }

Texture::~Texture()
    {
    }

DmaBuf::DmaBuf()
    : width( 0 ), height( 0 ), fourcc( 0 ), modifier( 0x00ffffffffffffffULL ), planes( 0 )
    {
    for( int i = 0;
         i < 4;
         ++i )
        {
        fd[ i ] = -1;
        offset[ i ] = 0;
        stride[ i ] = 0;
        }
    }

Draw::Draw()
    : texture( NULL ), color( Qt::black ), color_alpha( 1.0 ), opacity( 1.0 ), filter( NearestFilter ),
      repeat( NoRepeat ), blend( BlendOver ), mask( NULL ), mask_filter( NearestFilter ),
      mask_repeat( NoRepeat )
    {
    }

Renderer::Renderer( Display* d )
    : dpy( d )
    {
    }

Renderer::~Renderer()
    {
    }

QString Renderer::backendName( Backend backend )
    {
    switch( backend )
        {
        case GLES:
            return "gles";
        case XRender:
            return "xrender";
        case Auto:
            break;
        }
    return "auto";
    }

Renderer* Renderer::create( Display* dpy, Backend backend, QString* error )
    {
    QString gles_error;
#ifdef KWIN_HAVE_GLES
    if( backend == Auto || backend == GLES )
        {
        Renderer* r = GlesRenderer::create( dpy, &gles_error );
        if( r != NULL || backend == GLES )
            {
            if( r == NULL && error != NULL )
                *error = gles_error;
            return r;
            }
        }
#else
    if( backend == GLES )
        {
        if( error != NULL )
            *error = i18n( "KWin was built without OpenGL ES support." );
        return NULL;
        }
#endif
    Renderer* r = XRenderRenderer::create( dpy, error );
    if( r == NULL && error != NULL && !gles_error.isEmpty())
        *error = gles_error + " " + *error;
    return r;
    }

Texture* Renderer::textureFromDmaBuf( const DmaBuf&, bool )
    {
    return NULL;
    }

void Renderer::drawTexture( Texture* tex, const RectF& src, const RectF& dst, double opacity,
    Filter filter, Blend blend, Repeat repeat )
    {
    Draw d;
    d.texture = tex;
    d.src = src;
    d.dst = dst;
    d.opacity = opacity;
    d.filter = filter;
    d.blend = blend;
    d.repeat = repeat;
    draw( d );
    }

void Renderer::drawTexture( Texture* tex, int x, int y, double opacity, Blend blend )
    {
    drawTexture( tex, RectF( 0, 0, tex->width(), tex->height()),
        RectF( x, y, tex->width(), tex->height()), opacity, NearestFilter, blend );
    }

void Renderer::fill( const RectF& r, const QColor& c, double alpha, Blend blend )
    {
    Draw d;
    d.color = c;
    d.color_alpha = alpha;
    d.dst = r;
    d.blend = blend;
    draw( d );
    }

void Renderer::fillMasked( const RectF& r, const QColor& c, double alpha, Texture* mask, const RectF& mask_src,
    Filter filter )
    {
    Draw d;
    d.color = c;
    d.color_alpha = alpha;
    d.dst = r;
    d.mask = mask;
    d.mask_src = mask_src;
    d.mask_filter = filter;
    d.mask_repeat = PadRepeat;
    draw( d );
    }

void Renderer::drawNineSlice( Texture* const tiles[ 8 ], const QRect& outer, const QColor& c, double alpha )
    {
    drawNineSlice( tiles, RectF( outer ), 1.0, 1.0, c, alpha );
    }

void Renderer::drawNineSlice( Texture* const tiles[ 8 ], const RectF& outer, double sx, double sy, const QColor& c,
    double alpha )
    {
    if( outer.w <= 0 || outer.h <= 0 || sx <= 0 || sy <= 0 )
        return;
    // the corner sizes from the corner tiles, scaled
    const double left = tiles[ 0 ]->width() * sx;
    const double top = tiles[ 0 ]->height() * sy;
    const double right = tiles[ 2 ]->width() * sx;
    const double bottom = tiles[ 6 ]->height() * sy;
    const double x0 = outer.x;
    const double y0 = outer.y;
    const double x1 = outer.x + outer.w;
    const double y1 = outer.y + outer.h;
    // too small for the corners: they would overlap (and double the alpha)
    const double xm = x0 + QMIN( left, outer.w * left / ( left + right ));
    const double ym = y0 + QMIN( top, outer.h * top / ( top + bottom ));
    const double xr = QMAX( xm, x1 - right );
    const double yb = QMAX( ym, y1 - bottom );
    const bool exact = sx == 1 && sy == 1 && x0 == floor( x0 ) && y0 == floor( y0 );
    struct Cell
        {
        int tile;
        double x, y, w, h; // destination
        bool stretch_x, stretch_y;
        bool from_right, from_bottom; // a cut corner shows its outer part
        } cells[ 8 ] =
        {
        { 0, x0, y0, xm - x0, ym - y0, false, false, false, false },
        { 1, xm, y0, xr - xm, ym - y0, true, false, false, false },
        { 2, xr, y0, x1 - xr, ym - y0, false, false, true, false },
        { 3, xr, ym, x1 - xr, yb - ym, false, true, true, false },
        { 4, xr, yb, x1 - xr, y1 - yb, false, false, true, true },
        { 5, xm, yb, xr - xm, y1 - yb, true, false, false, true },
        { 6, x0, yb, xm - x0, y1 - yb, false, false, false, true },
        { 7, x0, ym, xm - x0, yb - ym, false, true, false, false }
        };
    for( int i = 0;
         i < 8;
         ++i )
        {
        const Cell& cell = cells[ i ];
        if( cell.w <= 0 || cell.h <= 0 )
            continue;
        Texture* t = tiles[ cell.tile ];
        // the cell in tile pixels (the edges stretched along their edge)
        const double tw = cell.stretch_x ? t->width() : cell.w / sx;
        const double th = cell.stretch_y ? t->height() : cell.h / sy;
        RectF src( cell.from_right && !cell.stretch_x ? t->width() - tw : 0,
            cell.from_bottom && !cell.stretch_y ? t->height() - th : 0, tw, th );
        fillMasked( RectF( cell.x, cell.y, cell.w, cell.h ), c, alpha, t, src,
            ( cell.stretch_x || cell.stretch_y || !exact ) ? BilinearFilter : NearestFilter );
        }
    }

} // namespace Render

} // namespace KWinInternal
