add_compile_definitions(LIBDIR=${KDE3_FULL_LIB_INSTALL_DIR})

kde3_add_module(kcm_crypto
  SOURCES
    crypto.cpp
    certexport.cpp
    kdatetimedlg.cpp
  MOC
    certexport.h
    crypto.h
    kdatetimedlg.h
  LINK
    KDE3::kdeui
    KDE3::kio
    ${LIBSSL}
)

install(FILES
  crypto.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
