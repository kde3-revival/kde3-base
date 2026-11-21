# RandR 1.2+ display configuration shared by krandrstartup, kded_randr
# and the control module
kde3_add_library(randrcore STATIC
  SOURCES
    randrcore.cpp
    randrx11.cpp
  LINK
    KDE3::kdecore
    ${X11_Xrandr_LIB}
    ${X11_Xext_LIB}
    ${X11_X11_LIB}
)

# applies the saved configuration at login (startkde)
kde3_add_executable(krandrstartup
  SOURCES
    krandrstartup.cpp
  LINK
    randrcore
)

# applies it when monitors are connected or disconnected
kde3_add_module(kded_randr
  SOURCES
    randrdaemon.cpp
    randrdaemon.skel
  MOC
    randrdaemon.h
  LINK
    randrcore
    KDE3::kio
)

install(FILES
  randrd.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR}/kded)

# tests of the configuration logic against a simulated X server
kde3_add_executable(randrtest NO_INSTALL
  SOURCES
    tests/randrtest.cpp
  LINK
    randrcore
)
add_test(NAME randrtest COMMAND randrtest)

# the monitor canvas of the control module
kde3_add_library(randrcanvas STATIC
  SOURCES
    monitorcanvas.cpp
  MOC
    monitorcanvas.h
  LINK
    randrcore
    KDE3::kdeui
)

# the canvas alone, to try by hand
kde3_add_executable(canvastest NO_INSTALL
  SOURCES
    tests/canvastest.cpp
  MOC
    tests/canvastest.cpp
  LINK
    randrcanvas
)

# the control module (Peripherals, Display, Monitors)
kde3_add_module(kcm_randr
  SOURCES
    displaymodule.cpp
    ktimerdialog.cpp
  MOC
    displaymodule.h
    ktimerdialog.h
  LINK
    randrcanvas
    KDE3::kdeui
)

install(FILES
  randr.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/.hidden)
