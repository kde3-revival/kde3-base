# RandR 1.2+ display configuration shared by krandrstartup, kded_randr
# and the control module
kde3_add_library(randrcore STATIC
  SOURCES
    randrcore.cpp
    randrx11.cpp
  LINK
    KDE3::kdecore
    ${X11_Xrandr_LIB}
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

kde3_add_library(randrinternal OBJECT
  SOURCES
    ktimerdialog.cpp
    randr.cpp
  MOC
    ktimerdialog.h
    randr.h
  LINK
    KDE3::kdeui
    KDE3::kio
    KDE3::kutils
)

kde3_add_module(kcm_randr
  SOURCES
    krandrmodule.cpp
  MOC
    krandrmodule.h
  LINK
    randrinternal
    KDE3::kdeui
    ${LIB_XRANDR}
)

kde3_add_executable(krandrtray
  SOURCES
    main.cpp
    krandrtray.cpp
    krandrapp.cpp
    krandrpassivepopup.cpp
  MOC
    krandrapp.h
    krandrpassivepopup.h
    krandrtray.h
  LINK
    randrinternal
    KDE3::kio
    KDE3::kutils
    ${LIB_XRANDR}
)

install(FILES
  krandrtray.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

install(FILES
  randr.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/.hidden)
