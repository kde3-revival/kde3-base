include_directories(
  "${PROJECT_SOURCE_DIR}/libkonq"
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)

kde3_add_module(konq_sidebartree_history
  SOURCES
    history_module.cpp
    history_item.cpp
    history_settings.cpp
    history_settings.skel
  MOC
    history_module.h
    history_settings.h
  LINK
    konqsidebarplugin
    konq_sidebar_tree
)

kde3_add_module(kcm_history
  SOURCES
    kcmhistory.cpp
    history_dlg.ui
    history_settings.cpp
    history_settings.skel
  MOC
    history_settings.h
    kcmhistory.h
  LINK
    KDE3::kdeui
    konq
)

install(FILES
  kcmhistory.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
