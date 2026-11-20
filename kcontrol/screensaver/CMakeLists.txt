kde3_add_module(kcm_screensaver
  SOURCES
    advanceddialogimpl.ui
    scrnsave.cpp
    testwin.cpp
    saverlist.cpp
    saverconfig.cpp
    advanceddialog.cpp
    kswidget.cpp
  MOC
    advanceddialog.h
    kswidget.h
    scrnsave.h
    testwin.h
  LINK
    KDE3::kio
    ${DPMSLIB}
    ${GLLIB}
)

install(FILES
  screensaver.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
