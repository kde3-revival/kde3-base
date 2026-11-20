kde3_add_module(kcm_keys
  SOURCES
    shortcuts.cpp
    modifiers.cpp
    main.cpp
    khotkeys.cpp
    treeview.cpp
    commandShortcuts.cpp
  MOC
    commandShortcuts.h
    main.h
    modifiers.h
    shortcuts.h
    treeview.h
  LINK
    KDE3::kdeui
    KDE3::kio
)

install(FILES
  kaccel.upd
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)

install(PROGRAMS
  convertShortcuts.pl
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)

install(FILES
  keys.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

install(FILES
  wm3.kksrc
  kde3.kksrc
  kde4.kksrc
  win3.kksrc
  win4.kksrc
  mac4.kksrc
  unix3.kksrc
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kcmkeys)
