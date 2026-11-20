include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
)

kde3_add_module(kwin3_quartz
  SOURCES
    quartz.cpp
  MOC
    quartz.h
  LINK
    KDE3::kdeui
    kdecorations
)

install(FILES
  quartz.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kwin/)

add_subdirectory(config)
