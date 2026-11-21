# compositorbench: the renderer backends with a desktop-like scene (see the
# comment at the top of compositorbench.cpp); not installed
kde3_add_executable(compositorbench NO_INSTALL
  SOURCES
    compositorbench.cpp
    ../render.cpp
    ../render_xrender.cpp
    ../shadow.cpp
    ../compositing.cpp
  MOC
    ../compositing.h
  LINK
    KDE3::kdecore
    ${LIB_KWIN_OVERVIEW}
    ${LIB_KWIN_GLES}
)
if(KWIN_HAVE_GLES)
  target_sources(compositorbench PRIVATE ../render_gles.cpp)
endif()
