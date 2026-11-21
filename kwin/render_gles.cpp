/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2026 The KDE3 revival project

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

#include <config.h>

#include "render_gles.h"

#include <qmemarray.h>
#include <qvaluevector.h>
#include <kdebug.h>
#include <klocale.h>

#include <X11/Xlib-xcb.h>
#include <xcb/xcb.h>
#include <xcb/dri3.h>
#include <xcb/sync.h>
extern "C"
{
#include <X11/xshmfence.h>
}

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <gbm.h>
#include <drm_fourcc.h>

#include <fixx11h.h>

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace KWinInternal
{

namespace Render
{

/*
 Vertices are in target pixels, texture coordinates in texture pixels
 (normalized in the vertex shader).  The targets are textures (FBOs), never
 a window: their first row in memory is the X top row, and GL writes window
 y = 0 into the first row, so no y flip anywhere.

 NoRepeat (transparent outside) is emulated in the fragment shader with
 clamp-to-edge sampling times the coverage of the texture, exactly what
 bilinear filtering with a transparent border gives.
*/
static const char vertex_source[] =
    "#version 300 es\n"
    "in vec2 pos;\n"
    "in vec2 tc;\n"
    "in vec2 mc;\n"
    "uniform vec2 target_size;\n"
    "uniform vec2 tex_size;\n"
    "uniform vec2 mask_size;\n"
    "out vec2 v_tc;\n"
    "out vec2 v_mc;\n"
    "void main()\n"
    "{\n"
    "    v_tc = tc / tex_size;\n"
    "    v_mc = mc / mask_size;\n"
    "    gl_Position = vec4( pos / target_size * 2.0 - 1.0, 0.0, 1.0 );\n"
    "}\n";

static const char fragment_source[] =
    "#version 300 es\n"
    "precision highp float;\n"
    "uniform sampler2D tex;\n"
    "uniform sampler2D mask;\n"
    "uniform vec4 color;\n" // premultiplied, without a texture
    "uniform float opacity;\n"
    "uniform int use_tex;\n" // 0 colour, 1 texture
    "uniform int tex_opaque;\n"
    "uniform int use_mask;\n"
    "uniform int tex_edge;\n" // NoRepeat: 0 off, 1 nearest, 2 bilinear
    "uniform int mask_edge;\n"
    "uniform vec2 tex_size;\n"
    "uniform vec2 mask_size;\n"
    "in vec2 v_tc;\n"
    "in vec2 v_mc;\n"
    "out vec4 frag;\n"
    "float coverage( vec2 t, vec2 size, int mode )\n"
    "{\n"
    "    vec2 p = t * size;\n"
    "    if( mode == 1 )\n"
    "        return ( p.x >= 0.0 && p.y >= 0.0 && p.x < size.x && p.y < size.y ) ? 1.0 : 0.0;\n"
    "    vec2 c = clamp( min( p, size - p ) + 0.5, 0.0, 1.0 );\n"
    "    return c.x * c.y;\n"
    "}\n"
    "void main()\n"
    "{\n"
    "    vec4 c = color;\n"
    "    if( use_tex != 0 )\n"
    "        {\n"
    "        c = texture( tex, v_tc );\n"
    "        if( tex_opaque != 0 )\n"
    "            c.a = 1.0;\n"
    "        if( tex_edge != 0 )\n"
    "            c *= coverage( v_tc, tex_size, tex_edge );\n"
    "        }\n"
    "    c *= opacity;\n"
    "    if( use_mask != 0 )\n"
    "        {\n"
    "        float m = texture( mask, v_mc ).a;\n"
    "        if( mask_edge != 0 )\n"
    "            m *= coverage( v_mc, mask_size, mask_edge );\n"
    "        c *= m;\n"
    "        }\n"
    "    frag = c;\n"
    "}\n";

class GLTexture : public Texture
    {
    public:
        GLTexture( GlesPrivate* p, int w, int h, bool alpha, bool alpha_only );
        virtual ~GLTexture();
        GlesPrivate* priv;
        GLuint tex;
        GLuint fbo;
        EGLImageKHR image;
        Pixmap pixmap; // a present buffer's
        struct gbm_bo* bo;
        int filter; // the texture parameters set last (-1 unknown)
        bool tiled; // GL_REPEAT
    };

class GlesPrivate
    {
    public:
        GlesPrivate( Display* d );
        ~GlesPrivate();
        Display* dpy;
        xcb_connection_t* xcb;
        int dri3_major, dri3_minor;
        int drm_fd;
        struct gbm_device* gbm;
        EGLDisplay egl;
        EGLContext ctx;
        bool modifiers; // EGL_EXT_image_dma_buf_import_modifiers
        bool robust;
        bool lost;
        QString renderer;
        QString version;
        PFNEGLCREATEIMAGEKHRPROC createImage;
        PFNEGLDESTROYIMAGEKHRPROC destroyImage;
        PFNGLEGLIMAGETARGETTEXTURE2DOESPROC imageTargetTexture;
        PFNGLGETGRAPHICSRESETSTATUSEXTPROC resetStatus;
        PFNEGLQUERYDMABUFMODIFIERSEXTPROC queryModifiers;
        GLuint program;
        GLuint vao;
        GLuint vbo;
        GLint u_target_size, u_tex_size, u_mask_size, u_color, u_opacity, u_use_tex, u_tex_opaque,
            u_use_mask, u_tex_edge, u_mask_edge, u_tex, u_mask;
        // the xshmfence of acquireClientBuffers()
        struct xshmfence* shm_fence;
        xcb_sync_fence_t x_fence;
        bool use_fence;
        GLTexture* target;
        QRegion clip;
        bool clipped;
        // the batch of draws with the same state (see flush())
        struct BatchState
            {
            BatchState() : tex( NULL ), mask( NULL ), filter( 0 ), mask_filter( 0 ), repeat( 0 ), mask_repeat( 0 ),
                blend( 0 ), opacity( 1 ) { color[ 0 ] = color[ 1 ] = color[ 2 ] = color[ 3 ] = 0; }
            bool operator==( const BatchState& o ) const
                {
                return tex == o.tex && mask == o.mask && filter == o.filter && mask_filter == o.mask_filter
                    && repeat == o.repeat && mask_repeat == o.mask_repeat && blend == o.blend && opacity == o.opacity
                    && color[ 0 ] == o.color[ 0 ] && color[ 1 ] == o.color[ 1 ] && color[ 2 ] == o.color[ 2 ]
                    && color[ 3 ] == o.color[ 3 ];
                }
            GLTexture* tex;
            GLTexture* mask;
            int filter, mask_filter, repeat, mask_repeat, blend;
            float opacity;
            float color[ 4 ];
            };
        BatchState state;
        QMemArray< GLfloat > batch;
        void flush();
        void setWrap( GLTexture* t, bool tile );
        bool importDmaBuf( GLTexture* t, const DmaBuf& buf );
        bool makeFramebuffer( GLTexture* t );
        void setFilter( GLTexture* t, Filter f );
        void checkLost();
    };

GlesPrivate::GlesPrivate( Display* d )
    : dpy( d ), xcb( NULL ), dri3_major( 0 ), dri3_minor( 0 ), drm_fd( -1 ), gbm( NULL ), egl( EGL_NO_DISPLAY ),
      ctx( EGL_NO_CONTEXT ), modifiers( false ), robust( false ), lost( false ), createImage( NULL ),
      destroyImage( NULL ), imageTargetTexture( NULL ), resetStatus( NULL ), queryModifiers( NULL ),
      program( 0 ), vao( 0 ), vbo( 0 ), shm_fence( NULL ), x_fence( 0 ), use_fence( true ), target( NULL ),
      clipped( false )
    {
    }

GlesPrivate::~GlesPrivate()
    {
    if( ctx != EGL_NO_CONTEXT )
        {
        if( program != 0 )
            glDeleteProgram( program );
        if( vbo != 0 )
            glDeleteBuffers( 1, &vbo );
        if( vao != 0 )
            glDeleteVertexArrays( 1, &vao );
        eglMakeCurrent( egl, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
        eglDestroyContext( egl, ctx );
        }
    if( egl != EGL_NO_DISPLAY )
        eglTerminate( egl );
    if( gbm != NULL )
        gbm_device_destroy( gbm );
    if( drm_fd >= 0 )
        close( drm_fd );
    if( x_fence != 0 )
        xcb_sync_destroy_fence( xcb, x_fence );
    if( shm_fence != NULL )
        xshmfence_unmap_shm( shm_fence );
    }

GLTexture::GLTexture( GlesPrivate* p, int w, int h, bool alpha, bool alpha_only )
    : Texture( w, h, alpha, alpha_only ), priv( p ), tex( 0 ), fbo( 0 ), image( EGL_NO_IMAGE_KHR ),
      pixmap( None ), bo( NULL ), filter( -1 ), tiled( false )
    {
    glGenTextures( 1, &tex );
    glBindTexture( GL_TEXTURE_2D, tex );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    }

GLTexture::~GLTexture()
    {
    priv->flush(); // (it may be in the batch)
    if( priv->target == this )
        priv->target = NULL;
    if( fbo != 0 )
        glDeleteFramebuffers( 1, &fbo );
    glDeleteTextures( 1, &tex );
    if( image != EGL_NO_IMAGE_KHR )
        priv->destroyImage( priv->egl, image );
    if( pixmap != None )
        XFreePixmap( priv->dpy, pixmap ); // (the server keeps the buffer while it shows it)
    if( bo != NULL )
        gbm_bo_destroy( bo );
    }

void GlesPrivate::setFilter( GLTexture* t, Filter f )
    {
    if( t->filter == f )
        return;
    t->filter = f;
    glBindTexture( GL_TEXTURE_2D, t->tex );
    const GLint mode = f == BilinearFilter ? GL_LINEAR : GL_NEAREST;
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mode );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mode );
    }

bool GlesPrivate::importDmaBuf( GLTexture* t, const DmaBuf& buf )
    {
    static const EGLint fd_attr[ 4 ] =
        { EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE1_FD_EXT, EGL_DMA_BUF_PLANE2_FD_EXT, EGL_DMA_BUF_PLANE3_FD_EXT };
    static const EGLint offset_attr[ 4 ] =
        { EGL_DMA_BUF_PLANE0_OFFSET_EXT, EGL_DMA_BUF_PLANE1_OFFSET_EXT, EGL_DMA_BUF_PLANE2_OFFSET_EXT,
          EGL_DMA_BUF_PLANE3_OFFSET_EXT };
    static const EGLint pitch_attr[ 4 ] =
        { EGL_DMA_BUF_PLANE0_PITCH_EXT, EGL_DMA_BUF_PLANE1_PITCH_EXT, EGL_DMA_BUF_PLANE2_PITCH_EXT,
          EGL_DMA_BUF_PLANE3_PITCH_EXT };
    static const EGLint mod_lo_attr[ 4 ] =
        { EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT,
          EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT };
    static const EGLint mod_hi_attr[ 4 ] =
        { EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT,
          EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT };
    if( buf.planes < 1 || buf.planes > 4 )
        return false;
    EGLint attribs[ 64 ];
    int n = 0;
    attribs[ n++ ] = EGL_WIDTH;
    attribs[ n++ ] = buf.width;
    attribs[ n++ ] = EGL_HEIGHT;
    attribs[ n++ ] = buf.height;
    attribs[ n++ ] = EGL_LINUX_DRM_FOURCC_EXT;
    attribs[ n++ ] = buf.fourcc;
    const bool with_modifier = buf.modifier != DRM_FORMAT_MOD_INVALID;
    if( with_modifier && !modifiers )
        return false;
    for( int i = 0;
         i < buf.planes;
         ++i )
        {
        attribs[ n++ ] = fd_attr[ i ];
        attribs[ n++ ] = buf.fd[ i ];
        attribs[ n++ ] = offset_attr[ i ];
        attribs[ n++ ] = buf.offset[ i ];
        attribs[ n++ ] = pitch_attr[ i ];
        attribs[ n++ ] = buf.stride[ i ];
        if( with_modifier )
            {
            attribs[ n++ ] = mod_lo_attr[ i ];
            attribs[ n++ ] = (EGLint)( buf.modifier & 0xffffffff );
            attribs[ n++ ] = mod_hi_attr[ i ];
            attribs[ n++ ] = (EGLint)( buf.modifier >> 32 );
            }
        }
    attribs[ n++ ] = EGL_IMAGE_PRESERVED_KHR;
    attribs[ n++ ] = EGL_TRUE;
    attribs[ n++ ] = EGL_NONE;
    t->image = createImage( egl, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attribs );
    if( t->image == EGL_NO_IMAGE_KHR )
        return false;
    glBindTexture( GL_TEXTURE_2D, t->tex );
    imageTargetTexture( GL_TEXTURE_2D, (GLeglImageOES) t->image );
    return glGetError() == GL_NO_ERROR;
    }

bool GlesPrivate::makeFramebuffer( GLTexture* t )
    {
    if( t->fbo != 0 )
        return true;
    glGenFramebuffers( 1, &t->fbo );
    glBindFramebuffer( GL_FRAMEBUFFER, t->fbo );
    glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->tex, 0 );
    if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
        {
        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
        glDeleteFramebuffers( 1, &t->fbo );
        t->fbo = 0;
        return false;
        }
    return true;
    }

