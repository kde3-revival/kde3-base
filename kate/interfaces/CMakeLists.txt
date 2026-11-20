kde3_add_library(kateinterfacesprivate OBJECT
  SOURCES
    application.cpp
    mainwindow.cpp
    documentmanager.cpp
    viewmanager.cpp
    toolviewmanager.cpp
    pluginmanager.cpp
    plugin.cpp
    pluginconfiginterface.cpp
    pluginconfiginterfaceextension.cpp
  MOC
    application.h
    documentmanager.h
    mainwindow.h
    plugin.h
    pluginconfiginterfaceextension.h
    pluginmanager.h
    toolviewmanager.h
    viewmanager.h
  LINK
    KDE3::katepartinterfaces
)

install(FILES
  application.h
  documentmanager.h
  mainwindow.h
  plugin.h
  viewmanager.h
  pluginconfiginterface.h
  pluginconfiginterfaceextension.h
  toolviewmanager.h
  pluginmanager.h
  DESTINATION ${KDE3_INCLUDE_INSTALL_DIR}/kate)
