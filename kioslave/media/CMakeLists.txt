include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/libmediacommon"
)

kde3_add_library(kiomedia OBJECT
  SOURCES
    kio_media.cpp
    mediaimpl.cpp
    mediaimpl.skel
  MOC
    kio_media.h
    mediaimpl.h
  LINK
    KDE3::kio
)

kdebase_dummy_cpp(_dummy)
kde3_add_module(kio_media
  SOURCES
    ${_dummy}
  LINK
    kiomedia
    mediacommon
    KDE3::kio
)

install(FILES
  media.protocol
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

add_subdirectory(libmediacommon)
add_subdirectory(mediamanager)
add_subdirectory(medianotifier)
add_subdirectory(mounthelper)
add_subdirectory(kfile-plugin)
add_subdirectory(kcmodule)
add_subdirectory(mimetypes)
add_subdirectory(services)
# propsdlgplugin needs the HAL backend (include_media_halbackend); HAL
# is not supported any more
