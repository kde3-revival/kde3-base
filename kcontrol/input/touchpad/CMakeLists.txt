# The touchpad settings (touchpadrc) and their application to the
# libinput X driver's devices: shared by the Touchpad tab of kcm_input
# and kded_touchpad, which applies them at login and to the touchpads
# connected later.
kde3_add_library(touchpadsettings STATIC
  SOURCES
    touchpadsettings.cpp
  LINK
    KDE3::kdecore
    ${X11_Xi_LIB}
    ${X11_X11_LIB}
)

kde3_add_module(kded_touchpad
  SOURCES
    touchpaddaemon.cpp
    touchpaddaemon.skel
  MOC
    touchpaddaemon.h
  LINK
    touchpadsettings
    KDE3::kdecore
)

install(FILES
  touchpad.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR}/kded)

# for tests on a real X server: lists the touchpads and their settings,
# applies touchpadrc; "selftest" checks touchpadrc without X
kde3_add_executable(touchpadtool NO_INSTALL
  SOURCES
    tests/touchpadtool.cpp
  LINK
    touchpadsettings
    KDE3::kdecore
)
add_test(NAME touchpadsettingstest COMMAND touchpadtool selftest)
