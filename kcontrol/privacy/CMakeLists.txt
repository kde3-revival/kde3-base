kde3_add_module(kcm_privacy
  SOURCES
    privacy.cpp
    kprivacysettings.ui
    kcmprivacydialog.ui
    kprivacymanager.cpp
  MOC
    kprivacymanager.h
    privacy.h
  LINK
    KDE3::kdeui
    KDE3::kio
)

install(FILES
  privacy.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
