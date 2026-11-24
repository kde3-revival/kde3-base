/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2026 The KDE3 revival project

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

#include "shadow.h"

#include "render.h"

#include <math.h>

namespace KWinInternal
{

// the largest corner cut out of a frame shape that still counts as a corner
static const int MaxCorner = 16;
// how many shadow tile sets are kept
static const unsigned int CacheSize = 32;
// how many blurred shapes are kept (e.g. the five masks of the bouncing
// launch feedback icon, a balloon)
static const unsigned int ShapeCacheSize = 16;
// the largest blurred shape texture (pixels); larger windows are blurred
// at a half, a quarter... of their size
static const int MaxShapeArea = 256 * 1024;

ShadowTiles::ShadowTiles()
    : radius( 0 ), corner( 0 ), used( 0 )
    {
    for( int i = 0;
         i < 8;
         ++i )
        tile[ i ] = NULL;
    }

ShadowTiles::~ShadowTiles()
    {
    for( int i = 0;
         i < 8;
         ++i )
        delete tile[ i ];
    }

ShapeShadow::ShapeShadow()
    : texture( NULL ), radius( 0 ), scale( 1 ), used( 0 )
    {
    }

ShapeShadow::~ShapeShadow()
    {
    delete texture;
    }

ShadowCache::ShadowCache( Render::Renderer* r )
    : renderer( r ), stamp( 0 )
    {
    cache.setAutoDelete( true );
    shape_cache.setAutoDelete( true );
    }

ShadowCache::~ShadowCache()
    {
    clear();
    }

void ShadowCache::clear()
    {
    cache.clear();
    shape_cache.clear();
    }

QRect ShadowCache::outerRect( const QRect& geometry, int radius, int dx, int dy )
    {
    return QRect( geometry.x() + dx - radius, geometry.y() + dy - radius,
        geometry.width() + 2 * radius, geometry.height() + 2 * radius );
    }

// the corner size C and the ( 2C + 1 ) x ( 2C + 1 ) template of the shape;
// -1 (and the pattern of a rectangle) when it is not a rectangle with cut
// corners
int ShadowCache::cornerPattern( const QRegion& shape, const QSize& size, QValueVector< unsigned char >* pattern )
    {
    const int w = size.width();
    const int h = size.height();
    int c = 0;
    if( !shape.isNull() && ( w <= 2 || h <= 2 ) && shape != QRegion( 0, 0, w, h ))
        { // (too thin for corners: blurred as a whole)
        pattern->resize( 1 );
        (*pattern)[ 0 ] = 255;
        return -1;
        }
    if( !shape.isNull() && w > 2 && h > 2 )
        {
        const int limit = QMIN( MaxCorner, QMIN( w, h ) / 2 - 1 );
        QMemArray< QRect > missing = ( QRegion( 0, 0, w, h ) - shape ).rects();
        for( unsigned int i = 0;
             i < missing.size();
             ++i )
            {
            const QRect& r = missing[ i ];
            const bool left = r.left() < limit;
            const bool right = r.right() >= w - limit;
            const bool top = r.top() < limit;
            const bool bottom = r.bottom() >= h - limit;
            int extent = -1;
            if( left && top && r.right() < limit && r.bottom() < limit )
                extent = QMAX( r.right() + 1, r.bottom() + 1 );
            else if( right && top && r.left() >= w - limit && r.bottom() < limit )
                extent = QMAX( w - r.left(), r.bottom() + 1 );
            else if( left && bottom && r.right() < limit && r.top() >= h - limit )
                extent = QMAX( r.right() + 1, h - r.top());
            else if( right && bottom && r.left() >= w - limit && r.top() >= h - limit )
                extent = QMAX( w - r.left(), h - r.top());
            if( extent < 0 )
                { // not a frame with cut corners
                pattern->resize( 1 );
                (*pattern)[ 0 ] = 255;
                return -1;
                }
            c = QMAX( c, extent );
            }
        }
    const int n = 2 * c + 1;
    pattern->resize( n * n );
    for( int j = 0;
         j < n;
         ++j )
        {
        const int y = j < c ? j : j > c ? h - ( n - j ) : h / 2;
        for( int i = 0;
             i < n;
             ++i )
            {
            const int x = i < c ? i : i > c ? w - ( n - i ) : w / 2;
            (*pattern)[ j * n + i ] = ( c == 0 || shape.contains( QPoint( x, y ))) ? 255 : 0;
            }
        }
    return c;
    }

// a separable Gaussian blur, the result radius larger on each side
QValueVector< unsigned char > ShadowCache::blur( const QValueVector< unsigned char >& src, int w, int h, int radius,
    double sigma )
    {
    const int ow = w + 2 * radius;
    const int oh = h + 2 * radius;
    QValueVector< unsigned char > out( ow * oh, 0 );
    if( radius <= 0 )
        {
        for( int i = 0;
             i < w * h;
             ++i )
            out[ i ] = src[ i ];
        return out;
        }
    // most of the Gaussian inside the radius: sigma = radius / 2.5
    if( sigma <= 0 )
        sigma = radius / 2.5;
    QValueVector< double > kernel( 2 * radius + 1 );
    double sum = 0;
    for( int i = -radius;
         i <= radius;
         ++i )
        {
        kernel[ i + radius ] = exp( -( i * i ) / ( 2 * sigma * sigma ));
        sum += kernel[ i + radius ];
        }
    for( int i = 0;
         i <= 2 * radius;
         ++i )
        kernel[ i ] /= sum;
    // horizontal pass into the padded width, then vertical
    QValueVector< double > tmp( ow * h, 0.0 );
    for( int y = 0;
         y < h;
         ++y )
        for( int x = 0;
             x < ow;
             ++x )
            {
            double v = 0;
            for( int k = -radius;
                 k <= radius;
                 ++k )
                {
                const int sx = x - radius + k;
                if( sx >= 0 && sx < w )
                    v += src[ y * w + sx ] * kernel[ k + radius ];
                }
            tmp[ y * ow + x ] = v;
            }
    for( int y = 0;
         y < oh;
         ++y )
        for( int x = 0;
             x < ow;
             ++x )
            {
            double v = 0;
            for( int k = -radius;
                 k <= radius;
                 ++k )
                {
                const int sy = y - radius + k;
                if( sy >= 0 && sy < h )
                    v += tmp[ sy * ow + x ] * kernel[ k + radius ];
                }
            out[ y * ow + x ] = (unsigned char) QMIN( 255.0, floor( v + 0.5 ));
            }
    return out;
    }

static bool samePattern( const QValueVector< unsigned char >& a, const QValueVector< unsigned char >& b )
    {
    if( a.size() != b.size())
        return false;
    for( unsigned int i = 0;
         i < a.size();
         ++i )
        if( a[ i ] != b[ i ] )
            return false;
    return true;
    }

ShadowTiles* ShadowCache::make( int radius, int c, const QValueVector< unsigned char >& pattern )
    {
    // the template: the corners of the pattern, its middle row and column
    // stretched to 2R + 1, so that the middle of the blurred template is
    // the profile of a straight edge (nothing of the corners reaches it)
    const int p = 2 * c + 1; // the pattern size
    const int n = 2 * c + 2 * radius + 1; // the template size
    QValueVector< unsigned char > templ( n * n );
    for( int j = 0;
         j < n;
         ++j )
        {
        const int pj = j < c ? j : j >= n - c ? j - ( n - p ) : c;
        for( int i = 0;
             i < n;
             ++i )
            {
            const int pi = i < c ? i : i >= n - c ? i - ( n - p ) : c;
            templ[ j * n + i ] = pattern[ pj * p + pi ];
            }
        }
    const int s = n + 2 * radius; // the blurred size, 2k + 1
    const int k = 2 * radius + c; // the corner tile size
    QValueVector< unsigned char > blurred = blur( templ, n, n, radius );
    ShadowTiles* t = new ShadowTiles;
    t->radius = radius;
    t->corner = c;
    t->pattern = pattern;
    const unsigned char* data = &blurred[ 0 ];
    // corners
    t->tile[ 0 ] = renderer->textureFromAlpha( data, k, k, s );
    t->tile[ 2 ] = renderer->textureFromAlpha( data + ( s - k ), k, k, s );
    t->tile[ 4 ] = renderer->textureFromAlpha( data + ( s - k ) * s + ( s - k ), k, k, s );
    t->tile[ 6 ] = renderer->textureFromAlpha( data + ( s - k ) * s, k, k, s );
    // edges: the middle column (top, bottom) and row (left, right)
    QValueVector< unsigned char > column( k );
    for( int y = 0;
         y < k;
         ++y )
        column[ y ] = blurred[ y * s + k ];
    t->tile[ 1 ] = renderer->textureFromAlpha( &column[ 0 ], 1, k, 1 );
    for( int y = 0;
         y < k;
         ++y )
        column[ y ] = blurred[( s - k + y ) * s + k ];
    t->tile[ 5 ] = renderer->textureFromAlpha( &column[ 0 ], 1, k, 1 );
    t->tile[ 3 ] = renderer->textureFromAlpha( data + k * s + ( s - k ), k, 1, s );
    t->tile[ 7 ] = renderer->textureFromAlpha( data + k * s, k, 1, s );
    for( int i = 0;
         i < 8;
         ++i )
        if( t->tile[ i ] == NULL )
            {
            delete t;
            return NULL;
            }
    return t;
    }

const ShadowTiles* ShadowCache::tiles( int radius, const QRegion& shape, const QSize& size )
    {
    if( radius <= 0 || size.isEmpty())
        return NULL;
    QValueVector< unsigned char > pattern;
    const int c = cornerPattern( shape, size, &pattern );
    return tiles( radius, QMAX( c, 0 ), pattern );
    }

const ShadowTiles* ShadowCache::tiles( int radius, int c, const QValueVector< unsigned char >& pattern )
    {
    ++stamp;
    for( QPtrListIterator< ShadowTiles > it( cache );
         it.current() != NULL;
         ++it )
        if( it.current()->radius == radius && it.current()->corner == c
            && samePattern( it.current()->pattern, pattern ))
            {
            it.current()->used = stamp;
            return it.current();
            }
    ShadowTiles* t = make( radius, c, pattern );
    if( t == NULL )
        return NULL;
    t->used = stamp;
    if( cache.count() >= CacheSize )
        { // the least recently used one goes
        ShadowTiles* oldest = NULL;
        for( QPtrListIterator< ShadowTiles > it( cache );
             it.current() != NULL;
             ++it )
            if( oldest == NULL || it.current()->used < oldest->used )
                oldest = it.current();
        cache.removeRef( oldest );
        }
    cache.append( t );
    return t;
    }

/*
 The blurred shape: the window's coverage on a canvas grown by R on each
 side (at 1 / scale of the size: the average of scale x scale pixels),
 blurred like the tiles (the same Gaussian, so that straight edges get
 the same profile as those of the nine slices).
*/
ShapeShadow* ShadowCache::makeShape( int radius, const QRegion& shape, const QSize& size )
    {
    ShapeShadow* s = new ShapeShadow;
    s->radius = radius;
    s->size = size;
    s->shape = shape;
    // (64 bits: a window up to 32767 x 32767, any radius)
    const long long cw = (long long) size.width() + 2LL * radius;
    const long long ch = (long long) size.height() + 2LL * radius;
    const long long max_side = renderer->maxTextureSize();
    long long f = 1;
    while(((( cw + f - 1 ) / f ) * (( ch + f - 1 ) / f ) > MaxShapeArea && radius / ( 2 * f ) >= 1 )
        || ( cw + f - 1 ) / f > max_side || ( ch + f - 1 ) / f > max_side )
        {
        if( f >= 1024 )
            return s; // (no texture: no shadow, and not tried again)
        f *= 2;
        }
    const long long tw_l = ( cw + f - 1 ) / f;
    const long long th_l = ( ch + f - 1 ) / f;
    if( tw_l * th_l > 4LL * MaxShapeArea || cw * ch > 16LL * 1024 * 1024 )
        return s; // (a huge window with a tiny radius; or more pixels to visit than sane)
    s->scale = (int) f;
    const int tw = (int) tw_l;
    const int th = (int) th_l;
    QValueVector< int > sum( tw * th, 0 );
    QMemArray< QRect > rects = ( shape & QRegion( 0, 0, size.width(), size.height())).rects();
    for( unsigned int i = 0;
         i < rects.size();
         ++i )
        {
        const QRect& r = rects[ i ];
        for( int y = r.top() + radius;
             y <= r.bottom() + radius;
             ++y )
            {
            int* row = &sum[ ( y / s->scale ) * tw ];
            if( s->scale == 1 )
                {
                for( int x = r.left() + radius;
                     x <= r.right() + radius;
                     ++x )
                    row[ x ] = 1;
                }
            else
                for( int x = r.left() + radius;
                     x <= r.right() + radius;
                     ++x )
                    ++row[ x / s->scale ];
            }
        }
    QValueVector< unsigned char > canvas( tw * th );
    for( int i = 0;
         i < tw * th;
         ++i )
        canvas[ i ] = (unsigned char) (( sum[ i ] * 255LL + f * f / 2 ) / ( f * f ));
    // (the canvas has the room for the blur: its middle part is the result)
    const int rr = radius / s->scale;
    QValueVector< unsigned char > blurred = blur( canvas, tw, th, rr, radius / 2.5 / s->scale );
    const int bw = tw + 2 * rr;
    // (NULL when the upload failed: kept so, no shadow)
    s->texture = renderer->textureFromAlpha( &blurred[ rr * bw + rr ], tw, th, bw );
    return s;
    }

const ShapeShadow* ShadowCache::shapeShadow( int radius, const QRegion& shape, const QSize& size )
    {
    ++stamp;
    for( QPtrListIterator< ShapeShadow > it( shape_cache );
         it.current() != NULL;
         ++it )
        if( it.current()->radius == radius && it.current()->size == size && it.current()->shape == shape )
            {
            it.current()->used = stamp;
            return it.current();
            }
    ShapeShadow* s = makeShape( radius, shape, size );
    if( s == NULL )
        return NULL;
    s->used = stamp;
    if( shape_cache.count() >= ShapeCacheSize )
        {
        ShapeShadow* oldest = NULL;
        for( QPtrListIterator< ShapeShadow > it( shape_cache );
             it.current() != NULL;
             ++it )
            if( oldest == NULL || it.current()->used < oldest->used )
                oldest = it.current();
        shape_cache.removeRef( oldest );
        }
    shape_cache.append( s );
    return s;
    }

bool ShadowCache::draw( Render::Renderer* r, int radius, const QRegion& shape, const QSize& size,
    const Compositing::RectF& outer, double sx, double sy, const QColor& c, double alpha )
    {
    if( radius <= 0 || size.isEmpty())
        return false;
    QValueVector< unsigned char > pattern;
    const int corner = cornerPattern( shape, size, &pattern );
    if( corner >= 0 )
        {
        const ShadowTiles* t = tiles( radius, corner, pattern );
        if( t == NULL )
            return false;
        r->drawNineSlice( t->tile, outer, sx, sy, c, alpha );
        return true;
        }
    if( shape.isEmpty())
        return false; // (an empty bounding shape: nothing shows)
    const ShapeShadow* s = shapeShadow( radius, shape, size );
    if( s == NULL || s->texture == NULL )
        return false;
    const double f = s->scale;
    r->fillMasked( outer, c, alpha, s->texture,
        Compositing::RectF( 0, 0, ( size.width() + 2 * radius ) / f, ( size.height() + 2 * radius ) / f ),
        ( f > 1 || sx != 1 || sy != 1 ) ? Render::BilinearFilter : Render::NearestFilter );
    return true;
    }

} // namespace
