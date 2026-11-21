include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../libmediacommon"
  "${CMAKE_CURRENT_BINARY_DIR}/../libmediacommon"
)

# include_media_linuxcdpolling (linux/cdrom.h)
if(COMPILE_LINUXCDPOLLING)
kde3_add_library(linuxcdpolling OBJECT
  SOURCES
    linuxcdpolling.cpp
  MOC
    linuxcdpolling.h
  LINK
    KDE3::kio
)

endif()

kde3_add_module(kded_mediamanager
  SOURCES
    mediamanager.cpp
    mediamanager.skel
    medialist.cpp
    backendbase.cpp
    fstabbackend.cpp
    removablebackend.cpp
    mediadirnotify.cpp
    mediadirnotify.skel
  MOC
    fstabbackend.h
    medialist.h
    mediamanager.h
    removablebackend.h
  LINK
    KDE3::kio
    mediacommon
)
if(COMPILE_LINUXCDPOLLING)
  target_link_libraries(kded_mediamanager PRIVATE linuxcdpolling)
endif()

# The UDisks2 backend; udisks2dbus.cpp talks to UDisks2 with GIO's GDBus
if(COMPILE_UDISKS2BACKEND)
  target_sources(kded_mediamanager PRIVATE
    udisks2backend.cpp
    udisks2dbus.cpp
  )
  target_link_libraries(kded_mediamanager PRIVATE PkgConfig::GIO)
endif()

install(FILES
  mediamanager.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR}/kded)
