include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
)

add_compile_definitions(QT_PLUGIN)

kde3_add_module(kwin3_plastik
  SOURCES
    plastik.cpp
    plastikclient.cpp
    plastikbutton.cpp
    misc.cpp
  MOC
    plastik.h
    plastikbutton.h
  LINK
    KDE3::kdeui
    kdecorations
)

install(FILES
  plastik.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kwin/)

add_subdirectory(config)