void GlesPrivate::checkLost()
    {
    if( lost )
        return;
    if( resetStatus != NULL && resetStatus() != GL_NO_ERROR )
        {
        lost = true;
        kdWarning( 1212 ) << "compositor: the GPU was reset, the GL context is lost" << endl;
        }
    }

static bool hasExtension( const char* list, const char* name )
    {
    if( list == NULL )
        return false;
    const size_t len = strlen( name );
    for( const char* p = list;
         ( p = strstr( p, name )) != NULL;
         p += len )
        if(( p == list || p[ -1 ] == ' ' ) && ( p[ len ] == ' ' || p[ len ] == '\0' ))
            return true;
    return false;
    }

static GLuint compileShader( GLenum type, const char* source, QString* error )
    {
    GLuint shader = glCreateShader( type );
    glShaderSource( shader, 1, &source, NULL );
    glCompileShader( shader );
    GLint ok = 0;
    glGetShaderiv( shader, GL_COMPILE_STATUS, &ok );
    if( !ok )
        {
        char log[ 1024 ];
        glGetShaderInfoLog( shader, sizeof( log ), NULL, log );
        *error = QString( "shader: " ) + log;
        glDeleteShader( shader );
        return 0;
        }
    return shader;
    }

//****************************************
// GlesRenderer
//****************************************

