include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
)

kde3_add_module(kcm_kwindecoration
  SOURCES
    kwindecoration.cpp
    buttons.cpp
    kwindecorationIface.skel
    preview.cpp
  MOC
    buttons.h
    kwindecoration.h
    preview.h
  LINK
    KDE3::kdeui
    kdecorations
)

install(FILES
  kwindecoration.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
