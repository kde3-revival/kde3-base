kde3_add_library(mediacommon OBJECT
  SOURCES
    medium.cpp
    actionlistboxitem.cpp
    notifieraction.cpp
    notifierserviceaction.cpp
    notifiernothingaction.cpp
    notifieropenaction.cpp
    notifiersettings.cpp
    mediamanagersettings.kcfgc
  # all_includes: the users link libkio
  LINK
    KDE3::kio
)

install(FILES
  mediamanagersettings.kcfg
  DESTINATION ${KDE3_KCFG_INSTALL_DIR})
