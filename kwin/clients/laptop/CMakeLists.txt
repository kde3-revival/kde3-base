include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
)

kde3_add_module(kwin3_laptop
  SOURCES
    laptopclient.cpp
  LINK
    kdecorations
)

install(FILES
  laptop.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kwin)
