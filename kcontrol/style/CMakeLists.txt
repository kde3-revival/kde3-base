kde3_add_module(kcm_style
  SOURCES
    kcmstyle.cpp
    stylepreview.ui
    menupreview.cpp
    styleconfdialog.cpp
  MOC
    kcmstyle.h
    menupreview.h
    styleconfdialog.h
  LINK
    KDE3::kio
    krdb
)

install(FILES
  style.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

add_subdirectory(keramik)