GlesRenderer::GlesRenderer( Display* dpy )
    : Renderer( dpy ), d( new GlesPrivate( dpy ))
    {
    }

GlesRenderer::~GlesRenderer()
    {
    delete d;
    }

Renderer* GlesRenderer::create( Display* dpy, QString* error )
    {
    GlesRenderer* r = new GlesRenderer( dpy );
    if( !r->init( error ))
        {
        delete r;
        return NULL;
        }
    return r;
    }

bool GlesRenderer::init( QString* error )
    {
    QString dummy;
    if( error == NULL )
        error = &dummy;
    d->xcb = XGetXCBConnection( dpy );
    const xcb_query_extension_reply_t* ext = xcb_get_extension_data( d->xcb, &xcb_dri3_id );
    if( ext == NULL || !ext->present )
        {
        *error = i18n( "The X server has no DRI3 (no GPU rendering)." );
        return false;
        }
    xcb_dri3_query_version_reply_t* ver = xcb_dri3_query_version_reply( d->xcb,
        xcb_dri3_query_version( d->xcb, 1, 2 ), NULL );
    if( ver == NULL )
        {
        *error = "DRI3 QueryVersion failed";
        return false;
        }
    d->dri3_major = ver->major_version;
    d->dri3_minor = ver->minor_version;
    free( ver );
    const Window root = DefaultRootWindow( dpy );
    xcb_dri3_open_reply_t* open_reply = xcb_dri3_open_reply( d->xcb, xcb_dri3_open( d->xcb, root, None ), NULL );
    if( open_reply == NULL || open_reply->nfd != 1 )
        {
        free( open_reply );
        *error = i18n( "The X server did not open its GPU device for us (DRI3Open)." );
        return false;
        }
    d->drm_fd = xcb_dri3_open_reply_fds( d->xcb, open_reply )[ 0 ];
    free( open_reply );
    fcntl( d->drm_fd, F_SETFD, FD_CLOEXEC );
    d->gbm = gbm_create_device( d->drm_fd );
    if( d->gbm == NULL )
        {
        *error = "gbm_create_device failed";
        return false;
        }
    const char* client_ext = eglQueryString( EGL_NO_DISPLAY, EGL_EXTENSIONS );
    if( !hasExtension( client_ext, "EGL_KHR_platform_gbm" ) && !hasExtension( client_ext, "EGL_MESA_platform_gbm" ))
        {
        *error = "EGL without the GBM platform";
        return false;
        }
    PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress( "eglGetPlatformDisplayEXT" );
    if( getPlatformDisplay == NULL )
        {
        *error = "no eglGetPlatformDisplayEXT";
        return false;
        }
    d->egl = getPlatformDisplay( EGL_PLATFORM_GBM_KHR, d->gbm, NULL );
    EGLint egl_major, egl_minor;
    if( d->egl == EGL_NO_DISPLAY || !eglInitialize( d->egl, &egl_major, &egl_minor ))
        {
        d->egl = EGL_NO_DISPLAY;
        *error = "eglInitialize failed";
        return false;
        }
    const char* egl_ext = eglQueryString( d->egl, EGL_EXTENSIONS );
    if( !hasExtension( egl_ext, "EGL_KHR_surfaceless_context" )
        || !hasExtension( egl_ext, "EGL_EXT_image_dma_buf_import" ) || !hasExtension( egl_ext, "EGL_KHR_image_base" ))
        {
        *error = "EGL lacks surfaceless contexts or DMA-BUF import";
        return false;
        }
    d->modifiers = hasExtension( egl_ext, "EGL_EXT_image_dma_buf_import_modifiers" );
    d->createImage = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress( "eglCreateImageKHR" );
    d->destroyImage = (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress( "eglDestroyImageKHR" );
    if( d->modifiers )
        d->queryModifiers = (PFNEGLQUERYDMABUFMODIFIERSEXTPROC) eglGetProcAddress( "eglQueryDmaBufModifiersEXT" );
    if( !eglBindAPI( EGL_OPENGL_ES_API ))
        {
        *error = "eglBindAPI failed";
        return false;
        }
    EGLConfig config = 0;
    if( !hasExtension( egl_ext, "EGL_KHR_no_config_context" ) && !hasExtension( egl_ext, "EGL_MESA_configless_context" ))
        {
        static const EGLint config_attribs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE };
        EGLint n = 0;
        if( !eglChooseConfig( d->egl, config_attribs, &config, 1, &n ) || n < 1 )
            {
            *error = "no GLES 3 EGL config";
            return false;
            }
        }
    const bool robustness = hasExtension( egl_ext, "EGL_EXT_create_context_robustness" );
    for( int attempt = robustness ? 0 : 1;
         attempt < 2 && d->ctx == EGL_NO_CONTEXT;
         ++attempt )
        {
        EGLint attribs[ 16 ];
        int n = 0;
        attribs[ n++ ] = EGL_CONTEXT_MAJOR_VERSION;
        attribs[ n++ ] = 3;
        attribs[ n++ ] = EGL_CONTEXT_MINOR_VERSION;
        attribs[ n++ ] = 0;
        if( attempt == 0 )
            { // the context reports GPU resets instead of crashing us
            attribs[ n++ ] = EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT;
            attribs[ n++ ] = EGL_LOSE_CONTEXT_ON_RESET_EXT;
            }
        attribs[ n++ ] = EGL_NONE;
        d->ctx = eglCreateContext( d->egl, config, EGL_NO_CONTEXT, attribs );
        d->robust = d->ctx != EGL_NO_CONTEXT && attempt == 0;
        }
    if( d->ctx == EGL_NO_CONTEXT )
        {
        *error = "eglCreateContext (GLES 3.0) failed";
        return false;
        }
    if( !eglMakeCurrent( d->egl, EGL_NO_SURFACE, EGL_NO_SURFACE, d->ctx ))
        {
        *error = "eglMakeCurrent failed";
        return false;
        }
    const char* gl_ext = (const char*) glGetString( GL_EXTENSIONS );
    if( !hasExtension( gl_ext, "GL_OES_EGL_image" ))
        {
        *error = "GLES without GL_OES_EGL_image";
        return false;
        }
    d->imageTargetTexture = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) eglGetProcAddress( "glEGLImageTargetTexture2DOES" );
    if( d->robust && ( hasExtension( gl_ext, "GL_EXT_robustness" ) || hasExtension( gl_ext, "GL_KHR_robustness" )))
        {
        d->resetStatus = (PFNGLGETGRAPHICSRESETSTATUSEXTPROC) eglGetProcAddress( "glGetGraphicsResetStatusEXT" );
        if( d->resetStatus == NULL )
            d->resetStatus = (PFNGLGETGRAPHICSRESETSTATUSEXTPROC) eglGetProcAddress( "glGetGraphicsResetStatusKHR" );
        }
    if( d->createImage == NULL || d->destroyImage == NULL || d->imageTargetTexture == NULL )
        {
        *error = "EGL image functions missing";
        return false;
        }
    d->renderer = QString::fromLatin1( (const char*) glGetString( GL_RENDERER ));
    d->version = QString::fromLatin1( (const char*) glGetString( GL_VERSION ));
    // the shader program
    GLuint vs = compileShader( GL_VERTEX_SHADER, vertex_source, error );
    GLuint fs = vs != 0 ? compileShader( GL_FRAGMENT_SHADER, fragment_source, error ) : 0;
    if( vs == 0 || fs == 0 )
        {
        if( vs != 0 )
            glDeleteShader( vs );
        return false;
        }
    d->program = glCreateProgram();
    glAttachShader( d->program, vs );
    glAttachShader( d->program, fs );
    glBindAttribLocation( d->program, 0, "pos" );
    glBindAttribLocation( d->program, 1, "tc" );
    glBindAttribLocation( d->program, 2, "mc" );
    glLinkProgram( d->program );
    glDeleteShader( vs );
    glDeleteShader( fs );
    GLint linked = 0;
    glGetProgramiv( d->program, GL_LINK_STATUS, &linked );
    if( !linked )
        {
        char log[ 1024 ];
        glGetProgramInfoLog( d->program, sizeof( log ), NULL, log );
        *error = QString( "shader link: " ) + log;
        return false;
        }
    glUseProgram( d->program );
    d->u_target_size = glGetUniformLocation( d->program, "target_size" );
    d->u_tex_size = glGetUniformLocation( d->program, "tex_size" );
    d->u_mask_size = glGetUniformLocation( d->program, "mask_size" );
    d->u_color = glGetUniformLocation( d->program, "color" );
    d->u_opacity = glGetUniformLocation( d->program, "opacity" );
    d->u_use_tex = glGetUniformLocation( d->program, "use_tex" );
    d->u_tex_opaque = glGetUniformLocation( d->program, "tex_opaque" );
    d->u_use_mask = glGetUniformLocation( d->program, "use_mask" );
    d->u_tex_edge = glGetUniformLocation( d->program, "tex_edge" );
    d->u_mask_edge = glGetUniformLocation( d->program, "mask_edge" );
    d->u_tex = glGetUniformLocation( d->program, "tex" );
    d->u_mask = glGetUniformLocation( d->program, "mask" );
    glUniform1i( d->u_tex, 0 );
    glUniform1i( d->u_mask, 1 );
    glGenVertexArrays( 1, &d->vao );
    glBindVertexArray( d->vao );
    glGenBuffers( 1, &d->vbo );
    glBindBuffer( GL_ARRAY_BUFFER, d->vbo );
    for( int i = 0;
         i < 3;
         ++i )
        {
        glEnableVertexAttribArray( i );
        glVertexAttribPointer( i, 2, GL_FLOAT, GL_FALSE, 6 * sizeof( GLfloat ), (void*)( i * 2 * sizeof( GLfloat )));
        }
    glDisable( GL_DEPTH_TEST );
    glDisable( GL_CULL_FACE );
    // the fence of acquireClientBuffers()
    d->use_fence = getenv( "KWIN_COMPOSITOR_NO_FENCE" ) == NULL;
    const xcb_query_extension_reply_t* sync_ext = xcb_get_extension_data( d->xcb, &xcb_sync_id );
    if( d->use_fence && sync_ext != NULL && sync_ext->present )
        {
        int fd = xshmfence_alloc_shm();
        if( fd >= 0 )
            {
            d->shm_fence = xshmfence_map_shm( fd );
            if( d->shm_fence != NULL )
                {
                d->x_fence = xcb_generate_id( d->xcb );
                xcb_generic_error_t* err = xcb_request_check( d->xcb,
                    xcb_dri3_fence_from_fd_checked( d->xcb, root, d->x_fence, false, fd )); // (fd is sent and closed)
                if( err != NULL )
                    {
                    free( err );
                    d->x_fence = 0;
                    xshmfence_unmap_shm( d->shm_fence );
                    d->shm_fence = NULL;
                    }
                }
            else
                close( fd );
            }
        }
    if( d->shm_fence == NULL )
        d->use_fence = false;
    if( glGetError() != GL_NO_ERROR )
        {
        *error = "GL error during the initialization";
        return false;
        }
    return true;
    }

