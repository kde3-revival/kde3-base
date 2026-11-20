kde3_add_module(kcm_clock
  SOURCES
    dtime.cpp
    tzone.cpp
    main.cpp
  MOC
    dtime.h
    main.h
    tzone.h
  LINK
    KDE3::kio
)

install(FILES
  clock.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
