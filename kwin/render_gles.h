/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2026 The KDE3 revival project

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

#ifndef KWIN_RENDER_GLES_H
#define KWIN_RENDER_GLES_H

#include "render.h"

namespace KWinInternal
{

namespace Render
{

class GlesPrivate;

/*
 The GLES 3 backend: EGL on GBM, on the device the X server renders with
 (DRI3Open), without any window surface.  The buffers are shared with the
 X server as DMA-BUFs: window pixmaps are imported (DRI3 BuffersFromPixmap
 -> EGLImage -> texture, zero copy), the present buffers are allocated here
 (GBM, with the modifiers the server can scan out for the overlay window, so
 that Present can flip them) and given to the server (DRI3
 PixmapFromBuffers).

 Sync: acquireClientBuffers() triggers an X Sync fence backed by an
 xshmfence and waits for it (glamor flushes its rendering before it
 triggers a fence; the GPU then orders the shared buffers implicitly);
 finish() flushes our rendering, whose implicit fences the X server's copy
 or the page flip wait for.
*/
class GlesRenderer : public Renderer
    {
    public:
        static Renderer* create( Display* dpy, QString* error );
        virtual ~GlesRenderer();
        virtual Backend backend() const { return GLES; }
        virtual QString description() const;
        virtual bool isLost() const;
        virtual Texture* textureFromPixmap( Pixmap pixmap, int w, int h, int depth, Visual* visual );
        virtual Texture* textureFromDmaBuf( const DmaBuf& buf, bool alpha );
        virtual Texture* textureFromImage( const QImage& image );
        virtual Texture* textureFromAlpha( const unsigned char* data, int w, int h, int stride );
        virtual int maxTextureSize() const;
        virtual Texture* createOffscreen( int w, int h, bool alpha );
        virtual Texture* createPresentBuffer( Window window, int w, int h, Pixmap* pixmap );
        virtual void acquireClientBuffers();
        virtual SyncPoint finish();
        virtual bool setTarget( Texture* target );
        virtual void setClip( const QRegion& clip );
        virtual void clearClip();
        virtual void draw( const Draw& d );
    private:
        GlesRenderer( Display* dpy );
        bool init( QString* error );
        GlesPrivate* d;
    };

} // namespace Render

} // namespace KWinInternal

#endif
