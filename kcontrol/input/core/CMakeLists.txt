kde3_add_library(themepage OBJECT
  SOURCES
    themepage.cpp
  MOC
    themepage.h
  LINK
    KDE3::kio
    ${LIB_XCURSOR}
)
