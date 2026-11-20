include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
)

kde3_add_module(kwin3_b2
  SOURCES
    b2client.cpp
  MOC
    b2client.h
  LINK
    kdecorations
)

install(FILES
  b2.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kwin/)

add_subdirectory(config)
