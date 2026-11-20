include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/ksgrd"
  "${CMAKE_CURRENT_SOURCE_DIR}/SensorDisplayLib"
  "${PROJECT_BINARY_DIR}/ksysguard/gui/SensorDisplayLib"
)

kde3_add_module(sysguard_panelapplet
  SOURCES
    KSysGuardApplet.cc
    KSGAppletSettings.cc
  MOC
    KSysGuardApplet.h
  LINK
    ksgrd
    sensordisplays
    KDE3::kdeui
    KDE3::kio
)

kde3_add_executable(ksysguard
  SOURCES
    SensorBrowser.cc
    WorkSheet.cc
    WorkSheetSettings.cc
    Workspace.cc
    ksysguard.cc
    ksysguard.skel
  MOC
    SensorBrowser.h
    WorkSheet.h
    WorkSheetSettings.h
    Workspace.h
    ksysguard.h
  LINK
    ksgrd
    sensordisplays
    KDE3::kdeui
    KDE3::kio
    KDE3::kdnssd
)

kde3_add_executable(kpm
  SOURCES
    kpm.c
)

install(FILES
  x-ksysguard.desktop
  DESTINATION ${KDE3_MIME_INSTALL_DIR}/application)

install(FILES
  ksysguardui.rc
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/ksysguard)

install(FILES
  ksysguard.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

install(FILES
  ksysguardapplet.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/applets)

install(FILES
  ProcessTable.sgrd
  SystemLoad.sgrd
  KSysGuardApplet.xml
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/ksysguard)

add_subdirectory(ksgrd)
add_subdirectory(SensorDisplayLib)
