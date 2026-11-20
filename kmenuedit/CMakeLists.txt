kde3_add_library(kmenueditcommon OBJECT
  SOURCES
    basictab.cpp
    treeview.cpp
    kmenuedit.cpp
    khotkeys.cpp
    menufile.cpp
    menuinfo.cpp
  MOC
    basictab.h
    kmenuedit.h
    treeview.h
  LINK
    KDE3::kdeui
    KDE3::kio
)

kde3_add_kdeinit_executable(kmenuedit
  SOURCES
    main.cpp
  LINK
    kmenueditcommon
)

kde3_add_kdeinit_executable(kcontroledit
  SOURCES
    kcontrol_main.cpp
  LINK
    kmenueditcommon
)

install(FILES
  kmenuedit.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

install(FILES
  kmenueditui.rc
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kmenuedit)

install(FILES
  kcontroleditui.rc
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kcontroledit)

# install-data-local
install(FILES
  uninstall.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/System RENAME kmenuedit.desktop)

kde3_install_icons(kmenuedit)

add_subdirectory(pixmaps)
