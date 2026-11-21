# Built when KDEBASE_VOLUME_APPLET is on (ConfigureChecks.cmake, which
# also finds libpulse).

include_directories(
  "${PROJECT_SOURCE_DIR}/kicker/libkicker"
)

kde3_add_module(volume_panelapplet
  SOURCES
    kpulsemainloop.cpp
    pulsevolume.cpp
    volumebutton.cpp
    volumepopup.cpp
    volumeapplet.cpp
  MOC
    kpulsemainloop.h
    kpulsemainloop_p.h
    pulsevolume.h
    volumebutton.h
    volumepopup.h
    volumeapplet.h
  LINK
    kickermain
    KDE3::kio
    PkgConfig::LIBPULSE
)

install(FILES
  volumeapplet.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/applets)

# the panel button's states, loaded with KIconLoader("volumeapplet")
install(FILES
  volume.png
  volume-muted.png
  volume-error.png
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/volumeapplet/pics)

kde3_install_icons(kmix)
