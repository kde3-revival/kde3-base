include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/themeengine"
  "${CMAKE_CURRENT_SOURCE_DIR}/themeengine/default"
)

kde3_add_executable(ksplash
  SOURCES
    wndmain.cpp
    ksplashiface.skel
    main.cpp
  MOC
    wndmain.h
  LINK
    KDE3::kdeui
    themedefault
    ksplashthemes
    KDE3::kio
)

install(FILES
  ksplash.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

add_subdirectory(themeengine)
add_subdirectory(pics)
add_subdirectory(kcmksplash)
