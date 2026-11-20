kde3_add_library(kdecorations SHARED VERSION_INFO 1:0:0
  SOURCES
    kdecoration.cpp
    kdecoration_p.cpp
    kdecoration_plugins_p.cpp
    kdecorationfactory.cpp
    kcommondecoration.cpp
  MOC
    kcommondecoration.h
    kdecoration.h
  LINK
    KDE3::kdecore
)

install(FILES
  kdecoration.h
  kdecoration_p.h
  kdecoration_plugins_p.h
  kdecorationfactory.h
  kcommondecoration.h
  DESTINATION ${KDE3_INCLUDE_INSTALL_DIR})