QString GlesRenderer::description() const
    {
    return QString( "%1: %2 (DRI3 %3.%4%5%6)" ).arg( d->version ).arg( d->renderer ).arg( d->dri3_major )
        .arg( d->dri3_minor ).arg( d->modifiers ? ", modifiers" : "" ).arg( d->use_fence ? ", X fence" : "" );
    }

bool GlesRenderer::isLost() const
    {
    return d->lost;
    }

static unsigned int fourccForDepth( int depth, int bpp )
    {
    if( bpp == 32 && depth == 32 )
        return DRM_FORMAT_ARGB8888;
    if( bpp == 32 && depth == 24 )
        return DRM_FORMAT_XRGB8888;
    if( bpp == 32 && depth == 30 )
        return DRM_FORMAT_XRGB2101010;
    if( bpp == 16 && depth == 16 )
        return DRM_FORMAT_RGB565;
    return 0;
    }

Texture* GlesRenderer::textureFromPixmap( Pixmap pixmap, int, int, int, Visual* )
    {
    if( d->lost )
        return NULL;
    DmaBuf buf;
    int depth = 0;
    int bpp = 0;
    if( d->dri3_major > 1 || d->dri3_minor >= 2 )
        {
        xcb_generic_error_t* error = NULL; // (a window gone meanwhile: no error for Xlib's handler)
        xcb_dri3_buffers_from_pixmap_reply_t* reply = xcb_dri3_buffers_from_pixmap_reply( d->xcb,
            xcb_dri3_buffers_from_pixmap( d->xcb, pixmap ), &error );
        free( error );
        if( reply == NULL )
            return NULL;
        int* fds = xcb_dri3_buffers_from_pixmap_reply_fds( d->xcb, reply );
        const uint32_t* strides = xcb_dri3_buffers_from_pixmap_strides( reply );
        const uint32_t* offsets = xcb_dri3_buffers_from_pixmap_offsets( reply );
        buf.width = reply->width;
        buf.height = reply->height;
        buf.modifier = reply->modifier;
        buf.planes = QMIN( (int) reply->nfd, 4 );
        for( int i = 0;
             i < buf.planes;
             ++i )
            {
            buf.fd[ i ] = fds[ i ];
            buf.stride[ i ] = strides[ i ];
            buf.offset[ i ] = offsets[ i ];
            }
        for( int i = buf.planes;
             i < reply->nfd;
             ++i )
            close( fds[ i ] );
        depth = reply->depth;
        bpp = reply->bpp;
        free( reply );
        }
    else
        {
        xcb_generic_error_t* error = NULL;
        xcb_dri3_buffer_from_pixmap_reply_t* reply = xcb_dri3_buffer_from_pixmap_reply( d->xcb,
            xcb_dri3_buffer_from_pixmap( d->xcb, pixmap ), &error );
        free( error );
        if( reply == NULL )
            return NULL;
        buf.fd[ 0 ] = xcb_dri3_buffer_from_pixmap_reply_fds( d->xcb, reply )[ 0 ];
        buf.width = reply->width;
        buf.height = reply->height;
        buf.stride[ 0 ] = reply->stride;
        buf.planes = 1;
        buf.modifier = DRM_FORMAT_MOD_INVALID;
        depth = reply->depth;
        bpp = reply->bpp;
        free( reply );
        }
    buf.fourcc = fourccForDepth( depth, bpp );
    Texture* t = buf.fourcc != 0 ? textureFromDmaBuf( buf, depth == 32 ) : NULL;
    for( int i = 0;
         i < buf.planes;
         ++i )
        close( buf.fd[ i ] );
    return t;
    }

