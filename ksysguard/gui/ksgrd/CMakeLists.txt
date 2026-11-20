include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../SensorDisplayLib"
)

kde3_add_library(ksgrd SHARED VERSION_INFO 3:0:2
  SOURCES
    HostConnector.cc
    SensorAgent.cc
    SensorManager.cc
    SensorShellAgent.cc
    SensorSocketAgent.cc
    StyleEngine.cc
    StyleSettings.cc
    TimerSettings.cc
  MOC
    HostConnector.h
    SensorAgent.h
    SensorManager.h
    SensorShellAgent.h
    SensorSocketAgent.h
    StyleEngine.h
    StyleSettings.h
    TimerSettings.h
  LINK
    KDE3::kdeui
)

install(FILES
  HostConnector.h
  SensorAgent.h
  SensorClient.h
  SensorManager.h
  SensorShellAgent.h
  SensorSocketAgent.h
  StyleEngine.h
  StyleSettings.h
  TimerSettings.h
  DESTINATION ${KDE3_INCLUDE_INSTALL_DIR}/ksgrd)
