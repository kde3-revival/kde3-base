kde3_add_module(kcm_ioslaveinfo
  SOURCES
    kcmioslaveinfo.cpp
  MOC
    kcmioslaveinfo.h
  LINK
    KDE3::kdeui
    KDE3::kio
)

install(FILES
  ioslaveinfo.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

# install-data-local (only creates the directory)
install(DIRECTORY DESTINATION ${KDE3_APPS_INSTALL_DIR}/Settings/Information)