Texture* GlesRenderer::textureFromDmaBuf( const DmaBuf& buf, bool alpha )
    {
    if( d->lost )
        return NULL;
    GLTexture* t = new GLTexture( d, buf.width, buf.height, alpha, false );
    if( !d->importDmaBuf( t, buf ))
        {
        delete t;
        return NULL;
        }
    return t;
    }

Texture* GlesRenderer::textureFromImage( const QImage& image )
    {
    QImage img = image.convertDepth( 32 );
    const int w = img.width();
    const int h = img.height();
    if( w <= 0 || h <= 0 || d->lost )
        return NULL;
    const bool alpha = img.hasAlphaBuffer();
    // premultiplied, bytes B G R A (QRgb on little endian) uploaded as
    // RGBA with red and blue swizzled back
    QMemArray< unsigned char > data( w * h * 4 );
    for( int y = 0;
         y < h;
         ++y )
        {
        const QRgb* src = (const QRgb*) img.scanLine( y );
        unsigned char* dst = data.data() + y * w * 4;
        for( int x = 0;
             x < w;
             ++x )
            {
            const QRgb p = src[ x ];
            const unsigned int a = alpha ? qAlpha( p ) : 255;
            dst[ 4 * x + 0 ] = qBlue( p ) * a / 255;
            dst[ 4 * x + 1 ] = qGreen( p ) * a / 255;
            dst[ 4 * x + 2 ] = qRed( p ) * a / 255;
            dst[ 4 * x + 3 ] = a;
            }
        }
    GLTexture* t = new GLTexture( d, w, h, alpha, false );
    glBindTexture( GL_TEXTURE_2D, t->tex );
    glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
    glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED );
    return t;
    }

