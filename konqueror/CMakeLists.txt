include_directories(
  "${PROJECT_SOURCE_DIR}/libkonq"
  "${PROJECT_SOURCE_DIR}/kcontrol/input"
)

add_compile_definitions(_LARGEFILE64_SOURCE)

kde3_add_library(konqueror_intern OBJECT
  SOURCES
    konq_settingsxt.kcfgc
  LINK
    KDE3::kutils
)

kde3_add_kdeinit_executable(konqueror
  SOURCES
    konq_main.cc
    KonquerorIface.cc
    KonquerorIface.skel
    KonqMainWindowIface.cc
    KonqMainWindowIface.skel
    KonqViewIface.cc
    KonqViewIface.skel
    konq_guiclients.cc
    konq_run.cc
    konq_view.cc
    konq_viewmgr.cc
    konq_misc.cc
    konq_frame.cc
    konq_tabs.cc
    konq_actions.cc
    konq_profiledlg.cc
    konq_factory.cc
    konq_combo.cc
    konq_browseriface.cc
    delayedinitializer.cc
    konq_mainwindow.cc
    konq_extensionmanager.cc
  MOC
    delayedinitializer.h
    konq_actions.h
    konq_browseriface.h
    konq_combo.h
    konq_extensionmanager.h
    konq_frame.h
    konq_guiclients.h
    konq_mainwindow.h
    konq_mainwindow_p.h
    konq_misc.h
    konq_profiledlg.h
    konq_run.h
    konq_tabs.h
    konq_view.h
    konq_viewmgr.h
  LINK
    konq
    konqueror_intern
    KDE3::kutils
)

install(FILES
  konqueror.rc
  konq-simplebrowser.rc
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqueror)

install(FILES
  konqueror.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR})

install(FILES
  KonquerorIface.h
  DESTINATION ${KDE3_INCLUDE_INSTALL_DIR})

install(FILES
  kfmclient.desktop
  kfmclient_dir.desktop
  kfmclient_html.desktop
  kfmclient_war.desktop
  konqbrowser.desktop
  konquerorsu.desktop
  Home.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

install(FILES
  konqfilemgr.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/.hidden)

install(FILES
  konqueror.kcfg
  DESTINATION ${KDE3_KCFG_INSTALL_DIR})

# install-data-local: the view profiles
install(FILES profile_webbrowsing.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqueror/profiles RENAME webbrowsing)
install(FILES profile_filemanagement.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqueror/profiles RENAME filemanagement)
install(FILES profile_midnightcommander.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqueror/profiles RENAME midnightcommander)
install(FILES profile_tabbedbrowsing.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqueror/profiles RENAME tabbedbrowsing)
install(FILES profile_kde_devel.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqueror/profiles RENAME kde_devel)
install(FILES profile_simplebrowser.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqueror/profiles RENAME simplebrowser)

add_subdirectory(client)
add_subdirectory(iconview)
add_subdirectory(listview)
add_subdirectory(keditbookmarks)
add_subdirectory(shellcmdplugin)
add_subdirectory(about)
add_subdirectory(pics)
add_subdirectory(sidebar)
add_subdirectory(preloader)
add_subdirectory(quickprint)
add_subdirectory(remoteencodingplugin)
