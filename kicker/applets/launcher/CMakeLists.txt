include_directories(
  "${PROJECT_SOURCE_DIR}/kicker/libkicker"
  "${PROJECT_SOURCE_DIR}/kicker/kicker/ui"
)

kde3_add_module(launcher_panelapplet
  SOURCES
    quicklauncher.skel
    quicklauncher.cpp
    quickbutton.cpp
    quickaddappsmenu.cpp
    flowgridmanager.cpp
    popularity.cpp
    configdlgbase.ui
    prefs.kcfgc
    configdlg.cpp
  MOC
    configdlg.h
    quickaddappsmenu.h
    quickbutton.h
    quicklauncher.h
  LINK
    kicker_core
    kicker_buttons
    kicker_ui
    kickermain
    KDE3::kio
    KDE3::kdeui
    KDE3::kutils
)

install(FILES
  quicklauncher.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/applets)

install(FILES
  launcherapplet.kcfg
  DESTINATION ${KDE3_KCFG_INSTALL_DIR})
