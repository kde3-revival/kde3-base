/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2026 The KDE3 revival project

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

#ifndef KWIN_RENDER_H
#define KWIN_RENDER_H

/*
 The renderer interface of the compositor: the only code that talks to a
 GPU API.  The scene (compositor.cpp) and the effects draw through it, so
 that another backend (Vulkan, a wlroots render pass for the Wayland
 compositor) can be added without touching them.

 Backends: GLES 3 on EGL (render_gles.cpp: the buffers are DMA-BUFs shared
 with the X server through DRI3, zero copy) and XRender (render_xrender.cpp:
 Pictures on the same X pixmaps; the fallback without DRI3, e.g. Xvfb).

 The model is deliberately small:
   - textures: a window's pixmap, a DMA-BUF, an image from memory, an
     offscreen render target; every texture can be drawn from, offscreen
     ones and present buffers can be drawn into
   - draws: a texture (or a solid colour) from a source rectangle to a
     destination rectangle (scaling + translation, no rotation), with an
     opacity, a filter, a blending operator and optionally a mask (an
     alpha texture with its own source rectangle mapped onto the same
     destination); everything clipped to the current clip region
   - explicit sync points: acquireClientBuffers() before sampling what the
     X server rendered, finish() after drawing a frame that the X server
     shows (GLES does both implicitly or with an X fence, a Vulkan backend
     would wait on / signal semaphores or DRM syncobj timeline points)
 Colours are premultiplied inside, non-premultiplied in the API (QColor +
 alpha).  All coordinates are pixels, y down, origin top left.
*/

#include <qstring.h>
#include <qregion.h>
#include <qcolor.h>
#include <qimage.h>

#include <X11/Xlib.h>
#include <fixx11h.h>

#include "compositing.h"

namespace KWinInternal
{

namespace Render
{

using Compositing::RectF;

enum Filter { NearestFilter, BilinearFilter };
// NoRepeat: transparent outside the texture (soft edges when scaled with
// bilinear filtering); PadRepeat: the edge pixels extend; TileRepeat: the
// texture tiled (the root background)
enum Repeat { NoRepeat, PadRepeat, TileRepeat };
enum Blend { BlendOver, BlendSource, BlendAdd };

class Texture
    {
    public:
        virtual ~Texture();
        int width() const { return w; }
        int height() const { return h; }
        // false: opaque (the alpha channel, if any, is ignored)
        bool hasAlpha() const { return alpha; }
        // an alpha-only texture (masks, shadows)
        bool isAlphaOnly() const { return alpha_only; }
    protected:
        Texture( int w, int h, bool alpha, bool alpha_only = false );
        int w, h;
        bool alpha;
        bool alpha_only;
    };

// a buffer from another process (Wayland clients later, X pixmaps now)
struct DmaBuf
    {
    DmaBuf();
    int width, height;
    unsigned int fourcc; // DRM_FORMAT_*
    unsigned long long modifier; // DRM_FORMAT_MOD_INVALID: implicit
    int planes;
    int fd[ 4 ]; // not owned
    unsigned int offset[ 4 ];
    unsigned int stride[ 4 ];
    };

// the end of a frame's GPU work; fd -1: implicitly synchronized (the X
// server waits for the buffer's fences itself)
struct SyncPoint
    {
    SyncPoint() : fd( -1 ) {}
    int fd; // a sync_file, owned by the receiver
    };

struct Draw
    {
    Draw();
    Texture* texture; // NULL: the colour
    QColor color;
    double color_alpha;
    RectF src; // texture pixels
    RectF dst; // target pixels
    double opacity;
    Filter filter;
    Repeat repeat;
    Blend blend;
    Texture* mask; // its alpha multiplies the source; NULL: none
    RectF mask_src; // mask pixels, mapped onto dst
    Filter mask_filter;
    Repeat mask_repeat;
    };

class Renderer
    {
    public:
        enum Backend { Auto, GLES, XRender };
        // NULL on failure (with the reason in *error)
        static Renderer* create( Display* dpy, Backend backend, QString* error );
        static QString backendName( Backend backend );
        virtual ~Renderer();
        virtual Backend backend() const = 0;
        // e.g. "GLES 3.2: AMD Radeon RX 7600 (radeonsi, ...)"
        virtual QString description() const = 0;
        // true: the GPU context is gone (reset) or the renderer failed for
        // good; the compositor must give up and may create a new one
        virtual bool isLost() const = 0;

        // ---- textures (delete them like any object) ----
        // a view of an X pixmap, without copying where possible: its
        // contents follow the pixmap (see acquireClientBuffers()); the
        // pixmap stays the caller's and must outlive the texture
        virtual Texture* textureFromPixmap( Pixmap pixmap, int w, int h, int depth, Visual* visual ) = 0;
        // NULL when not supported
        virtual Texture* textureFromDmaBuf( const DmaBuf& buf, bool alpha );
        // a copy of a 32 bit QImage (Qt's non-premultiplied ARGB; hasAlphaBuffer())
        virtual Texture* textureFromImage( const QImage& image ) = 0;
        // an alpha-only texture from 8 bit values (stride bytes per row)
        virtual Texture* textureFromAlpha( const unsigned char* data, int w, int h, int stride ) = 0;
        // a texture that can be drawn into (cleared to transparent black)
        virtual Texture* createOffscreen( int w, int h, bool alpha ) = 0;
        // a buffer for presenting into window with Present: drawn into as a
        // target, *pixmap (owned by the texture) is what XPresentPixmap shows
        virtual Texture* createPresentBuffer( Window window, int w, int h, Pixmap* pixmap ) = 0;

        // ---- sync points ----
        // what the X server rendered into the pixmaps of textures until now
        // is complete before the following draws read them
        virtual void acquireClientBuffers() = 0;
        // submits all drawing; the X server may read the targets after
        // this (the returned sync point tells when it may)
        virtual SyncPoint finish() = 0;

        // ---- drawing ----
        // the target of the following draws (an offscreen texture or a
        // present buffer); resets the clip
        virtual bool setTarget( Texture* target ) = 0;
        // the draws only touch this region (target pixels)
        virtual void setClip( const QRegion& clip ) = 0;
        virtual void clearClip() = 0;
        virtual void draw( const Draw& d ) = 0;

        // helpers
        void drawTexture( Texture* tex, const RectF& src, const RectF& dst, double opacity = 1.0,
            Filter filter = NearestFilter, Blend blend = BlendOver, Repeat repeat = NoRepeat );
        // the whole texture at x, y
        void drawTexture( Texture* tex, int x, int y, double opacity = 1.0, Blend blend = BlendOver );
        void fill( const RectF& r, const QColor& c, double alpha, Blend blend = BlendOver );
        // c through the mask (alpha texture)
        void fillMasked( const RectF& r, const QColor& c, double alpha, Texture* mask, const RectF& mask_src,
            Filter filter = NearestFilter );
        /*
         Nine-slice drawing (shadows): tiles[ 0..7 ] are the top left,
         top, top right, right, bottom right, bottom, bottom left and left
         alpha tiles; the corners are drawn as they are, the edges
         stretched along the edge, the middle (inside) left empty.  outer
         is the rectangle the tiles cover.
        */
        void drawNineSlice( Texture* const tiles[ 8 ], const QRect& outer, const QColor& c, double alpha );
        // the same scaled (a window drawn scaled: the corners scaled by sx, sy)
        void drawNineSlice( Texture* const tiles[ 8 ], const RectF& outer, double sx, double sy, const QColor& c,
            double alpha );

    protected:
        Renderer( Display* dpy );
        Display* dpy;
    };

} // namespace Render

} // namespace KWinInternal

#endif
