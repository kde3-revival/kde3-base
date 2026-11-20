include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
)

# tiles.h: the pixmaps of pics/ embedded by embedtool
file(GLOB _pics CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/pics/*.png")
list(SORT _pics)
add_custom_command(OUTPUT tiles.h
  COMMAND embedtool ${_pics}
  DEPENDS embedtool ${_pics}
  COMMENT "Generating tiles.h"
  VERBATIM)

kde3_add_module(kwin3_keramik
  SOURCES
    keramik.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/tiles.h"
  MOC
    keramik.h
  LINK
    KDE3::kdeui
    kdecorations
)

kde3_add_executable(embedtool
  NO_INSTALL
  SOURCES
    embedtool.cpp
  LINK
    KDE3Qt::Qt
)

install(FILES
  keramik.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kwin)

add_subdirectory(config)
