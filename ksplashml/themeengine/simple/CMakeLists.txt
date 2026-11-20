kde3_add_executable(ksplashsimple
  SOURCES
    main.cpp
  LINK
    X11::X11
)
if(HAVE_XINERAMA)
  # LIB_XINERAMA = -lXinerama -lXext
  target_link_libraries(ksplashsimple PRIVATE X11::Xinerama X11::Xext)
endif()
