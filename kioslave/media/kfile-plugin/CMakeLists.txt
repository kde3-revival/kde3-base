include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../libmediacommon"
)

kde3_add_module(kfile_media
  SOURCES
    kfilemediaplugin.cpp
  MOC
    kfilemediaplugin.h
  LINK
    KDE3::kio
    mediacommon
)

install(FILES
  kfile_media.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})
