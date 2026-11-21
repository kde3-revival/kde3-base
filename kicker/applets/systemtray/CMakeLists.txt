include_directories(
  "${PROJECT_SOURCE_DIR}/kicker/libkicker"
)

set(systemtray_SOURCES
  systemtrayapplet.cpp
  systemtrayapplet.skel)
set(systemtray_MOC systemtrayapplet.h)
set(systemtray_LINK kickermain KDE3::kdeui KDE3::kio)
# The icons of StatusNotifierItems (HAVE_STATUSNOTIFIER, ConfigureChecks.cmake)
if(HAVE_STATUSNOTIFIER)
  list(APPEND systemtray_SOURCES statusnotifierhost.cpp statusnotifieritem.cpp)
  list(APPEND systemtray_MOC statusnotifierhost.h statusnotifieritem.h)
  list(APPEND systemtray_LINK PkgConfig::GIO)
endif()

kde3_add_module(systemtray_panelapplet
  SOURCES ${systemtray_SOURCES}
  MOC ${systemtray_MOC}
  LINK ${systemtray_LINK}
)

install(FILES
  systemtrayapplet.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/applets)
