include_directories(
  "${PROJECT_SOURCE_DIR}/kicker/kicker/core"
  "${PROJECT_SOURCE_DIR}/kicker/libkicker"
  "${PROJECT_BINARY_DIR}/kicker/libkicker"
  "${CMAKE_CURRENT_SOURCE_DIR}/../background"
)

kde3_add_module(kcm_kicker
  SOURCES
    positiontab.ui
    hidingtab.ui
    lookandfeeltab.ui
    menutab.ui
    positiontab_impl.cpp
    hidingtab_impl.cpp
    lookandfeeltab_impl.cpp
    menutab_impl.cpp
    extensionInfo.cpp
    main.cpp
    main.skel
    advancedOptions.ui
    advancedDialog.cpp
    positionconfig.cpp
    hidingconfig.cpp
    menuconfig.cpp
    lookandfeelconfig.cpp
  MOC
    advancedDialog.h
    hidingconfig.h
    hidingtab_impl.h
    lookandfeelconfig.h
    lookandfeeltab_impl.h
    main.h
    menuconfig.h
    menutab_impl.h
    positionconfig.h
    positiontab_impl.h
  LINK
    kickermain
    bgnd
    KDE3::kio
    KDE3::kutils
)

install(FILES
  panel.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

install(FILES
  kicker_config.desktop
  kicker_config_arrangement.desktop
  kicker_config_hiding.desktop
  kicker_config_menus.desktop
  kicker_config_appearance.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/.hidden)

# install-data-local: hide the module of older KDE versions
install(FILES uninstall.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/Settings/LookNFeel RENAME panel.desktop)
install(FILES uninstall.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/Settings/LookNFeel RENAME panel_appearance.desktop)
install(FILES uninstall.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR} RENAME panel_appearance.desktop)
