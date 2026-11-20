include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/plugins/ikws"
)

kde3_add_module(kcm_kurifilt
  SOURCES
    filteropts.cpp
    main.cpp
  MOC
    filteropts.h
    main.h
  LINK
    KDE3::kio
)

install(FILES
  ebrowsing.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

add_subdirectory(plugins)
