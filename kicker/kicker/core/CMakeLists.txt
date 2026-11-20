include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../../libkicker"
  "${CMAKE_CURRENT_BINARY_DIR}/../../libkicker"
  "${CMAKE_CURRENT_SOURCE_DIR}/../ui"
  "${CMAKE_CURRENT_SOURCE_DIR}/../buttons"
  "${PROJECT_SOURCE_DIR}/libkonq"
)

kde3_add_library(kicker_core OBJECT
  SOURCES
    extensionSettings.kcfgc
    main.cpp
    kicker.cpp
    kicker.skel
    userrectsel.cpp
    containerarea.cpp
    applethandle.cpp
    container_base.cpp
    container_button.cpp
    container_applet.cpp
    container_extension.cpp
    extensionmanager.cpp
    menumanager.cpp
    pluginmanager.cpp
    showdesktop.cpp
    unhidetrigger.cpp
    containerarealayout.cpp
    panelextension.cpp
    panelextension.skel
  MOC
    applethandle.h
    container_applet.h
    container_base.h
    container_button.h
    container_extension.h
    containerarea.h
    extensionmanager.h
    kicker.h
    menumanager.h
    panelextension.h
    pluginmanager.h
    showdesktop.h
    unhidetrigger.h
    userrectsel.h
  LINK
    konq
    KDE3::kdeui
)

install(FILES
  default-apps
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker)

install(FILES
  childpanelextension.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/extensions)
