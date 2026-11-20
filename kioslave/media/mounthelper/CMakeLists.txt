include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../libmediacommon"
)

kde3_add_executable(kio_media_mounthelper
  SOURCES
    kio_media_mounthelper.cpp
  MOC
    kio_media_mounthelper.h
  LINK
    mediacommon
    KDE3::kio
)
