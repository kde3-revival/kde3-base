kde3_add_module(kcm_kthememanager
  SOURCES
    kthemedlg.ui
    newthemewidget.ui
    knewthemedlg.cpp
    ktheme.cpp
    kthememanager.cpp
  MOC
    knewthemedlg.h
    kthememanager.h
  LINK
    KDE3::kio
)

install(FILES
  kthememanager.desktop
  installktheme.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

install(FILES
  x-ktheme.desktop
  DESTINATION ${KDE3_MIME_INSTALL_DIR}/application)

install(PROGRAMS
  kdeinstallktheme
  DESTINATION ${KDE3_BIN_INSTALL_DIR})
