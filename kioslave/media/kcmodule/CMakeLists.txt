include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../libmediacommon"
  "${CMAKE_CURRENT_BINARY_DIR}/../libmediacommon"
)

kde3_add_module(kcm_media
  SOURCES
    notifiermodule.cpp
    notifiermoduleview.ui
    serviceconfigdialog.cpp
    serviceview.ui
    mimetypelistboxitem.cpp
    managermodule.cpp
    managermoduleview.ui
    main.cpp
  MOC
    main.h
    managermodule.h
    notifiermodule.h
    serviceconfigdialog.h
  LINK
    mediacommon
    KDE3::kio
)

install(FILES
  media.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
