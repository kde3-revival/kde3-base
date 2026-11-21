/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2026 The KDE3 revival project

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

#ifndef KWIN_RENDER_XRENDER_H
#define KWIN_RENDER_XRENDER_H

#include "render.h"

#include <qmap.h>

#ifdef HAVE_XRENDER
#include <X11/extensions/Xrender.h>
#endif

namespace KWinInternal
{

namespace Render
{

/*
 The XRender backend: every texture is a Picture (on the window's pixmap,
 or on a pixmap of our own), every draw one XRenderComposite.  The X server
 does the rendering in request order (with glamor on the GPU), so the sync
 points are no-ops.  Clipping uses clip rectangles on the target picture.
*/
class XRenderRenderer : public Renderer
    {
    public:
        static Renderer* create( Display* dpy, QString* error );
        virtual ~XRenderRenderer();
        virtual Backend backend() const { return XRender; }
        virtual QString description() const;
        virtual bool isLost() const { return false; }
        virtual Texture* textureFromPixmap( Pixmap pixmap, int w, int h, int depth, Visual* visual );
        virtual Texture* textureFromImage( const QImage& image );
        virtual Texture* textureFromAlpha( const unsigned char* data, int w, int h, int stride );
        virtual Texture* createOffscreen( int w, int h, bool alpha );
        virtual Texture* createPresentBuffer( Window window, int w, int h, Pixmap* pixmap );
        virtual void acquireClientBuffers();
        virtual SyncPoint finish();
        virtual bool setTarget( Texture* target );
        virtual void setClip( const QRegion& clip );
        virtual void clearClip();
        virtual void draw( const Draw& d );
#ifdef HAVE_XRENDER
    private:
        XRenderRenderer( Display* dpy );
        XID solidPicture( const QColor& c, double alpha );
        XID alphaPicture( double alpha );
        XID scratchMask( int w, int h );
        void applyClip();
        class XRTexture* target;
        QRegion clip;
        bool clipped;
        bool clip_dirty;
        QMap< unsigned int, XID > solids; // premultiplied ARGB -> solid fill
        XID alphas[ 256 ];
        Pixmap scratch_pixmap;
        XID scratch_picture;
        int scratch_w, scratch_h;
        int render_major, render_minor;
        friend class XRTexture;
#endif
    };

} // namespace Render

} // namespace KWinInternal

#endif
