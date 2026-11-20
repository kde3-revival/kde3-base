add_compile_definitions(_LARGEFILE64_SOURCE)

kde3_add_library(kxftconfig OBJECT
  SOURCES
    kxftconfig.cpp
  LINK
    ${LIBFONTCONFIG_LIBS}
    # all_includes and FREETYPE2_CFLAGS
    KDE3::kdecore
    ${LIBFREETYPE_LIBS}
)

kde3_add_module(kcm_fonts
  SOURCES
    fonts.cpp
  MOC
    fonts.h
  LINK
    kxftconfig
    krdb
    KDE3::kdeui
    ${LIBFONTCONFIG_LIBS}
    ${LIBFREETYPE_LIBS}
)

install(FILES
  fonts.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
