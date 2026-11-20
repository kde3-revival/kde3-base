include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../lib"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../fonts"
)

add_compile_definitions(_LARGEFILE64_SOURCE)

kde3_add_module(kio_fonts
  SOURCES
    KioFonts.cpp
  LINK
    kxftconfig
    ${LIBFONTCONFIG_LIBS}
    ${LIBFREETYPE_LIBS}
    KDE3::kio
    KDE3::kdesu
    libkfontinst
)

install(FILES
  fonts.protocol
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

install(FILES
  fonts.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqsidebartng/virtual_folders/services)

install(FILES
  folder.desktop
  system-folder.desktop
  package.desktop
  DESTINATION ${KDE3_MIME_INSTALL_DIR}/fonts)
