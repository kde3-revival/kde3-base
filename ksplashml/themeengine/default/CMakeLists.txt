include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)

kde3_add_library(themedefault OBJECT
  SOURCES
    themelegacy.cpp
  MOC
    themelegacy.h
  LINK
    KDE3::kdeui
    ksplashthemes
)

kde3_add_module(ksplashdefault
  SOURCES
    main.cpp
  LINK
    KDE3::kdeui
    ksplashthemes
    themedefault
)

install(FILES
  ksplashdefault.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})
