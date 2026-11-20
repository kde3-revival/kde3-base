add_compile_definitions(QT_NO_CAST_ASCII)

kde3_add_module(liblocaldomainurifilter
  SOURCES
    localdomainurifilter.cpp
    localdomainurifilter.skel
  MOC
    localdomainurifilter.h
  LINK
    KDE3::kio
    KDE3::kdeui
    KDE3::kdecore
)

kde3_add_executable(klocaldomainurifilterhelper
  SOURCES
    klocaldomainurifilterhelper.c
)

install(FILES
  localdomainurifilter.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})
