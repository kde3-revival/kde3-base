kde3_add_library(konq SHARED VERSION_INFO 6:0:2
  SOURCES
    konq_popupmenu.cc
    knewmenu.cc
    konq_xmlguiclient.cc
    kfileivi.cc
    konq_iconviewwidget.cc
    konq_settings.cc
    konq_drag.cc
    konq_operations.cc
    konq_dirpart.cc
    konq_propsview.cc
    konq_events.cc
    konq_bgnddlg.cc
    konq_undo.cc
    konq_undo.skel
    konq_historymgr.cc
    konq_historycomm.cc
    konq_historycomm.skel
    konq_pixmapprovider.cc
    kivdirectoryoverlay.cc
    konq_faviconmgr.cc
    konq_faviconmgr.skel
    konq_filetip.cc
  MOC
    kivdirectoryoverlay.h
    knewmenu.h
    konq_bgnddlg.h
    konq_dirpart.h
    konq_drag.h
    konq_faviconmgr.h
    konq_filetip.h
    konq_historymgr.h
    konq_iconviewwidget.h
    konq_operations.h
    konq_popupmenu.h
    konq_undo.h
  LINK
    KDE3::kparts
)

install(FILES
  directory_bookmarkbar.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kbookmark)

install(FILES
  konqpopupmenuplugin.desktop
  DESTINATION ${KDE3_SERVICETYPES_INSTALL_DIR})

install(FILES
  konq_popupmenu.h
  knewmenu.h
  kfileivi.h
  konq_drag.h
  konq_iconviewwidget.h
  konq_defaults.h
  konq_settings.h
  konq_operations.h
  libkonq_export.h
  konq_dirpart.h
  konq_propsview.h
  konq_events.h
  konq_undo.h
  konq_historymgr.h
  konq_historycomm.h
  konq_pixmapprovider.h
  kivdirectoryoverlay.h
  konq_faviconmgr.h
  konq_xmlguiclient.h
  konqbookmarkmanager.h
  konq_filetip.h
  DESTINATION ${KDE3_INCLUDE_INSTALL_DIR})

add_subdirectory(pics)
add_subdirectory(favicons)
