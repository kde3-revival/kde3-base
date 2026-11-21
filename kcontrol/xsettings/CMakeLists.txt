# The XSETTINGS manager of the KDE session (kded module): GTK and the
# toolkits that follow GTK get KDE's fonts, DPI and mouse settings.
kde3_add_library(xsettingsdata STATIC
  SOURCES
    xsettingsdata.cpp
  LINK
    KDE3::kdecore
)

kde3_add_module(kded_xsettings
  SOURCES
    xsettingsd.cpp
    xsettingsd.skel
  MOC
    xsettingsd.h
  LINK
    xsettingsdata
    KDE3::kdecore
    ${X11_X11_LIB}
)

# with GIO: the KDE-derived keys of the session's dconf database (see
# startkde), with the settings of the portal backend below
if(KDEBASE_SETTINGS_PORTAL)
  kde3_add_library(dconfsettings STATIC
    SOURCES
      dconfsettings.cpp
    LINK
      PkgConfig::GIO
  )
  target_link_libraries(kded_xsettings PRIVATE dconfsettings portalsettings)
  target_compile_definitions(kded_xsettings PRIVATE KDEBASE_DCONF_SYNC)

  # tests of the profile check and the changes; with --write, the writer
  # for tests/dconftest.sh (startkde's database, private session bus)
  kde3_add_executable(dconfsettingstest NO_INSTALL
    SOURCES
      tests/dconfsettingstest.cpp
    LINK
      dconfsettings
      portalsettings
      KDE3::kdecore
      PkgConfig::GIO
  )
  add_test(NAME dconfsettingstest COMMAND dconfsettingstest)
  find_program(DBUS_RUN_SESSION dbus-run-session)
  find_program(GSETTINGS gsettings)
  if(DBUS_RUN_SESSION AND GSETTINGS)
    add_test(NAME dconftest COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/dconftest.sh
      ${CMAKE_SOURCE_DIR}/startkde $<TARGET_FILE:dconfsettingstest>)
  endif()
endif()

install(FILES
  xsettings.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR}/kded)

# tests of the property format and of the mapping of the settings
kde3_add_executable(xsettingstest NO_INSTALL
  SOURCES
    tests/xsettingstest.cpp
  LINK
    xsettingsdata
)
add_test(NAME xsettingstest COMMAND xsettingstest)

# xdg-desktop-portal-kde3: the same settings, and the colours, for the
# Settings portal (libadwaita, GTK 4 on Wayland, Flatpak).
# xdg-desktop-portal finds kde3.portal in <datadir>/xdg-desktop-portal/portals
# of $XDG_DATA_HOME, $XDG_DATA_DIRS and its own prefix, and the session bus
# the D-Bus service file in <datadir>/dbus-1/services of the directories
# it was started with: for KDE installed elsewhere than /usr, both belong
# in /usr/share too (as the packages do).
if(KDEBASE_SETTINGS_PORTAL)
  kde3_add_library(portalsettings STATIC
    SOURCES
      portalsettings.cpp
    LINK
      xsettingsdata
      KDE3::kdecore
      PkgConfig::GIO
  )

  kde3_add_executable(xdg-desktop-portal-kde3
    SOURCES
      xdg-desktop-portal-kde3.cpp
    LINK
      portalsettings
      KDE3::kdecore
      PkgConfig::GIO
  )

  configure_file(org.freedesktop.impl.portal.desktop.kde3.service.in
    org.freedesktop.impl.portal.desktop.kde3.service @ONLY)
  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.kde3.service
    DESTINATION ${KDE3_DATA_ROOT_INSTALL_DIR}/dbus-1/services)
  install(FILES
    kde3.portal
    DESTINATION ${KDE3_DATA_ROOT_INSTALL_DIR}/xdg-desktop-portal/portals)

  # tests of the settings, their filtering and their changes
  kde3_add_executable(portalsettingstest NO_INSTALL
    SOURCES
      tests/portalsettingstest.cpp
    LINK
      portalsettings
      KDE3::kdecore
      PkgConfig::GIO
  )
  add_test(NAME portalsettingstest COMMAND portalsettingstest)
endif()
