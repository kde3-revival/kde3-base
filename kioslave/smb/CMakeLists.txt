kde3_add_module(kio_smb
  SOURCES
    kio_smb.cpp
    kio_smb_auth.cpp
    kio_smb_browse.cpp
    kio_smb_config.cpp
    kio_smb_dir.cpp
    kio_smb_file.cpp
    kio_smb_internal.cpp
    kio_smb_mount.cpp
  MOC
    kio_smb.h
  LINK
    KDE3::kio
    PkgConfig::SMBCLIENT
)

install(FILES
  smb.protocol
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

install(FILES
  smb-network.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqueror/dirtree/remote)

install(FILES
  smb-network.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/remoteview)

install(FILES
  x-smb-workgroup.desktop
  x-smb-server.desktop
  DESTINATION ${KDE3_MIME_INSTALL_DIR}/application)
