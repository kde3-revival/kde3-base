include_directories(
  "${PROJECT_BINARY_DIR}/kicker/taskbar"
)

kde3_add_module(kcm_taskbar
  SOURCES
    kcmtaskbarui.ui
    kcmtaskbar.cpp
  MOC
    kcmtaskbar.h
  LINK
    taskbar
    KDE3::kdeui
)

install(FILES
  kcmtaskbar.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

# install-data-local: hide the module of older KDE versions
install(FILES uninstall.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/Settings/LookNFeel RENAME kcmtaskbar.desktop)
