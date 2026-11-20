include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${CMAKE_CURRENT_BINARY_DIR}/.."
)

add_compile_definitions(QT_NO_CAST_ASCII)

kde3_add_module(kded_konqy_preloader
  SOURCES
    preloader.cc
    preloader.skel
  MOC
    preloader.h
  LINK
    konqueror_intern
    KDE3::kio
    KDE3::kdecore
)

install(FILES
  konqy_preloader.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR}/kded)

install(FILES
  konqy_preload.desktop
  DESTINATION ${KDE3_AUTOSTART_INSTALL_DIR})
