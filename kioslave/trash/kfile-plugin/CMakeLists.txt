kde3_add_module(kfile_trash
  SOURCES
    kfile_trash.cpp
  MOC
    kfile_trash.h
  LINK
    trashcommon
    KDE3::kio
)

install(FILES
  kfile_trash.desktop
  kfile_trash_system.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})
