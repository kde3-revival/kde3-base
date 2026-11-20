include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
)

kde3_add_module(kwin3_redmond
  SOURCES
    redmond.cpp
  MOC
    redmond.h
  LINK
    KDE3::kdecore
    kdecorations
)

install(FILES
  redmond.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kwin)
