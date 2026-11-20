include_directories(
  "${PROJECT_SOURCE_DIR}/kicker/libkicker"
)

kde3_add_module(clock_panelapplet
  SOURCES
    clock.skel
    clock.cpp
    datepicker.cpp
    zone.cpp
    analog.ui
    digital.ui
    fuzzy.ui
    settings.ui
    prefs.kcfgc
  MOC
    clock.h
  LINK
    kickermain
    KDE3::kdeui
)

install(FILES
  lcd.png
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/clockapplet/pics)

install(FILES
  clockapplet.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/applets)
