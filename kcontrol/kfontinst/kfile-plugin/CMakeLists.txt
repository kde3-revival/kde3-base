include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../lib"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../fonts"
)

kde3_add_module(kfile_font
  SOURCES
    KFileFont.cpp
  LINK
    KDE3::kio
    ${LIBFREETYPE_LIBS}
    libkfontinst
)

install(FILES
  kfile_font.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})
