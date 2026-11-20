add_compile_definitions(_KDEPRINT_COMPILE)

kde3_add_module(kcm_printmgr
  SOURCES
    kcmprintmgr.cpp
  LINK
    KDE3::kdeprint_management
)

install(FILES
  printers.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

kde3_install_icons(printmgr)
