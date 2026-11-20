# AM_CPPFLAGS = -UQT_NO_ASCII_CAST had no effect: automake puts
# AM_CPPFLAGS before the CXXFLAGS with KDE_DEFAULT_CXXFLAGS.

kde3_add_executable(krandom.kss
  SOURCES
    random.cpp
  MOC
    random.h
  LINK
    KDE3::kdeui
    m
)

kde3_add_executable(kblankscrn.kss
  SOURCES
    blankscrn.cpp
  MOC
    blankscrn.h
  LINK
    KDE3::kdeui
    KDE3::kscreensaver
    m
)

install(FILES
  KRandom.desktop
  KBlankscreen.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/System/ScreenSavers)
