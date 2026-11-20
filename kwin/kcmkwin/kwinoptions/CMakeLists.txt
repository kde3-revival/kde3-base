kde3_add_module(kcm_kwinoptions
  SOURCES
    windows.cpp
    mouse.cpp
    main.cpp
  MOC
    main.h
    mouse.h
    windows.h
  LINK
    KDE3::kdeui
)

install(FILES
  kwinactions.desktop
  kwinadvanced.desktop
  kwinfocus.desktop
  kwinmoving.desktop
  kwintranslucency.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/.hidden)

install(FILES
  kwinoptions.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
