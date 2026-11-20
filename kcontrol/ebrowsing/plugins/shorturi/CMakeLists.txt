add_compile_definitions(QT_NO_CAST_ASCII)

kde3_add_module(libkshorturifilter
  SOURCES
    kshorturifilter.cpp
    kshorturifilter.skel
  MOC
    kshorturifilter.h
  LINK
    KDE3::kio
    KDE3::kdeui
)

install(FILES
  kshorturifilter.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

install(FILES
  kshorturifilterrc
  DESTINATION ${KDE3_CONFIG_INSTALL_DIR})
