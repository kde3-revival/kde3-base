include_directories(
  "${PROJECT_SOURCE_DIR}/kcontrol/background"
  "${PROJECT_SOURCE_DIR}/libkonq"
  "${PROJECT_SOURCE_DIR}/kdmlib"
)

kde3_add_library(kdesktopsettings OBJECT
  SOURCES
    kdesktopsettings.kcfgc
    klaunchsettings.kcfgc
  LINK
    KDE3::kdecore
)

kde3_add_kdeinit_executable(kdesktop
  SOURCES
    main.cc
    krootwm.cc
    xautolock.cc
    kdiconview.cc
    desktop.cc
    lockeng.cc
    KDesktopIface.skel
    bgmanager.cc
    init.cc
    KScreensaverIface.skel
    minicli.cpp
    KBackgroundIface.skel
    pixmapserver.cc
    kcustommenu.cc
    startupid.cpp
    minicli_ui.ui
    xautolock_diy.c
    xautolock_engine.c
    kshadowengine.cpp
    kshadowsettings.cpp
    kdesktopshadowsettings.cpp
    kfileividesktop.cpp
  MOC
    bgmanager.h
    desktop.h
    kcustommenu.h
    kdiconview.h
    krootwm.h
    lockeng.h
    minicli.h
    pixmapserver.h
    startupid.h
    xautolock.h
  LINK
    kdesktopsettings
    konq
    bgnd
    dmctl
    ${LIB_XSS}
    KDE3::kutils
    ${LIB_XCURSOR}
)

kde3_add_executable(kcheckrunning
  SOURCES
    kcheckrunning.cpp
  LINK
    X11::X11
)

install(FILES
  KDesktopIface.h
  KScreensaverIface.h
  KBackgroundIface.h
  DESTINATION ${KDE3_INCLUDE_INSTALL_DIR})

install(FILES
  kdesktop_custom_menu1
  kdesktop_custom_menu2
  DESTINATION ${KDE3_CONFIG_INSTALL_DIR})

install(FILES
  kdesktop.desktop
  DESTINATION ${KDE3_AUTOSTART_INSTALL_DIR})

install(FILES
  kdesktopSetAsBackground.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqueror/servicemenus)

install(FILES
  kdesktop_bgperscreen.upd
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)

install(PROGRAMS
  kdesktop_bgperscreen.sh
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)

install(FILES
  kdesktop.kcfg
  klaunch.kcfg
  DESTINATION ${KDE3_KCFG_INSTALL_DIR})

add_subdirectory(lock)
add_subdirectory(pics)
add_subdirectory(patterns)
add_subdirectory(programs)
add_subdirectory(init)
add_subdirectory(kwebdesktop)
