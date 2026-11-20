kde3_add_module(kcm_konsole
  SOURCES
    schemaeditor.cpp
    sessioneditor.cpp
    kcmkonsole.cpp
    schemadialog.ui
    sessiondialog.ui
    kcmkonsoledialog.ui
  MOC
    kcmkonsole.h
    schemaeditor.h
    sessioneditor.h
  LINK
    KDE3::kdeui
    KDE3::kio
)

install(FILES
  kcmkonsole.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/.hidden)
