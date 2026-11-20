include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../../taskmanager"
)

# libkasbar.la (the target name kasbar is the program)
kde3_add_library(libkasbar SHARED VERSION_INFO 1:0:0 OUTPUT_NAME kasbar
  SOURCES
    kasbar.cpp
    kasitem.cpp
    kastaskitem.cpp
    kastasker.cpp
    kaspopup.cpp
    kastaskpopup.cpp
    kasstartupitem.cpp
    kasaboutdlg.cpp
    kasprefsdlg.cpp
    kasclockitem.cpp
    kasgroupitem.cpp
    kasresources.cpp
    kasgrouper.cpp
    kasloaditem.cpp
  MOC
    kasaboutdlg.h
    kasbar.h
    kasclockitem.h
    kasgroupitem.h
    kasitem.h
    kasloaditem.h
    kaspopup.h
    kasprefsdlg.h
    kasresources.h
    kasstartupitem.h
    kastasker.h
    kastaskitem.h
    kastaskpopup.h
  LINK
    KDE3::kdeui
    taskmanager
)

kde3_add_module(kasbar_panelextension
  SOURCES
    kasbarextension.cpp
  MOC
    kasbarextension.h
  LINK
    libkasbar
)

kde3_add_executable(kasbar
  SOURCES
    kasbarapp.cpp
  LINK
    libkasbar
    KDE3Qt::Qt
    KDE3::kdecore
    KDE3::kdeui
)

install(FILES
  kasbarextension.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/extensions)
