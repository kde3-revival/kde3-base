# KPowerManager, from the earlier kde3 tree.  Built when
# KDEBASE_POWER_APPLET is on (ConfigureChecks.cmake, which also finds
# libupower-glib and GIO).

include_directories(
  "${PROJECT_SOURCE_DIR}/kicker/libkicker"
)

kde3_add_module(kpowermanager_panelapplet
  SOURCES
    panelapplet.cpp
    upowerbackend.cpp
    backlight.cpp
    tooltip.cpp
    cpumonitor.cpp
  MOC
    panelapplet.h
    upowerbackend.h
    cpumonitor.h
  LINK
    kickermain
    KDE3::kio
    PkgConfig::UPOWER
    PkgConfig::GIO
)

install(FILES
  kpowermanager.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/applets)

# the panel icons, loaded with KIconLoader("kpowermanager"), and the
# applet's own icon
kde3_install_icons(ac battery battery_charging suspend hibernate
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kpowermanager/icons)
kde3_install_icons(kpowermanager)
