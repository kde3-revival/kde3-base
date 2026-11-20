kde3_add_module(kio_sftp
  SOURCES
    process.cpp
    atomicio.cpp
    kio_sftp.cpp
    sftpfileattr.cpp
    ksshprocess.cpp
  LINK
    KDE3::kio
)

install(FILES
  sftp.protocol
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})
