kde3_add_kdeinit_executable(kcontrol
  SOURCES
    main.cpp
    toplevel.cpp
    indexwidget.cpp
    helpwidget.cpp
    dockcontainer.cpp
    aboutwidget.cpp
    moduletreeview.cpp
    moduleiconview.cpp
    moduleIface.cpp
    moduleIface.skel
    global.cpp
    modules.cpp
    proxywidget.cpp
    kcrootonly.cpp
    searchwidget.cpp
  MOC
    aboutwidget.h
    dockcontainer.h
    indexwidget.h
    main.h
    moduleIface.h
    moduleiconview.h
    modules.h
    moduletreeview.h
    proxywidget.h
    searchwidget.h
    toplevel.h
  LINK
    KDE3::kdeui
    KDE3::kutils
    KDE3::khtml
)

install(FILES
  kcontrolui.rc
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kcontrol)

install(FILES
  kinfocenterui.rc
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kinfocenter)

install(FILES
  KControl.desktop
  kinfocenter.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

kde3_install_icons(AUTO)

add_subdirectory(about)

# install-data-local
kde3_install_symlink(kcontrol ${KDE3_BIN_INSTALL_DIR}/kinfocenter)
