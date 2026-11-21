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

ShadowCache::ShadowCache( Render::Renderer* r )
    : renderer( r ), stamp( 0 )
    {
    cache.setAutoDelete( true );
    }

ShadowCache::~ShadowCache()
    {
    clear();
    }

void ShadowCache::clear()
    {
    cache.clear();
    }

QRect ShadowCache::outerRect( const QRect& geometry, int radius, int dx, int dy )
    {
    return QRect( geometry.x() + dx - radius, geometry.y() + dy - radius,
        geometry.width() + 2 * radius, geometry.height() + 2 * radius );
    }

// the corner size C and the ( 2C + 1 ) x ( 2C + 1 ) template of the shape
int ShadowCache::cornerPattern( const QRegion& shape, const QSize& size, QValueVector< unsigned char >* pattern )
    {
    const int w = size.width();
    const int h = size.height();
    int c = 0;
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
                { // not a frame with cut corners: the bounding rectangle
                c = 0;
                pattern->resize( 1 );
                (*pattern)[ 0 ] = 255;
                return 0;
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
QValueVector< unsigned char > ShadowCache::blur( const QValueVector< unsigned char >& src, int w, int h, int radius )
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
    const double sigma = radius / 2.5;
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

} // namespace
