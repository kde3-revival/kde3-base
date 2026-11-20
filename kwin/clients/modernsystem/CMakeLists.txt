include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
)

kde3_add_module(kwin3_modernsys
  SOURCES
    modernsys.cpp
  LINK
    kdecorations
)

install(FILES
  modernsystem.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kwin/)

add_subdirectory(config)
