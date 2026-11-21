# Built when KDEBASE_NETWORK_APPLET is on (ConfigureChecks.cmake, which
# also finds libnm).

include_directories(
  "${PROJECT_SOURCE_DIR}/kicker/libkicker"
)

kde3_add_module(network_panelapplet
  SOURCES
    networkbackend.cpp
    networkbutton.cpp
    networkdialogs.cpp
    networkicons.cpp
    networkpopup.cpp
    networkapplet.cpp
  MOC
    networkbackend.h
    networkbutton.h
    networkdialogs.h
    networkpopup.h
    networkapplet.h
  LINK
    kickermain
    KDE3::kio
    PkgConfig::LIBNM
)

install(FILES
  networkapplet.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/applets)
