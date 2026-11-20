kde3_add_module(kcm_css
  SOURCES
    template.cpp
    cssconfig.ui
    csscustom.ui
    kcmcss.cpp
    preview.ui
  MOC
    kcmcss.h
  LINK
    KDE3::kdeui
    KDE3::kio
)

install(FILES
  template.css
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kcmcss)

install(FILES
  kcmcss.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
