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
 column and row the edges.  Windows whose shape is not a rectangle with
 cut corners get the shadow of their bounding rectangle.
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

class ShadowCache
    {
    public:
        ShadowCache( Render::Renderer* renderer );
        ~ShadowCache();
        // the tiles for a window of size with the bounding shape (window
        // relative; a null region: a plain rectangle) and blur radius;
        // NULL when they can't be made
        const ShadowTiles* tiles( int radius, const QRegion& shape, const QSize& size );
        // where the nine slices go for a window at geometry
        static QRect outerRect( const QRect& geometry, int radius, int dx, int dy );
        void clear();
        // the blurred coverage of a template (exposed for the tests)
        static QValueVector< unsigned char > blur( const QValueVector< unsigned char >& src, int w, int h, int radius );
    private:
        static int cornerPattern( const QRegion& shape, const QSize& size, QValueVector< unsigned char >* pattern );
        ShadowTiles* make( int radius, int corner, const QValueVector< unsigned char >& pattern );
        Render::Renderer* renderer;
        QPtrList< ShadowTiles > cache;
        unsigned int stamp;
    };

} // namespace

#endif