Texture* GlesRenderer::textureFromAlpha( const unsigned char* data, int w, int h, int stride )
    {
    if( w <= 0 || h <= 0 || d->lost )
        return NULL;
    GLTexture* t = new GLTexture( d, w, h, true, true );
    glBindTexture( GL_TEXTURE_2D, t->tex );
    glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
    glPixelStorei( GL_UNPACK_ROW_LENGTH, stride );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, data );
    glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
    glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
    // ( 0, 0, 0, value ): premultiplied black, like an A8 picture
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_ZERO );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_ZERO );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_ZERO );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED );
    return t;
    }

Texture* GlesRenderer::createOffscreen( int w, int h, bool alpha )
    {
    if( w <= 0 || h <= 0 || d->lost )
        return NULL;
    d->flush();
    GLTexture* t = new GLTexture( d, w, h, alpha, false );
    glBindTexture( GL_TEXTURE_2D, t->tex );
    glTexStorage2D( GL_TEXTURE_2D, 1, GL_RGBA8, w, h );
    if( !d->makeFramebuffer( t ))
        {
        delete t;
        return NULL;
        }
    glBindFramebuffer( GL_FRAMEBUFFER, t->fbo );
    glClearColor( 0, 0, 0, 0 );
    glClear( GL_COLOR_BUFFER_BIT );
    if( d->target != NULL )
        glBindFramebuffer( GL_FRAMEBUFFER, d->target->fbo );
    return t;
    }

/*
 The present buffers are allocated here, like Mesa's DRI3 swap chain does:
 with the modifiers the X server can scan out on the CRTCs of the window
 (DRI3 GetSupportedModifiers), so that a full-screen presentation can be a
 page flip.  Without them (or when that fails) a pixmap of the server.
*/
Texture* GlesRenderer::createPresentBuffer( Window window, int w, int h, Pixmap* pixmap )
    {
    if( d->lost )
        return NULL;
    d->flush();
    const int depth = DefaultDepth( dpy, DefaultScreen( dpy ));
    if( depth == 24 && ( d->dri3_major > 1 || d->dri3_minor >= 2 ) && getenv( "KWIN_COMPOSITOR_X_BUFFERS" ) == NULL )
        {
        QValueVector< unsigned long long > mods;
        xcb_dri3_get_supported_modifiers_reply_t* reply = xcb_dri3_get_supported_modifiers_reply( d->xcb,
            xcb_dri3_get_supported_modifiers( d->xcb, window, 24, 32 ), NULL );
        if( reply != NULL )
            {
            const uint64_t* list = xcb_dri3_get_supported_modifiers_window_modifiers( reply );
            int n = xcb_dri3_get_supported_modifiers_window_modifiers_length( reply );
            if( n == 0 )
                {
                list = xcb_dri3_get_supported_modifiers_screen_modifiers( reply );
                n = xcb_dri3_get_supported_modifiers_screen_modifiers_length( reply );
                }
            // only those we can render into
            EGLuint64KHR egl_mods[ 128 ];
            EGLBoolean external[ 128 ];
            EGLint egl_n = 0;
            if( d->queryModifiers != NULL )
                d->queryModifiers( d->egl, DRM_FORMAT_XRGB8888, 128, egl_mods, external, &egl_n );
            for( int i = 0;
                 i < n;
                 ++i )
                for( int j = 0;
                     j < egl_n;
                     ++j )
                    if( egl_mods[ j ] == list[ i ] && !external[ j ] )
                        mods.append( list[ i ] );
            free( reply );
            }
        struct gbm_bo* bo = NULL;
        if( !mods.isEmpty())
            bo = gbm_bo_create_with_modifiers2( d->gbm, w, h, GBM_FORMAT_XRGB8888,
                (const uint64_t*) &mods[ 0 ], mods.count(), GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT );
        if( bo == NULL )
            bo = gbm_bo_create( d->gbm, w, h, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT );
        if( bo != NULL )
            {
            DmaBuf buf;
            buf.width = w;
            buf.height = h;
            buf.fourcc = DRM_FORMAT_XRGB8888;
            buf.modifier = gbm_bo_get_modifier( bo );
            buf.planes = gbm_bo_get_plane_count( bo );
            int xfds[ 4 ] = { -1, -1, -1, -1 };
            bool ok = buf.planes >= 1 && buf.planes <= 4;
            for( int i = 0;
                 ok && i < buf.planes;
                 ++i )
                {
                buf.fd[ i ] = gbm_bo_get_fd_for_plane( bo, i );
                xfds[ i ] = buf.fd[ i ] >= 0 ? dup( buf.fd[ i ] ) : -1;
                buf.stride[ i ] = gbm_bo_get_stride_for_plane( bo, i );
                buf.offset[ i ] = gbm_bo_get_offset( bo, i );
                ok = buf.fd[ i ] >= 0 && xfds[ i ] >= 0;
                }
            GLTexture* t = NULL;
            if( !ok )
                for( int i = 0;
                     i < buf.planes && i < 4;
                     ++i )
                    if( xfds[ i ] >= 0 )
                        close( xfds[ i ] );
            if( ok )
                {
                Pixmap pix = xcb_generate_id( d->xcb );
                // (xcb sends and closes xfds)
                xcb_generic_error_t* err = xcb_request_check( d->xcb, xcb_dri3_pixmap_from_buffers_checked( d->xcb,
                    pix, window, buf.planes, w, h, buf.stride[ 0 ], buf.offset[ 0 ], buf.stride[ 1 ], buf.offset[ 1 ],
                    buf.stride[ 2 ], buf.offset[ 2 ], buf.stride[ 3 ], buf.offset[ 3 ], 24, 32, buf.modifier, xfds ));
                if( err == NULL )
                    {
                    t = new GLTexture( d, w, h, false, false );
                    t->pixmap = pix;
                    t->bo = bo;
                    if( !d->importDmaBuf( t, buf ) || !d->makeFramebuffer( t ))
                        {
                        delete t; // (frees the pixmap and the bo)
                        t = NULL;
                        bo = NULL;
                        }
                    }
                else
                    free( err );
                }
            for( int i = 0;
                 i < buf.planes && i < 4;
                 ++i )
                if( buf.fd[ i ] >= 0 )
                    close( buf.fd[ i ] );
            if( t != NULL )
                {
                if( d->target != NULL )
                    glBindFramebuffer( GL_FRAMEBUFFER, d->target->fbo );
                *pixmap = t->pixmap;
                return t;
                }
            if( bo != NULL )
                gbm_bo_destroy( bo );
            }
        }
    // a pixmap of the server, imported
    Pixmap pix = XCreatePixmap( dpy, window, w, h, depth );
    GLTexture* t = static_cast< GLTexture* >( textureFromPixmap( pix, w, h, depth, NULL ));
    if( t == NULL || !d->makeFramebuffer( t ))
        {
        delete t;
        XFreePixmap( dpy, pix );
        return NULL;
        }
    t->pixmap = pix;
    if( d->target != NULL )
        glBindFramebuffer( GL_FRAMEBUFFER, d->target->fbo );
    *pixmap = pix;
    return t;
    }

