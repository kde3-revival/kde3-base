include_directories(
  "${PROJECT_SOURCE_DIR}/kdm/kfrontend"
)

kde3_add_library(dmctl OBJECT
  SOURCES
    dmctl.cpp
  LINK
    KDE3::kdecore
    X11::Xau
)

kde3_add_module(kgreet_classic
  SOURCES
    kgreet_classic.cpp
  MOC
    kgreet_classic.h
  LINK
    KDE3::kdeui
)

kde3_add_module(kgreet_winbind
  SOURCES
    kgreet_winbind.cpp
  MOC
    kgreet_winbind.h
  LINK
    KDE3::kdeui
)

install(FILES
  kgreeterplugin.h
  DESTINATION ${KDE3_INCLUDE_INSTALL_DIR})
