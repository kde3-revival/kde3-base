include_directories(
  "${PROJECT_SOURCE_DIR}/libkonq"
)

kde3_add_module(kcm_konq
  SOURCES
    behaviour.cpp
    desktop.cpp
    rootopts.cpp
    fontopts.cpp
    previews.cpp
    browser.cpp
    ${KDE3_INCLUDE_DIR}/kio/uiserver.stub
    main.cpp
    desktopbehavior_impl.cpp
    desktopbehavior.ui
  MOC
    behaviour.h
    browser.h
    desktop.h
    desktopbehavior_impl.h
    fontopts.h
    previews.h
    rootopts.h
  LINK
    KDE3::kio
    KDE3::kdeui
    KDE3::kutils
)

install(FILES
  overlapping.png
  onlyone.png
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kcontrol/pics)

install(FILES
  filebehavior.desktop
  fileappearance.desktop
  filepreviews.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/.hidden)

install(FILES
  desktopbehavior.desktop
  desktop.desktop
  desktoppath.desktop
  filebrowser.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
