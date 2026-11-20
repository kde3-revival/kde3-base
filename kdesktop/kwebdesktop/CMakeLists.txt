kde3_add_executable(kwebdesktop
  SOURCES
    kwebdesktop.cpp
    kwebdesktopsettings.kcfgc
  MOC
    kwebdesktop.h
  LINK
    KDE3::khtml
)

install(FILES
  kwebdesktop.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kdesktop/programs)

install(FILES
  kwebdesktop.kcfg
  DESTINATION ${KDE3_KCFG_INSTALL_DIR})
