kde3_add_library(trashcommon OBJECT
  SOURCES
    trashimpl.cpp
  MOC
    trashimpl.h
  LINK
    KDE3::kio
)

kde3_add_module(kio_trash
  SOURCES
    kio_trash.cpp
  MOC
    kio_trash.h
  LINK
    trashcommon
    KDE3::kio
)

kde3_add_executable(ktrash
  SOURCES
    ktrash.cpp
  LINK
    KDE3::kio
)

install(FILES
  trash.protocol
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

add_subdirectory(kfile-plugin)
