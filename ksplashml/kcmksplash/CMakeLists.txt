kde3_add_module(kcm_ksplashthemes
  SOURCES
    installer.cpp
    main.cpp
  MOC
    installer.h
  LINK
    KDE3::kio
)

install(FILES
  ksplashthememgr.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

add_subdirectory(pics)
