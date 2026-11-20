kde3_add_module(kcm_icons
  SOURCES
    iconthemes.cpp
    icons.cpp
    main.cpp
  MOC
    icons.h
    iconthemes.h
    main.h
  LINK
    KDE3::kio
)

install(FILES
  icons.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

# install-data-local: hide the module of older KDE versions
install(FILES uninstall.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/Settings/LookNFeel/Themes RENAME iconthemes.desktop)
