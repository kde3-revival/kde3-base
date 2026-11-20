include_directories(
  "${PROJECT_SOURCE_DIR}/libkonq"
  "${PROJECT_SOURCE_DIR}/konqueror"
)

kde3_add_module(konq_listview
  SOURCES
    konq_listview.cc
    konq_listviewwidget.cc
    konq_listviewitems.cc
    konq_treeviewwidget.cc
    konq_treeviewitem.cc
    konq_textviewwidget.cc
    konq_textviewitem.cc
    konq_infolistviewwidget.cc
    konq_infolistviewitem.cc
    konq_listviewsettings.kcfgc
  MOC
    konq_infolistviewwidget.h
    konq_listview.h
    konq_listviewwidget.h
    konq_textviewwidget.h
    konq_treeviewwidget.h
  LINK
    konq
)

install(FILES
  konq_treeview.desktop
  konq_detailedlistview.desktop
  konq_textview.desktop
  konq_infolistview.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

install(FILES
  konq_treeview.rc
  konq_detailedlistview.rc
  konq_textview.rc
  konq_infolistview.rc
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqlistview)

install(FILES
  konq_listview.kcfg
  DESTINATION ${KDE3_KCFG_INSTALL_DIR})