void GlesRenderer::acquireClientBuffers()
    {
    if( !d->use_fence )
        return;
    xshmfence_reset( d->shm_fence );
    xcb_sync_trigger_fence( d->xcb, d->x_fence );
    xcb_flush( d->xcb );
    if( xshmfence_await( d->shm_fence ) != 0 )
        {
        kdWarning( 1212 ) << "compositor: the X fence failed, rendering without it" << endl;
        d->use_fence = false;
        }
    }

SyncPoint GlesRenderer::finish()
    {
    d->flush();
    glFlush();
    d->checkLost();
    return SyncPoint();
    }

bool GlesRenderer::setTarget( Texture* target )
    {
    GLTexture* t = static_cast< GLTexture* >( target );
    d->flush();
    d->clipped = false;
    d->clip = QRegion();
    if( t == NULL || d->lost || !d->makeFramebuffer( t ))
        {
        d->target = NULL;
        return false;
        }
    d->target = t;
    glBindFramebuffer( GL_FRAMEBUFFER, t->fbo );
    glViewport( 0, 0, t->width(), t->height());
    glUseProgram( d->program );
    glUniform2f( d->u_target_size, t->width(), t->height());
    return true;
    }

void GlesRenderer::setClip( const QRegion& clip )
    {
    d->clip = clip;
    d->clipped = true;
    }

void GlesRenderer::clearClip()
    {
    d->clip = QRegion();
    d->clipped = false;
    }

/*
 Draws are batched: consecutive ones with the same state (textures,
 filters, edge modes, blending, opacity, colour) go into one vertex
 buffer and one draw call (the genie's strips, the shadows' cells).  Any
 other GL work first flushes the batch.
*/
void GlesPrivate::flush()
    {
    if( batch.size() == 0 )
        return;
    const BatchState& st = state;
    if( st.tex != NULL )
        {
        setFilter( st.tex, (Filter) st.filter );
        setWrap( st.tex, st.repeat == TileRepeat );
        glActiveTexture( GL_TEXTURE0 );
        glBindTexture( GL_TEXTURE_2D, st.tex->tex );
        glUniform1i( u_use_tex, 1 );
        glUniform1i( u_tex_opaque, st.tex->hasAlpha() ? 0 : 1 );
        glUniform2f( u_tex_size, st.tex->width(), st.tex->height());
        glUniform1i( u_tex_edge, st.repeat == NoRepeat ? ( st.filter == BilinearFilter ? 2 : 1 ) : 0 );
        }
    else
        {
        glUniform1i( u_use_tex, 0 );
        glUniform4f( u_color, st.color[ 0 ], st.color[ 1 ], st.color[ 2 ], st.color[ 3 ] );
        glUniform2f( u_tex_size, 1, 1 );
        glUniform1i( u_tex_edge, 0 );
        }
    if( st.mask != NULL )
        {
        setFilter( st.mask, (Filter) st.mask_filter );
        setWrap( st.mask, st.mask_repeat == TileRepeat );
        glActiveTexture( GL_TEXTURE1 );
        glBindTexture( GL_TEXTURE_2D, st.mask->tex );
        glActiveTexture( GL_TEXTURE0 );
        glUniform1i( u_use_mask, 1 );
        glUniform2f( u_mask_size, st.mask->width(), st.mask->height());
        glUniform1i( u_mask_edge, st.mask_repeat == NoRepeat ? ( st.mask_filter == BilinearFilter ? 2 : 1 ) : 0 );
        }
    else
        {
        glUniform1i( u_use_mask, 0 );
        glUniform2f( u_mask_size, 1, 1 );
        glUniform1i( u_mask_edge, 0 );
        }
    glUniform1f( u_opacity, st.opacity );
    switch( st.blend )
        {
        case BlendSource:
            glDisable( GL_BLEND );
            break;
        case BlendAdd:
            glEnable( GL_BLEND );
            glBlendFunc( GL_ONE, GL_ONE );
            break;
        default:
            glEnable( GL_BLEND );
            glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
            break;
        }
    glBufferData( GL_ARRAY_BUFFER, batch.size() * sizeof( GLfloat ), batch.data(), GL_STREAM_DRAW );
    glDrawArrays( GL_TRIANGLES, 0, batch.size() / 6 );
    batch.resize( 0 );
    state.tex = state.mask = NULL;
    }

