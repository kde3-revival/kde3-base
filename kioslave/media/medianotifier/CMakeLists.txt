include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../libmediacommon"
  "${CMAKE_CURRENT_BINARY_DIR}/../libmediacommon"
)

kde3_add_module(kded_medianotifier
  SOURCES
    medianotifier.cpp
    medianotifier.skel
    notificationdialog.cpp
    notificationdialogview.ui
  MOC
    medianotifier.h
    notificationdialog.h
  LINK
    mediacommon
    KDE3::kdecore
    KDE3::kdeui
    KDE3::kio
)

install(FILES
  medianotifier.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR}/kded)
