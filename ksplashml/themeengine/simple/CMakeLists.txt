kde3_add_executable(ksplashsimple
  SOURCES
    main.cpp
  LINK
    ${LIB_XINERAMA}
    X11::X11
)