void GlesPrivate::setWrap( GLTexture* t, bool tile )
    {
    if( t->tiled == tile )
        return;
    t->tiled = tile;
    glBindTexture( GL_TEXTURE_2D, t->tex );
    const GLint mode = tile ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, mode );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, mode );
    }

void GlesRenderer::draw( const Draw& dr )
    {
    if( d->target == NULL || d->lost || dr.dst.w <= 0 || dr.dst.h <= 0 )
        return;
    if( dr.texture != NULL && ( dr.src.w <= 0 || dr.src.h <= 0 ))
        return;
    RectF dst = dr.dst;
    RectF src = dr.src;
    RectF msrc = dr.mask_src;
    const double tsx = dr.texture != NULL ? dr.src.w / dr.dst.w : 0;
    const double tsy = dr.texture != NULL ? dr.src.h / dr.dst.h : 0;
    const double msx = dr.mask != NULL ? dr.mask_src.w / dr.dst.w : 0;
    const double msy = dr.mask != NULL ? dr.mask_src.h / dr.dst.h : 0;
    // a scaled texture with a transparent edge covers the pixels around its
    // fractional edges partly (XRender draws all pixels of the box, the
    // texture fading out at its edge): drawn over the whole pixels, the
    // shader fades the outside (GL would leave out pixels whose centre
    // isn't inside)
    if(( dr.texture != NULL && dr.repeat == NoRepeat && dr.filter == BilinearFilter )
        || ( dr.mask != NULL && dr.mask_repeat == NoRepeat && dr.mask_filter == BilinearFilter ))
        {
        const QRect b = dst.bounds();
        dst = RectF( b.x(), b.y(), b.width(), b.height());
        src = RectF( dr.src.x + ( dst.x - dr.dst.x ) * tsx, dr.src.y + ( dst.y - dr.dst.y ) * tsy,
            dst.w * tsx, dst.h * tsy );
        msrc = RectF( dr.mask_src.x + ( dst.x - dr.dst.x ) * msx, dr.mask_src.y + ( dst.y - dr.dst.y ) * msy,
            dst.w * msx, dst.h * msy );
        }
    // the pieces of dst inside the clip
    const double dx0 = QMAX( 0.0, dst.x );
    const double dy0 = QMAX( 0.0, dst.y );
    const double dx1 = QMIN( (double) d->target->width(), dst.x + dst.w );
    const double dy1 = QMIN( (double) d->target->height(), dst.y + dst.h );
    if( dx1 <= dx0 || dy1 <= dy0 )
        return;
    QMemArray< QRect > rects;
    if( d->clipped )
        {
        rects = d->clip.intersect( QRegion( RectF( dx0, dy0, dx1 - dx0, dy1 - dy0 ).bounds())).rects();
        if( rects.isEmpty())
            return;
        }
    // the batch: the same state appends
    GlesPrivate::BatchState st;
    st.tex = static_cast< GLTexture* >( dr.texture );
    st.mask = static_cast< GLTexture* >( dr.mask );
    st.filter = dr.filter;
    st.mask_filter = dr.mask_filter;
    st.repeat = dr.repeat;
    st.mask_repeat = dr.mask_repeat;
    st.blend = dr.blend;
    st.opacity = Compositing::clamp01( dr.opacity );
    const double a = Compositing::clamp01( dr.color_alpha );
    st.color[ 0 ] = dr.texture == NULL ? dr.color.red() / 255.0 * a : 0;
    st.color[ 1 ] = dr.texture == NULL ? dr.color.green() / 255.0 * a : 0;
    st.color[ 2 ] = dr.texture == NULL ? dr.color.blue() / 255.0 * a : 0;
    st.color[ 3 ] = dr.texture == NULL ? a : 0;
    if( d->batch.size() > 0 && !( st == d->state ))
        d->flush();
    d->state = st;
    const int count = d->clipped ? rects.size() : 1;
    const unsigned int base = d->batch.size();
    d->batch.resize( base + count * 36 );
    GLfloat* v = d->batch.data() + base;
    int n = 0;
    for( int i = 0;
         i < count;
         ++i )
        {
        double x0 = dx0, y0 = dy0, x1 = dx1, y1 = dy1;
        if( d->clipped )
            {
            x0 = QMAX( x0, (double) rects[ i ].x());
            y0 = QMAX( y0, (double) rects[ i ].y());
            x1 = QMIN( x1, (double) rects[ i ].x() + rects[ i ].width());
            y1 = QMIN( y1, (double) rects[ i ].y() + rects[ i ].height());
            if( x1 <= x0 || y1 <= y0 )
                continue;
            }
        const double xs[ 2 ] = { x0, x1 };
        const double ys[ 2 ] = { y0, y1 };
        static const int corners[ 6 ][ 2 ] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        for( int c = 0;
             c < 6;
             ++c )
            {
            const double x = xs[ corners[ c ][ 0 ]];
            const double y = ys[ corners[ c ][ 1 ]];
            *v++ = x;
            *v++ = y;
            *v++ = src.x + ( x - dst.x ) * tsx;
            *v++ = src.y + ( y - dst.y ) * tsy;
            *v++ = msrc.x + ( x - dst.x ) * msx;
            *v++ = msrc.y + ( y - dst.y ) * msy;
            }
        n += 6;
        }
    d->batch.resize( base + n * 6 );
    }

} // namespace Render

} // namespace KWinInternal
