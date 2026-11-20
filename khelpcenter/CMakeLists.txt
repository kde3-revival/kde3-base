kde3_add_kdeinit_executable(khelpcenter
  SOURCES
    navigator.cpp
    navigatoritem.cpp
    navigatorappitem.cpp
    view.cpp
    searchwidget.cpp
    searchengine.cpp
    docmetainfo.cpp
    docentrytraverser.cpp
    formatter.cpp
    glossary.cpp
    toc.cpp
    mainwindow.cpp
    docentry.cpp
    htmlsearch.cpp
    history.cpp
    application.cpp
    searchwidget.skel
    treebuilder.cpp
    infotree.cpp
    mainwindow.skel
    kcmhelpcenter.cpp
    htmlsearchconfig.cpp
    kcmhelpcenter.skel
    fontdialog.cpp
    plugintraverser.cpp
    scrollkeepertreebuilder.cpp
    prefs.kcfgc
    searchhandler.cpp
  MOC
    fontdialog.h
    glossary.h
    history.h
    htmlsearch.h
    htmlsearchconfig.h
    infotree.h
    kcmhelpcenter.h
    mainwindow.h
    navigator.h
    scrollkeepertreebuilder.h
    searchengine.h
    searchhandler.h
    searchwidget.h
    toc.h
    treebuilder.h
    view.h
  LINK
    KDE3::khtml
)

kde3_add_executable(khc_indexbuilder
  SOURCES
    khc_indexbuilder.cpp
  MOC
    khc_indexbuilder.h
  LINK
    KDE3::kdecore
)

install(FILES
  khelpcenter.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

install(FILES
  Help.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

install(FILES
  khelpcenterui.rc
  intro.html.in
  glossary.html.in
  table-of-contents.xslt
  glossary.xslt
  index.html.in
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/khelpcenter)

install(FILES
  khelpcenter.kcfg
  DESTINATION ${KDE3_KCFG_INSTALL_DIR})

add_subdirectory(plugins)
add_subdirectory(pics)
add_subdirectory(searchhandlers)
