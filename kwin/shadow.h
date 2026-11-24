/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2026 The KDE3 revival project

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

#ifndef KWIN_SHADOW_H
#define KWIN_SHADOW_H

#include <qregion.h>
#include <qsize.h>
#include <qptrlist.h>
#include <qvaluevector.h>
#include <qcolor.h>

#include "compositing.h"

namespace KWinInternal
{

namespace Render
{
class Renderer;
class Texture;
}

/*
 Soft window shadows, GNOME-like: a Gaussian blur of the window's shape,
 computed once per corner pattern and size, drawn as nine slices around
 each window (see Render::Renderer::drawNineSlice()).

 Like mutter's MetaWindowShape, the shape of a frame (rounded corners of
 the decoration) is reduced to its corners: a small template of the four
 corner squares (C x C) joined by 2R + 1 pixels of straight edge is
 blurred with radius R; the corners of the result (2R + C square: from R
 outside the window to R + C inside) are the corner tiles, its middle
 column and row the edges.

 Any other shape (a balloon with a tail, an icon's mask, corners larger
 than 16 pixels) is blurred as a whole: one alpha texture of the window
 grown by R, at a fraction of the size for large windows (the blur is
 smooth: drawn scaled up with bilinear filtering it looks the same).
 Never the bounding rectangle's shadow: inside it, outside the shape, that
 is the dark middle of the shadow (a hard dark band around the shape).

 An ARGB window that asks for it (_KDE_NET_WM_SHADOW_FROM_ALPHA) has the
 shadow of its alpha channel: blurred like a shape, its coverage from the
 alpha of each pixel (anti-aliased edges, rounded corners, a tail),
 cached by the alpha values (drawAlpha()).
*/
class ShadowTiles
    {
    public:
        ~ShadowTiles();
        // top left, top, top right, right, bottom right, bottom, bottom left, left
        Render::Texture* tile[ 8 ];
        int radius;
        int corner; // C
        QValueVector< unsigned char > pattern; // the corner pattern (cache key)
        unsigned int used; // LRU stamp
    private:
        friend class ShadowCache;
        ShadowTiles();
    };

// the blurred shape of an irregular window (see ShadowCache)
class ShapeShadow
    {
    public:
        ~ShapeShadow();
        Render::Texture* texture; // ( w + 2R ) / scale x ( h + 2R ) / scale, rounded up
        int radius;
        int scale; // texture pixels per scale x scale window pixels
        QSize size;
        QRegion shape; // (cache key)
        QValueVector< unsigned char > alpha; // (cache key) a window's alpha channel; empty: shape
        unsigned int used; // LRU stamp
        unsigned int frame; // the frame it was last drawn in
        long long bytes; // what it holds (the texture)
    private:
        friend class ShadowCache;
        ShapeShadow();
    };

class ShadowCache
    {
    public:
        ShadowCache( Render::Renderer* renderer );
        ~ShadowCache();
        // the tiles for a window of size with the bounding shape (window
        // relative; a null region: a plain rectangle) and blur radius (of
        // its bounding rectangle when the shape is not a rectangle with cut
        // corners: draw() handles every shape); NULL when they can't be made
        const ShadowTiles* tiles( int radius, const QRegion& shape, const QSize& size );
        // the shadow of a window of size with the bounding shape (as for
        // tiles()) drawn with r: the window's rectangle is drawn at outer
        // grown by the radius (outerRect(), scaled by sx, sy for a window
        // drawn scaled), in c with alpha; the nine slices for rectangles
        // with cut corners, the blurred shape for any other shape; false
        // when the textures can't be made
        bool draw( Render::Renderer* r, int radius, const QRegion& shape, const QSize& size,
            const Compositing::RectF& outer, double sx, double sy, const QColor& c, double alpha );
        // the same for the shadow of a window's alpha channel (size w x h
        // values, row by row) instead of a shape
        bool drawAlpha( Render::Renderer* r, int radius, const QValueVector< unsigned char >& coverage,
            const QSize& size, const Compositing::RectF& outer, double sx, double sy, const QColor& c,
            double alpha );
        // where the nine slices go for a window at geometry
        static QRect outerRect( const QRect& geometry, int radius, int dx, int dy );
        void clear();
        // a frame begins (the blurred shapes drawn in the last frames are
        // kept whatever the memory budget)
        void newFrame();
        // how many shapes were blurred so far (for the tests)
        unsigned int shapeBuilds() const { return builds; }
        // the blurred coverage of a template (exposed for the tests), the
        // result radius larger on each side; sigma 0: radius / 2.5
        static QValueVector< unsigned char > blur( const QValueVector< unsigned char >& src, int w, int h, int radius,
            double sigma = 0 );
    private:
        static int cornerPattern( const QRegion& shape, const QSize& size, QValueVector< unsigned char >* pattern );
        ShadowTiles* make( int radius, int corner, const QValueVector< unsigned char >& pattern );
        const ShadowTiles* tiles( int radius, int corner, const QValueVector< unsigned char >& pattern );
        const ShapeShadow* shapeShadow( int radius, const QRegion& shape, const QSize& size,
            const QValueVector< unsigned char >& coverage );
        void evictShapes();
        ShapeShadow* makeShape( int radius, const QRegion& shape, const QSize& size,
            const QValueVector< unsigned char >& coverage );
        bool drawShape( Render::Renderer* r, const ShapeShadow* s, int radius, const QSize& size,
            const Compositing::RectF& outer, double sx, double sy, const QColor& c, double alpha );
        Render::Renderer* renderer;
        QPtrList< ShadowTiles > cache;
        QPtrList< ShapeShadow > shape_cache;
        unsigned int stamp;
        unsigned int frame;
        unsigned int builds;
        long long shape_bytes; // held by shape_cache
    };

} // namespace

#endif
