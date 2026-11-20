kde3_add_module(kcm_spellchecking
  SOURCES
    spellchecking.cpp
  MOC
    spellchecking.h
  LINK
    KDE3::kdeui
)

install(FILES
  spellchecking.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
