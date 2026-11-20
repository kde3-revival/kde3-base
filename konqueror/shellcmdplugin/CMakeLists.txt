include_directories(
  "${PROJECT_SOURCE_DIR}/libkonq"
)

kde3_add_module(konq_shellcmdplugin
  SOURCES
    kshellcmdexecutor.cpp
    kshellcmddialog.cpp
    kshellcmdplugin.cpp
  MOC
    kshellcmddialog.h
    kshellcmdexecutor.h
    kshellcmdplugin.h
  LINK
    konq
)

install(FILES
  kshellcmdplugin.rc
  kshellcmdplugin.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqiconview/kpartplugins)

install(FILES
  kshellcmdplugin.rc
  kshellcmdplugin.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqlistview/kpartplugins)
