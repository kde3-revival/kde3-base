include_directories(
  "${PROJECT_SOURCE_DIR}/ksplashml/themeengine"
)

kde3_add_library(themestandard OBJECT
  SOURCES
    themestandard.cpp
    wndicon.cpp
    wndstatus.cpp
  MOC
    themestandard.h
    wndicon.h
    wndstatus.h
  LINK
    KDE3::kdeui
    ksplashthemes
)

kde3_add_module(ksplashstandard
  SOURCES
    themestandardlib.cpp
  LINK
    KDE3::kdeui
    ksplashthemes
    themestandard
)

install(FILES
  ksplashstandard.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

install(FILES
  Theme.rc
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/ksplash/Themes/Standard)

add_subdirectory(pics)
