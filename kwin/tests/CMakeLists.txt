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

# screendesktopstest: the per monitor desktops state (screendesktops.cpp, no X)
kde3_add_executable(screendesktopstest NO_INSTALL
  SOURCES
    screendesktopstest.cpp
    ../screendesktops.cpp
  LINK
    KDE3::kdecore
)

# wheelgrabtest: the active window gets the wheel without KWin's passive grab
# (XTest + XInput 2, run against a running KWin on a test X server)
if(TARGET X11::Xi AND TARGET X11::Xtst)
  kde3_add_executable(wheelgrabtest NO_INSTALL
    SOURCES
      wheelgrabtest.cpp
    LINK
      X11::X11
      X11::Xi
      X11::Xtst
  )
endif()
