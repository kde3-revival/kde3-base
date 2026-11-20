kde3_add_library(kickermain SHARED VERSION_INFO 1:0:0
  SOURCES
    appletinfo.cpp
    global.cpp
    kickertip.cpp
    menuinfo.cpp
    panelbutton.cpp
    panner.cpp
    kickerSettings.kcfgc
    kshadowsettings.cpp
    kshadowengine.cpp
    paneldrag.cpp
    simplebutton.cpp
  MOC
    kickertip.h
    panelbutton.h
    panner.h
    simplebutton.h
  LINK
    KDE3::kio
)

install(FILES
  kickerSettings.kcfg
  DESTINATION ${KDE3_KCFG_INSTALL_DIR})
