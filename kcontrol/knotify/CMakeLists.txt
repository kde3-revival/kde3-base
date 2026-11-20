kde3_add_module(kcm_knotify
  SOURCES
    knotify.cpp
    playersettings.ui
  MOC
    knotify.h
  LINK
    KDE3::kio
)

install(FILES
  kcmnotify.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

install(FILES
  play.png
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kcontrol/pics)

add_subdirectory(sounds)

# install-data-local: remove the modules of older KDE versions
install(CODE "file(REMOVE
  \"\$ENV{DESTDIR}${KDE3_FULL_APPS_INSTALL_DIR}/Settings/LookNFeel/knotify.desktop\"
  \"\$ENV{DESTDIR}${KDE3_FULL_APPS_INSTALL_DIR}/Settings/LookNFeel/kcmnotify.desktop\")")
