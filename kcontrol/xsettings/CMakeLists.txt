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
